#include <linux/module.h>
#include <linux/linkage.h>
#include <linux/init.h>
#include <linux/gfp.h>
#include <linux/highmem.h>
#include <linux/compiler.h>
#include <linux/linkage.h>
#include <linux/linkage.h>
#include <linux/init.h>
#include <asm/sections.h>
#include <linux/proc_fs.h>
#include <linux/slab.h>
#include <asm/page.h>
#include <linux/vmalloc.h>
#include <asm/fixmap.h>
#include <asm/memory.h>
#include <linux/blkdev.h>
#include <asm/page.h>
#include <linux/irqchip.h>
#include <linux/irqchip/arm-gic-common.h>
#include <linux/irqchip/arm-gic-v3.h>
#include <asm/arch_gicv3.h>
#include <linux/list.h>

#include <linux/hyplet.h>
#include <linux/hyplet_user.h>

extern pgd_t *hyp_pgd;


int create_hyp_user_mappings(void *from, void *to)
{
        unsigned long virt_addr;
        unsigned long fr = (unsigned long)from;
        unsigned long start = USER_TO_HYP((unsigned long)from);
        unsigned long end = USER_TO_HYP((unsigned long)to);


        start = start & PAGE_MASK;
        end = PAGE_ALIGN(end);
        printk("start %lx end %lx\n",start,end);

        for (virt_addr = start; virt_addr < end; virt_addr += PAGE_SIZE,fr += PAGE_SIZE) {
                int err;
                unsigned long pfn;

                pfn = kvm_uaddr_to_pfn(fr);
                if (pfn <= 0)
                        continue;

                err = __create_hyp_mappings(hyp_pgd, virt_addr,
                                            virt_addr + PAGE_SIZE,
                                            pfn,
                                            PAGE_HYP);
                if (err) {
                		hyplet_err("Failed to map %p\n",(void *)virt_addr);
                        return err;
                }
        }
        return 0;
}


struct hyp_addr* hyplet_get_addr_segment(long addr,struct hyplet_vm *tv)
{
	struct hyp_addr* tmp;

	if (list_empty(&tv->hyp_addr_lst))
			return NULL;

	list_for_each_entry(tmp,  &tv->hyp_addr_lst,lst) {

		long start = tmp->addr;
		long end = tmp->addr + tmp->size;

		if ( ( addr < end && addr >= start) )
			return tmp;

	}
	return NULL;
}

int __hyplet_map_user_data(void *umem,int size)
{
	int err;
	struct hyplet_vm *tv;
	struct hyp_addr* addr;
	unsigned long end_addr;
	unsigned long aligned_addr;

	aligned_addr = (unsigned long)umem & PAGE_MASK;
	end_addr = (unsigned long)umem + size;
	tv = hyplet_get_vm();

	err = create_hyp_user_mappings(umem, umem + size);
	if (err){
			hyplet_err(" failed to map to ttbr0_el2\n");
			return -1;
	}

	addr = kmalloc(sizeof(struct hyp_addr ), GFP_USER);
	addr->addr = (unsigned long)umem & PAGE_MASK;
	addr->size = PAGE_ALIGN((unsigned long)umem + size) - addr->addr;

	list_add(&addr->lst, &tv->hyp_addr_lst);

	hyplet_info("pid %d user mapped real (%p size=%d) in [%lx,%lx] size=%d\n",
			current->pid,umem ,size, addr->addr, addr->addr + addr->size ,addr->size );

	return 0;
}

/*
 *  scan the process's vmas to check that the memory is part of the process
 *  address space
 */
int hyplet_map_user_data(int __type, void *action)
{
	struct hyplet_map_addr *uaddr = (struct hyplet_map_addr *)action;
	struct hyplet_vm *tv;
	struct vm_area_struct* vma;
	int size;
	unsigned long umem;
	hyplet_ops type  = (hyplet_ops)__type;

	tv = hyplet_get_vm();

	size = uaddr->size;
	umem = (unsigned long)uaddr->addr & PAGE_MASK;

	printk("uaddr %lx umem %lx size %d\n",
			umem, uaddr->addr, size);

	if (hyplet_get_addr_segment(umem ,tv)) {
		hyplet_err(" address already mapped");
		return -1;
	}

	vma = current->active_mm->mmap;

	for (; vma ; vma = vma->vm_next) {

		printk("vma %lx .. %lx\n",vma->vm_start,vma->vm_end);

		if (vma->vm_start <= uaddr->addr && vma->vm_end >= uaddr->addr){

			if (vma->vm_flags & VM_EXEC
						&& type == HYPLET_MAP_CODE){
				tv->state |= USER_CODE_MAPPED;
			}

			if (type == HYPLET_MAP_STACK)
				tv->state |= USER_STACK_MAPPED;

			if (type == HYPLET_MAP_ANY)
				tv->state |= USER_MEM_ANON_MAPPED;

			if (size == -1) {
				uaddr->addr =  umem;
				size  = PAGE_SIZE;
			}
			return __hyplet_map_user_data((void *)uaddr->addr, size);

		}
	}

	return -1;
}

void hyplet_free_mem(struct hyplet_vm *tv)
{
        struct hyp_addr* tmp,*tmp2;

        list_for_each_entry_safe(tmp, tmp2, &tv->hyp_addr_lst, lst) {
        	hyp_user_unmap( tmp->addr , tmp->size);
                hyplet_call_hyp(hyplet_invld_tlb, tmp->addr);
                list_del(&tmp->lst);
                hyplet_info("unmap %lx\n",tmp->addr);
                kfree(tmp);
        }
}

