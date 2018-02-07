#include <linux/module.h>
#include <linux/linkage.h>
#include <linux/init.h>
#include <linux/gfp.h>
#include <linux/slab.h>
#include <asm/page.h>
#include <asm/cacheflush.h>
#include <linux/list.h>

#include <linux/hyplet.h>
#include <linux/hyplet_user.h>

extern pgd_t *hyp_pgd;


int create_hyp_user_mappings(void *_from, void *_to)
{
        unsigned long virt_addr;
        unsigned long from = (unsigned long)_from & PAGE_MASK;
        unsigned long start = USER_TO_HYP((unsigned long)_from);
        unsigned long end = USER_TO_HYP((unsigned long)_to);
        int nr_pages = 0;

        start = start & PAGE_MASK;
        end = PAGE_ALIGN(end);
        hyplet_info("start %lx end %lx\n",start,end);

        for (virt_addr = start; virt_addr < end; virt_addr += PAGE_SIZE,from += PAGE_SIZE) {
                int err;
                unsigned long pfn;

                pfn = kvm_uaddr_to_pfn(from);
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
                nr_pages++;
        }
        return nr_pages;
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

int __hyplet_map_user_data(void *umem,int size,int mem_type)
{

	struct hyplet_vm *tv;
	struct hyp_addr* addr;
	int pages = 0;

	tv = hyplet_get_vm();

	pages = create_hyp_user_mappings(umem, umem + size);
	if (pages <= 0){
			hyplet_err(" failed to map to ttbr0_el2\n");
			return -1;
	}

	addr = kmalloc(sizeof(struct hyp_addr ), GFP_USER);
	addr->addr = (unsigned long)umem;
	addr->size = size;
	addr->type = mem_type;
	addr->nr_pages = pages;
	list_add(&addr->lst, &tv->hyp_addr_lst);

	hyplet_info("pid %d user mapped %p size=%d pages=%d\n",
			current->pid,umem ,size, addr->nr_pages );

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
	int mem_type = 0;
	struct vm_area_struct* vma;
	int size = uaddr->size;
	hyplet_ops type  = (hyplet_ops)__type;

	tv = hyplet_get_vm();

	if (hyplet_get_addr_segment(uaddr->addr ,tv)) {
		hyplet_err(" address %lx already mapped\n",uaddr->addr);
		return -1;
	}

	vma = current->active_mm->mmap;

	for (; vma ; vma = vma->vm_next) {
		if (vma->vm_start <= uaddr->addr && vma->vm_end >= uaddr->addr){

			if (vma->vm_flags & VM_EXEC
						&& type == HYPLET_MAP_CODE){
				tv->state |= USER_CODE_MAPPED;
				mem_type = USER_CODE_MAPPED;
			}

			if (type == HYPLET_MAP_STACK){
				tv->state |= USER_STACK_MAPPED;
				mem_type = USER_STACK_MAPPED;
			}

			if (type == HYPLET_MAP_ANY){
				tv->state |= USER_MEM_ANON_MAPPED;
				mem_type = USER_MEM_ANON_MAPPED;
			}

			if (size == -1) {
				size  = PAGE_SIZE;
				mem_type = USER_NO_SIZE;
			}
			return __hyplet_map_user_data((void *)uaddr->addr, size, mem_type);

		}
	}

	return -1;
}

void hyplet_free_mem(struct hyplet_vm *tv)
{
        struct hyp_addr* tmp,*tmp2;
        int i;


        list_for_each_entry_safe(tmp, tmp2, &tv->hyp_addr_lst, lst) {

        	hyplet_info("unmap %lx, %lx size=%d pages=%d\n",
        			tmp->addr, tmp->addr & PAGE_MASK,
					tmp->size,  tmp->nr_pages);

        	for ( i = 0 ; i < tmp->nr_pages ; i++){
        		unsigned long addr = ( tmp->addr & PAGE_MASK )+ PAGE_SIZE * (i-1);
        		hyplet_user_unmap( addr );
        	}

        	hyplet_call_hyp(hyplet_invld_tlb,  tmp->addr);
    		if (tmp->type & (HYPLET_MAP_STACK | HYPLET_MAP_ANY ) ){
    			__flush_dcache_area((void *)tmp->addr, tmp->size);
    		}

    		if (tmp->type & USER_CODE_MAPPED ){
    				flush_icache_range(tmp->addr,
    							tmp->addr + tmp->size);
    		}

    		list_del(&tmp->lst);
            kfree(tmp);
        }
}

