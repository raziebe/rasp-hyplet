#include <linux/module.h>
#include <linux/linkage.h>
#include <linux/init.h>
#include <linux/gfp.h>
#include <linux/slab.h>
#include <asm/cacheflush.h>
#include <linux/list.h>

#include <linux/highmem.h>
#include <linux/compiler.h>


#include <linux/hyplet.h>
#include <linux/hyplet_user.h>
#include <linux/tp_mmu.h>

extern pgd_t *hyp_pgd;

unsigned long kvm_uaddr_to_pfn(unsigned long uaddr)
{
	unsigned long pfn;
	struct page *pages[1];
	int nr;

	nr = get_user_pages_fast(uaddr,1, 0, (struct page **)&pages);
	if (nr <= 0){
	     //  printk("TP: INSANE: failed to get user pages %p\n",(void *)uaddr);
	       return 0x00;
	}
	pfn = page_to_pfn(pages[0]);
	put_page(pages[0]);
	return pfn;
}

/**
 * create_hyp_user_mappings - duplicate a user virtual address range in Hyp mode
 * @from:	The virtual kernel start address of the range
 * @to:		The virtual kernel end address of the range (exclusive)
 *
 * The same virtual address as the kernel virtual address is also used
 * in Hyp-mode mapping (modulo HYP_PAGE_OFFSET) to the same underlying
 * physical pages.
 */
int create_hyp_user_mappings(void *from, void *to,pgprot_t prot)
{
	unsigned long virt_addr;
	unsigned long fr = (unsigned long)from;
	unsigned long start = USER_TO_HYP((unsigned long)from);
	unsigned long end = USER_TO_HYP((unsigned long)to);
	int mapped = 0;

	start = start & PAGE_MASK;
	end = PAGE_ALIGN(end);

	for (virt_addr = start; virt_addr < end; virt_addr += PAGE_SIZE,fr += PAGE_SIZE) {
		int err;
		unsigned long pfn;

		pfn = kvm_uaddr_to_pfn(fr);
		if (pfn <= 0)
			continue;

		err = __create_hyp_mappings(hyp_pgd, virt_addr,
					    virt_addr + PAGE_SIZE,
					    pfn,
						prot);
		if (err) {
			printk("TP: Failed to map %p\n",(void *)virt_addr);
			return mapped;
		}
		mapped++;
	}

	return mapped;
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

int __hyplet_map_user_data(long umem,int size,int flags,struct hyplet_vm *hyp)
{
	struct hyp_addr* addr;
	int pages = 0;

	pages = create_hyp_user_mappings((void *)umem, (void *)(umem + size), PAGE_HYP_RW_EXEC);
	if (pages <= 0){
			hyplet_err(" failed to map to ttbr0_el2\n");
			return -1;
	}

	addr = kmalloc(sizeof(struct hyp_addr ), GFP_USER);
	addr->addr = (unsigned long)umem;
	addr->size = pages * PAGE_SIZE;
	addr->flags = flags;
	addr->nr_pages = pages;
	list_add(&addr->lst, &hyp->hyp_addr_lst);

//	hyplet_info("pid %d user mapped %lx size=%d pages=%d\n",
	//		current->pid,umem ,size, addr->nr_pages );

	if (flags & VM_EXEC) {
		hyp->state  |= USER_CODE_MAPPED;
	}
	return 0;
}

/*
 *  scan the process's vmas and map all possible pages
 */
int hyplet_map_user(struct hyplet_vm *hyp)
{
	struct vm_area_struct* vma;

	vma = current->active_mm->mmap;

	for (; vma ; vma = vma->vm_next) {
		long start = vma->vm_start;
		for ( ; start < vma->vm_end ; start += PAGE_SIZE){
			 __hyplet_map_user_data(start, PAGE_SIZE, vma->vm_flags, hyp);
		}
	}
	return 0;
}

int hyplet_check_mapped(struct hyplet_vm *hyp,void *action)
{
	struct hyplet_map_addr *uaddr = (struct hyplet_map_addr *)action;

	if (hyplet_get_addr_segment(uaddr->addr ,hyp)) {
		hyplet_err(" address %lx already mapped\n",uaddr->addr);
		return 1;
	}
	return 0;
}

void hyplet_free_mem(struct hyplet_vm *tv)
{
        struct hyp_addr* tmp,*tmp2;

        list_for_each_entry_safe(tmp, tmp2, &tv->hyp_addr_lst, lst) {

/*
        	hyplet_info("unmap %lx/%lx size=%d "
        			"pages=%d flags=%x\n",
        		tmp->addr,
				tmp->addr & PAGE_MASK,
				tmp->size,
				tmp->nr_pages, tmp->flags);
*/

        	hyp_user_unmap( tmp->addr , PAGE_SIZE,  1 );
        	hyplet_call_hyp(hyplet_invld_tlb,  tmp->addr);

         	if (tmp->flags & VM_EXEC)
        			flush_icache_range(tmp->addr, tmp->addr + tmp->size);

         	if (tmp->flags & (VM_READ | VM_WRITE)){
    			__flush_cache_user_range(tmp->addr, tmp->addr + tmp->size);
		}
         	list_del(&tmp->lst);
        	kfree(tmp);
        }
        hyplet_call_hyp(hyplet_invld_all_tlb);
}

