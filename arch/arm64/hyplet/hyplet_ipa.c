#include <linux/module.h>
#include <linux/highmem.h>
#include <linux/hyplet.h>
#include <linux/delay.h>
#include "hyp_mmu.h"
#include "hypletS.h"

//
// alloc 512 * 4096  = 2MB
//
void create_level_three(struct page *pg, long *addr)
{
	int i;
	long *l3_descriptor;

	l3_descriptor = (long *) kmap(pg);
	if (l3_descriptor == NULL) {
		printk("%s desc NULL\n", __func__);
		return;
	}

	for (i = 0; i < PAGE_SIZE / sizeof(long long); i++) {
		/*
		 * see page 1781 for details
		 */
		l3_descriptor[i] = (DESC_AF) |
			(0b11 << DESC_SHREABILITY_SHIFT) |
			(0b11 << DESC_S2AP_SHIFT) | (0b1111 << 2) |	/* leave stage 1 unchanged see 1795 */
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
		printk("%s desc NULL\n", __func__);
		return;
	}

	pg_lvl_three = alloc_pages(GFP_KERNEL | __GFP_ZERO, 9);
	if (pg_lvl_three == NULL) {
		printk("%s alloc page NULL\n", __func__);
		return;
	}

	for (i = 0; i < PAGE_SIZE / (sizeof(long)); i++) {
		// fill an entire 2MB of mappings
		create_level_three(pg_lvl_three + i, addr);
		// calc the entry of this table
		l2_descriptor[i] =
		    (page_to_phys(pg_lvl_three + i)) | DESC_TABLE_BIT |
		    DESC_VALID_BIT;
	}

	kunmap(pg);
}

void create_level_one(struct page *pg, long *addr)
{
	int i;
	long *l1_descriptor;
	struct page *pg_lvl_two;
	int lvl_two_nr_pages =16;

	l1_descriptor = (long *) kmap(pg);
	if (l1_descriptor == NULL) {
		printk("%s desc NULL\n", __func__);
		return;
	}

	pg_lvl_two = alloc_pages(GFP_KERNEL | __GFP_ZERO, 4);
	if (pg_lvl_two == NULL) {
		printk("%s alloc page NULL\n", __func__);
		return;
	}

	for (i = 0; i < lvl_two_nr_pages ; i++) {
		get_page(pg_lvl_two + i);
		create_level_two(pg_lvl_two + i, addr);
		l1_descriptor[i] = (page_to_phys(pg_lvl_two + i)) | DESC_TABLE_BIT | DESC_VALID_BIT;

	}
	kunmap(pg);
}

void create_level_zero(struct hyplet_vm *vm, struct page *pg, long *addr)
{
	struct page *pg_lvl_one;
	long *l0_descriptor;;

	pg_lvl_one = alloc_page(GFP_KERNEL | __GFP_ZERO);
	if (pg_lvl_one == NULL) {
		printk("%s alloc page NULL\n", __func__);
		return;
	}

	get_page(pg_lvl_one);
	create_level_one(pg_lvl_one, addr);

	l0_descriptor = (long *) kmap(pg);
	if (l0_descriptor == NULL) {
		printk("%s desc NULL\n", __func__);
		return;
	}

	memset(l0_descriptor, 0x00, PAGE_SIZE);
	l0_descriptor[0] = (page_to_phys(pg_lvl_one)) | DESC_TABLE_BIT | DESC_VALID_BIT;
	vm->pg_lvl_one = (unsigned long)pg_lvl_one;
	kunmap(pg);

}

void hyplet_init_ipa(void)
{
	long addr = 0;
	long vmid = 012;
	struct page *pg_lvl_zero;
	int starting_level = 1;
	struct hyplet_vm *vm = hyplet_get_vm();;

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
		printk("%s alloc page NULL\n", __func__);
		return;
	}

	get_page(pg_lvl_zero);
	create_level_zero(vm, pg_lvl_zero, &addr);

	if (starting_level == 0)
		vm->vttbr_el2 = page_to_phys(pg_lvl_zero) | (vmid << 48);
	else
		vm->vttbr_el2 = page_to_phys((struct page *) vm->pg_lvl_one) | (vmid << 48);

	make_vtcr_el2(vm);
}


// D-2142
void make_vtcr_el2(struct hyplet_vm *vm)
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

	vm->vtcr_el2 = (vtcr_el2_t0sz) |
	    (vtcr_el2_sl0 << VTCR_EL2_SL0_BIT_SHIFT) |
	    (vtcr_el2_irgn0 << VTCR_EL2_IRGN0_BIT_SHIFT) |
	    (vtcr_el2_orgn0 << VTCR_EL2_ORGN0_BIT_SHIFT) |
	    (vtcr_el2_sh0 << VTCR_EL2_SH0_BIT_SHIFT) |
	    (vtcr_el2_tg0 << VTCR_EL2_TG0_BIT_SHIFT) |
	    (vtcr_el2_ps << VTCR_EL2_PS_BIT_SHIFT);

}

/*
 * the page us using attr_ind 4
 */
void make_mair_el2(struct hyplet_vm *vm)
{
	unsigned long mair_el2;

	mair_el2 = hyplet_call_hyp(read_mair_el2);
	vm->mair_el2 = (mair_el2 & 0x000000FF00000000L ) | 0x000000FF00000000L; //
	//tvm->mair_el2 = 0xFFFFFFFFFFFFFFFFL;
 	hyplet_call_hyp(set_mair_el2, vm->mair_el2);
}

