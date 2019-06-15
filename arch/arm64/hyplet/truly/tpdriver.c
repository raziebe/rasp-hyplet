#include <linux/module.h>    // included for all kernel modules
#include <linux/kernel.h>    // included for KERN_INFO
#include <linux/init.h>      // included for __init and __exit macros
#include <linux/slab.h>
#include <linux/pid.h>
#include <linux/kthread.h>
#include <linux/sched.h>
#include <linux/wait.h>
#include <linux/path.h>
#include <linux/highmem.h>
#include <linux/mm.h>
#include <linux/syscalls.h>
#include <linux/kmod.h>
#include <linux/fdtable.h>
#include <linux/file.h>
#include <linux/fs.h>
#include <linux/vmalloc.h>
#include <linux/elf.h>
#include <asm/uaccess.h>
#include <asm/unistd.h>
#include <linux/vmalloc.h>
#include <asm/tlbflush.h>
#include <linux/kallsyms.h>
#include <linux/hyplet.h>

#include "common.h"
#include "exec_prot_db_linux.h"
#include "ImageManager.h"
#include "elf.h"
#include "linux_kernfile.h"

#define     MAX_PERMS 777

void tp_init_procfs(IMAGE_MANAGER *_imgmgr);

struct img_layout_info {
    size_t pid;
    size_t base;
    size_t per_cpu_base;
    size_t length;
    char*  path;
    enum ep_module_group grp_type;
};

IMAGE_MANAGER         image_manager;

static void fill_img_layout_info (struct img_layout_info* info,
                                  size_t pid,
                                  size_t base,
                                  size_t per_cpu_base,
                                  size_t length,
                                  char*  path,
                                  enum ep_module_group grp_type)
{
    info->pid = pid;
    info->base = base;
    info->per_cpu_base = per_cpu_base;
    info->length = length;
    info->path = path;
    info->grp_type = grp_type;
}


static char* get_path_from_file(struct file* file, char** path_to_free)
{
    struct path* path;
    char* the_path, *path_name;

    if (!file)
        return NULL;

    path_name = the_path = NULL;
    path = &file->f_path;
    path_get(path);

    if ((the_path = tp_alloc(PAGE_SIZE*2)) == NULL) {
        path_put(path);
        goto end;
    }

    path_name = d_path(path, the_path, PAGE_SIZE);
    path_put(path);

    if (IS_ERR(path_name)) {
        tp_free(the_path);
        path_name = NULL;
        goto end;
    }
    *path_to_free = the_path;
    end:
    return path_name;
}

static enum ep_module_group get_group_by_arch_and_type (char arch, char type)
{
    if (arch == ELF_ARCH_64)
        return type == ELF_TYPE_EXEC ? EP_MOD_EXEC64 : EP_MOD_SO64;

    return type == ELF_TYPE_EXEC ? EP_MOD_EXEC32 : EP_MOD_SO32;
}


static void vma_tp_load(struct vm_area_struct* vma, void* context)
{
    char* path, *path_to_free;

    // Ignore non-executable vm areas
    if (!(vma->vm_flags & VM_EXEC)){
        return;
    }

    path = get_path_from_file(vma->vm_file, &path_to_free);

    if (path != NULL){
        struct img_layout_info info;
        struct file* file;
        char arch, type;
        enum ep_module_group grp_type;

        if ((file = file_open(path, O_RDONLY, MAX_PERMS)) == NULL) {
            file = vma->vm_file;
        }

        if (!get_elf_data(file, &arch, &type))
            goto clean;

        grp_type = get_group_by_arch_and_type(arch, type);

        fill_img_layout_info (&info,
                              current->pid,
                              vma->vm_start,
                              0,
                              vma->vm_end - vma->vm_start,
                              path,
                              grp_type);
clean:
        if (file != NULL && file != vma->vm_file)
            file_close(file);
        tp_free(path_to_free);
    }
}

void for_each_vma(struct task_struct* task, void* context, void (*callback)(struct vm_area_struct*, void*))
{
    struct vm_area_struct* p;

    if (!(task && task->mm && task->mm->mmap))
        return;
    for (p = task->mm->mmap; p ; p = p->vm_next)
        callback(p, context);
}

static char* executable_path(struct task_struct* process, char** path_to_free)
{
    #define PATH_MAX 4096
    char* p = NULL, *pathname;
    struct mm_struct* mm = current->mm;
    if (mm){
        if (mm->exe_file) {
            pathname = kmalloc(PATH_MAX*2, GFP_ATOMIC);
            *path_to_free = pathname;
            if (pathname)
                p = d_path(&mm->exe_file->f_path, pathname, PATH_MAX);
        }
    }
    #undef PATH_MAX
    return IS_ERR(p) ? NULL : p;
}

void* get_image_file(void)
{
	return image_manager.first_active_image;
}

void tp_execve_handler(unsigned long ret_value)
{
    char* exec_path, *path_to_free = NULL;
    PIMAGE_FILE img;
    struct file* file;
    unsigned long base, size;

    if (current->in_execve)
        goto clean;

    if ((exec_path = executable_path(current, &path_to_free)) == NULL)
        goto clean;

    base = current->mm->mmap->vm_start;
    size = current->mm->mmap->vm_end - base;

    for_each_vma(current, (void*)&base, vma_tp_load);
    base = current->mm->mmap->vm_start;

    if ((file = file_open(exec_path, O_RDONLY, MAX_PERMS)) == NULL)
        goto clean;

    img = image_file_init(file);
    if(img){
    	im_add_image(&image_manager,current->pid, img);
    	printk("Found %d to be protected\n",current->pid);
   }

   file_close(file);
clean:
   if (path_to_free)
      tp_free(path_to_free);
}


int tp_put_nop(void)
{
	PIMAGE_FILE img;
//	unsigned short nop_16bit = 0xbf00;
  	unsigned int nop_32bit = 0xe1a00000;
	char *kaddr;
	char *in_page_kaddr;
	long offset;

	if (!image_manager.first_active_image)
		return -1;

	img = image_manager.first_active_image;
	if (!img->trap.bkpt_page)
		return -1;

	kaddr =  (char *)kmap(img->trap.bkpt_page);
	offset = img->trap.elr_el2  & ~PAGE_MASK;
	in_page_kaddr = kaddr + offset;
	
	printk("Put nop\n");
	memcpy(in_page_kaddr, &nop_32bit ,sizeof(nop_32bit));
	kunmap( img->trap.bkpt_page );
	return 0;
}

int tp_put_trap(void)
{
	PIMAGE_FILE img;
//	unsigned short bkpt3_16it = 0xbe03;
	unsigned int bkpt_32bit = 0xe1200073;
	char *kaddr;
	char *in_page_kaddr;
	long offset;

	if (!image_manager.first_active_image)
		return -1;

	img = image_manager.first_active_image;

	if (!img->trap.bkpt_page)
		return -1;
	
	kaddr =  (char *)kmap(img->trap.bkpt_page);
	offset = img->trap.elr_el2  & ~PAGE_MASK;
	in_page_kaddr = kaddr + offset;
	
	printk("Put trap\n");
	memcpy(in_page_kaddr, &bkpt_32bit ,sizeof(bkpt_32bit));

	kunmap( img->trap.bkpt_page );
	return 0;
}


int locate_trap_code(PIMAGE_FILE img)
{
	struct hyplet_vm *vm = hyplet_get_vm();

	if (vm->elr_el2 == 0)
		return 0;

	if (img->trap.bkpt_page == 0) {
		struct page *pg[1];
		int nr;

		nr = get_user_pages_fast(vm->elr_el2, 1, 0, (struct page **)&pg);
		if (nr <= 0) {
			printk("CFLAT: Failed to get page\n");
			return 0;
		}
		img->trap.elr_el2 =  vm->elr_el2;
		img->trap.bkpt_page =  pg[0];
		printk("CFLAT: ELR_EL2=%lx\n", vm->elr_el2);
		img->flags |= CFLAT_FLG_TRAP_MAPPED;
	}
	return 1;
}

void tp_context_switch(struct task_struct *prev,struct task_struct *next)
{
	PIMAGE_FILE img;

	if (!image_manager.first_active_image)
		return;

	img = image_manager.first_active_image;
	if (!(prev->pid == img->pid && current->pid == prev->pid)){
		return;
	}

	printk("CFLAT: Switching prev %d current = %d\n",
			img->pid,current->pid);
	
	if (!(img->flags & CFLAT_FLG_TRAP_MAPPED)){
		if (!locate_trap_code(img) )
			return;
	}
}

void tp_handler_exit(struct task_struct *tsk)
{
	if (!im_is_process_exists(&image_manager,tsk->pid))
		return;

	printk("exiting...\n");
	tp_put_trap();
	im_remove_process(&image_manager,tsk->pid);
}

static int __init tp_init(void)
{
    im_init(&image_manager, NULL, MemoryLayoutInit(TRUE));
    tp_init_procfs(&image_manager);
    return 0;
}

module_init(tp_init);
