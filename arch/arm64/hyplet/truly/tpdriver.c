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
#include <linux/version.h>
#include <linux/kallsyms.h>
#include <linux/hyplet.h>

#include "common.h"
#include "exec_prot_db_linux.h"
#include "ImageManager.h"
#include "elf.h"
#include "linux_kernfile.h"

#define     MAX_PERMS 777


struct img_layout_info
{
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

void put_hook_on_attest(PIMAGE_FILE img)
{
	memcpy((char *)(img->attest.uaddr) ,(char *)(img->hooked.kaddr_copy),img->attest.size);
}

void put_attest_back(PIMAGE_FILE img)
{
	memcpy((char *)(img->attest.uaddr) ,(char *)(img->attest.kaddr_copy),img->attest.size);
}

int setup_sections(PIMAGE_FILE img)
{
	/*
	 *  Copy.attest to kernel. We do not walk on the pages here.
	 *    The reason is because pages in 32bit over 64bit kernel are 2MB.
	 *    So, there is no real kmap infrastructure for 2MB. so , we just memcpy
	 *    and make sure the memcpy is in the context of verified process.
	 */
	img->hooked.kaddr_copy = vmalloc(img->hooked.size);
	memcpy(img->hooked.kaddr_copy, img->hooked.uaddr, img->hooked.size);

	img->attest.kaddr_copy = vmalloc(img->attest.size);
	memcpy(img->attest.kaddr_copy,(void *)img->attest.uaddr, img->attest.size);

	put_hook_on_attest(img);
	return 0;
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
    	setup_sections(img);
    	printk("Found %d to be protected\n",current->pid);
   }

   file_close(file);
clean:
   if (path_to_free)
      tp_free(path_to_free);
}


void tp_handler_exit(struct task_struct *tsk)
{
	extern IMAGE_MANAGER image_manager;

	if (!im_is_process_exists(&image_manager,tsk->pid))
			return;
	put_attest_back(get_image_file());
	im_remove_process(&image_manager,tsk->pid);
}

static int __init tp_init(void)
{
    im_init(&image_manager, NULL, MemoryLayoutInit(TRUE));
    return 0;
}

module_init(tp_init);
