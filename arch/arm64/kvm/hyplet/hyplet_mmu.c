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


extern pgd_t *hyp_pgd;


void create_level_three(struct page *pg, long *addr)
{
	int i;
	long *l3_descriptor;

	l3_descriptor = (long *) kmap(pg);
	if (l3_descriptor == NULL) {
		hyplet_err("%s desc NULL\n", __func__);
		return;
	}

	for (i = 0; i < PAGE_SIZE / sizeof(long long); i++) {
		/*
		 * see page 1781 for details
		 */
		l3_descriptor[i] = (DESC_AF) |
			(0b11 << DESC_SHREABILITY_SHIFT) |
			(0b11 << DESC_S2AP_SHIFT) | (0b1111 << 2) |	/* leave stage 1 un-changed see 1795 */
		   	 DESC_TABLE_BIT | DESC_VALID_BIT | (*addr);

		(*addr) += PAGE_SIZE;

	}
	kunmap(pg);
}

// 1GB
void create_level_two(struct page *pg, long *addr)
{
	int i;
	long *l2_descriptor;
	struct page *pg_lvl_three;

	l2_descriptor = (long *) kmap(pg);
	if (l2_descriptor == NULL) {
		hyplet_err("%s desc NULL\n", __func__);
		return;
	}

	pg_lvl_three = alloc_pages(GFP_KERNEL | __GFP_ZERO, 9);
	if (pg_lvl_three == NULL) {
		hyplet_err("%s alloc page NULL\n", __func__);
		return;
	}

	for (i = 0; i < PAGE_SIZE / (sizeof(long)); i++) {
		// fill an entire 2MB of mappings
		create_level_three(pg_lvl_three + i, addr);
		// calc the entry of this table
		l2_descriptor[i] =
		    (page_to_phys(pg_lvl_three + i)) | DESC_TABLE_BIT |
		    DESC_VALID_BIT;

		//hyplet_info("L2 IPA %lx\n", l2_descriptor[i]);
	}

	kunmap(pg);
}

void create_level_one(struct page *pg, long *addr)
{
	int i;
	long *l1_descriptor;
	struct page *pg_lvl_two;

	l1_descriptor = (long *) kmap(pg);
	if (l1_descriptor == NULL) {
		hyplet_err("%s desc NULL\n", __func__);
		return;
	}

	pg_lvl_two = alloc_pages(GFP_KERNEL | __GFP_ZERO, 3);
	if (pg_lvl_two == NULL) {
		hyplet_err("%s alloc page NULL\n", __func__);
		return;
	}

	for (i = 0; i < 8; i++) {
		get_page(pg_lvl_two + i);
		create_level_two(pg_lvl_two + i, addr);
		l1_descriptor[i] =
		    (page_to_phys(pg_lvl_two + i)) | DESC_TABLE_BIT |
		    DESC_VALID_BIT;

	}
	kunmap(pg);
}

void create_level_zero(struct hyplet_vm *tvm, struct page *pg, long *addr)
{
	struct page *pg_lvl_one;
	long *l0_descriptor;;

	pg_lvl_one = alloc_page(GFP_KERNEL | __GFP_ZERO);
	if (pg_lvl_one == NULL) {
		hyplet_err("%s alloc page NULL\n", __func__);
		return;
	}

	get_page(pg_lvl_one);
	create_level_one(pg_lvl_one, addr);

	l0_descriptor = (long *) kmap(pg);
	if (l0_descriptor == NULL) {
		hyplet_err("%s desc NULL\n", __func__);
		return;
	}

	memset(l0_descriptor, 0x00, PAGE_SIZE);

	l0_descriptor[0] =
	    (page_to_phys(pg_lvl_one)) | DESC_TABLE_BIT | DESC_VALID_BIT;

	tvm->pg_lvl_one = (void *) pg_lvl_one;

	hyplet_info("L0 IPA %lx\n", l0_descriptor[0]);

	kunmap(pg);

}

unsigned long hyplet_create_pg_tbl(void *cxt)
{
	struct hyplet_vm *tvm = (struct hyplet_vm *) cxt;
	long addr = 0;
	long vmid = 012;
	struct page *pg_lvl_zero;
	int starting_level = 1;
/*
 tosz = 25 --> 39bits 64GB
	0-11
2       12-20   :512 * 4096 = 2MB per entry
1	21-29	: 512 * 2MB = per page
0	30-35 : 2^5 entries	, each points to 32 pages in level 1
 	pa range = 1 --> 36 bits 64GB

*/
	pg_lvl_zero = alloc_page(GFP_KERNEL | __GFP_ZERO);
	if (pg_lvl_zero == NULL) {
		hyplet_err("%s alloc page NULL\n", __func__);
		return 0x00;
	}

	get_page(pg_lvl_zero);
	create_level_zero(tvm, pg_lvl_zero, &addr);

	if (starting_level == 0)
		tvm->vttbr_el2 = page_to_phys(pg_lvl_zero) | (vmid << 48);
	else
		tvm->vttbr_el2 =
		    page_to_phys((struct page *) tvm->pg_lvl_one) | (vmid <<     48);

	return tvm->vttbr_el2;
}


void make_vtcr_el2(struct hyplet_vm *tvm)
{
	long vtcr_el2_t0sz;
	long vtcr_el2_sl0;
	long vtcr_el2_irgn0;
	long vtcr_el2_orgn0;
	long vtcr_el2_sh0;
	long vtcr_el2_tg0;
	long vtcr_el2_ps;

	vtcr_el2_t0sz = hyplet_get_tcr_el1() & 0b111111;
	vtcr_el2_sl0 = 0b01;	//IMPORTANT start at level 1.  D.2143 + D4.1746
	vtcr_el2_irgn0 = 0b1;
	vtcr_el2_orgn0 = 0b1;
	vtcr_el2_sh0 = 0b11;	// inner sharable
	vtcr_el2_tg0 = (hyplet_get_tcr_el1() & 0xc000) >> 14;
	vtcr_el2_ps = (hyplet_get_tcr_el1() & 0x700000000) >> 32;

	tvm->vtcr_el2 = (vtcr_el2_t0sz) |
	    (vtcr_el2_sl0 << VTCR_EL2_SL0_BIT_SHIFT) |
	    (vtcr_el2_irgn0 << VTCR_EL2_IRGN0_BIT_SHIFT) |
	    (vtcr_el2_orgn0 << VTCR_EL2_ORGN0_BIT_SHIFT) |
	    (vtcr_el2_sh0 << VTCR_EL2_SH0_BIT_SHIFT) |
	    (vtcr_el2_tg0 << VTCR_EL2_TG0_BIT_SHIFT) |
	    (vtcr_el2_ps << VTCR_EL2_PS_BIT_SHIFT);
}



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
	tv = hyplet_vm();

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
int hyplet_map_user_data(hyplet_ops type, void *action)
{
	struct hyplet_map_addr *uaddr = (struct hyplet_map_addr *)action;
	struct hyplet_vm *tv;
	struct vm_area_struct* vma;
	int size;
	unsigned long umem;


	tv = hyplet_vm();

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

void hyplet_free_mem(void)
{
        struct hyplet_vm *tv = hyplet_vm();
        struct hyp_addr* tmp,*tmp2;

        list_for_each_entry_safe(tmp, tmp2, &tv->hyp_addr_lst, lst) {
        		hyp_user_unmap( tmp->addr , tmp->size);
                hyplet_call_hyp(hyplet_invld_tlb, tmp->addr);
                list_del(&tmp->lst);
                hyplet_info("unmap %lx\n",tmp->addr);
                kfree(tmp);
        }
}

