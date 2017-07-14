#include <linux/module.h>
#include <linux/truly.h>
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

DEFINE_PER_CPU(struct truly_vm, TVM);
struct truly_vm *ttvm; // debug

struct truly_vm *get_tvm(void)
{
	return &TVM;
}

long truly_get_elr_el1(void)
{
	long e;

      asm("mrs  %0, elr_el1\n":"=r"(e));
	return e;
}

void truly_set_sp_el1(long e)
{
      asm("msr  sp_el1,%0\n":"=r"(e));
}

void truly_set_elr_el1(long e)
{
      asm("msr  elr_el1,%0\n":"=r"(e));
}

long truly_get_mfr(void)
{
	long e = 0;
      asm("mrs %0,id_aa64mmfr0_el1\n":"=r"(e));
	return e;
}

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

		//tp_info("L2 IPA %lx\n", l2_descriptor[i]);
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
		printk("%s desc NULL\n", __func__);
		return;
	}

	pg_lvl_two = alloc_pages(GFP_KERNEL | __GFP_ZERO, 3);
	if (pg_lvl_two == NULL) {
		printk("%s alloc page NULL\n", __func__);
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

void create_level_zero(struct truly_vm *tvm, struct page *pg, long *addr)
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

	l0_descriptor[0] =
	    (page_to_phys(pg_lvl_one)) | DESC_TABLE_BIT | DESC_VALID_BIT;

	tvm->pg_lvl_one = (void *) pg_lvl_one;

	tp_info("L0 IPA %lx\n", l0_descriptor[0]);

	kunmap(pg);

}

unsigned long tp_create_pg_tbl(void *cxt)
{
	struct truly_vm *tvm = (struct truly_vm *) cxt;
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
		printk("%s alloc page NULL\n", __func__);
		return 0x00;
	}

	get_page(pg_lvl_zero);
	create_level_zero(tvm, pg_lvl_zero, &addr);

	if (starting_level == 0)
		tvm->vttbr_el2 = page_to_phys(pg_lvl_zero) | (vmid << 48);
	else
		tvm->vttbr_el2 =
		    page_to_phys((struct page *) tvm->pg_lvl_one) | (vmid
								     <<
								     48);

	return tvm->vttbr_el2;
}

// D-2142
void make_vtcr_el2(struct truly_vm *tvm)
{
	long vtcr_el2_t0sz;
	long vtcr_el2_sl0;
	long vtcr_el2_irgn0;
	long vtcr_el2_orgn0;
	long vtcr_el2_sh0;
	long vtcr_el2_tg0;
	long vtcr_el2_ps;

	vtcr_el2_t0sz = truly_get_tcr_el1() & 0b111111;
	vtcr_el2_sl0 = 0b01;	//IMPORTANT start at level 1.  D.2143 + D4.1746
	vtcr_el2_irgn0 = 0b1;
	vtcr_el2_orgn0 = 0b1;
	vtcr_el2_sh0 = 0b11;	// inner sharable
	vtcr_el2_tg0 = (truly_get_tcr_el1() & 0xc000) >> 14;
	vtcr_el2_ps = (truly_get_tcr_el1() & 0x700000000) >> 32;

	tvm->vtcr_el2 = (vtcr_el2_t0sz) |
	    (vtcr_el2_sl0 << VTCR_EL2_SL0_BIT_SHIFT) |
	    (vtcr_el2_irgn0 << VTCR_EL2_IRGN0_BIT_SHIFT) |
	    (vtcr_el2_orgn0 << VTCR_EL2_ORGN0_BIT_SHIFT) |
	    (vtcr_el2_sh0 << VTCR_EL2_SH0_BIT_SHIFT) |
	    (vtcr_el2_tg0 << VTCR_EL2_TG0_BIT_SHIFT) |
	    (vtcr_el2_ps << VTCR_EL2_PS_BIT_SHIFT);

}

static struct proc_dir_entry *procfs = NULL;

static ssize_t proc_write(struct file *file, const char __user * buffer,
			  size_t count, loff_t * dummy)
{

	return count;
}

static int proc_open(struct inode *inode, struct file *filp)
{
	filp->private_data = (void *) 0x1;
	return 0;
}

static ssize_t proc_read(struct file *filp, char __user * page,
			 size_t size, loff_t * off)
{
	ssize_t len = 0;
	int cpu;

	if (filp->private_data == 0x00)
		return 0;

	for_each_possible_cpu(cpu) {
		struct truly_vm *tv = &per_cpu(TVM, cpu);
		len += sprintf(page + len, "cpu %d initialized %ld\n", cpu,
			       	   tv->initialized);
	}

	filp->private_data = 0x00;
	return len;
}



static struct file_operations proc_ops = {
	.open = proc_open,
	.read = proc_read,
	.write = proc_write,
};


static void init_procfs(void)
{
	procfs =
	    proc_create_data("truly_stats", O_RDWR, NULL, &proc_ops, NULL);
}


/*
 * construct page table
*/
int truly_init(void)
{
	long long tcr_el1;
	int t0sz;
	int t1sz;
	int ips;
	int pa_range;
	long id_aa64mmfr0_el1;
	struct truly_vm *_tvm;
	int cpu = 0;

	id_aa64mmfr0_el1 = truly_get_mfr();
	tcr_el1 = truly_get_tcr_el1();

	t0sz = tcr_el1 & 0b111111;
	t1sz = (tcr_el1 >> 16) & 0b111111;
	ips = (tcr_el1 >> 32) & 0b111;
	pa_range = id_aa64mmfr0_el1 & 0b1111;

	_tvm = this_cpu_ptr(&TVM);
	memset(_tvm, 0x00, sizeof(*_tvm));
    tp_create_pg_tbl(_tvm);
	make_vtcr_el2(_tvm);
	_tvm->hstr_el2 = 0;

	/* B4-1583 */
	_tvm->hcr_el2 = (HCR_RW | HCR_VM);
	_tvm->mdcr_el2 = 0x00;

	for_each_possible_cpu(cpu) {
		struct truly_vm *tv = &per_cpu(TVM, cpu);
		if (tv != _tvm) {
			memcpy(tv, _tvm, sizeof(*_tvm));
		}
	}
	init_procfs();
	return 0;
}

void truly_map_tvm(void)
{
	int err;
	struct truly_vm *tv = this_cpu_ptr(&TVM);

	if (tv->initialized)
		return;
	err = create_hyp_mappings(tv, tv + 1);
	if (err) {
		tp_err("Failed to map tvm");
	} else {
		tp_info("Mapped tvm");
	}
	tv->initialized = 1;
	mb();

}

int __hyp_text  matsov_encrypt(struct truly_vm *tv)
{
	int i;
	int bytes;
	int c = 0;
	char *start;
	struct truly_vm *tvm = (struct truly_vm *)KERN_TO_HYP(tv);

	bytes  = tvm->protect.size;
	start = (char *)KERN_TO_HYP(tvm->protect.addr);

	for (i = 0; i < (bytes - 1) ; i++ ) {
		c += start[i] ^ start[i+1];
	}

	return c;
}

int __hyp_text  matsov_decrypt(struct truly_vm *tv)
{
	int i;
	int bytes;
	int c = 0;
	char *start;
	struct truly_vm *tvm = (struct truly_vm *)KERN_TO_HYP(tv);

	bytes  = tvm->protect.size;
	start = (char *)KERN_TO_HYP(tvm->protect.addr);


	for (i = 0; i < (bytes - 1) ; i++ ) {
		c += start[i] ^ start[i+1];
	}

	return c;
}

static blk_qc_t (*org_make_request)(struct request_queue *,struct bio*) = NULL;
//
// bio_copy_data
//
void bio_map_data_to_hyp(struct truly_vm* tvm, struct bio *bi)
{
	int rw;
	struct bvec_iter src_iter;
	struct bio_vec src_bv;
	void *src_p;
	unsigned bytes;
	int err;

	src_iter = bi->bi_iter;

	rw = bio_data_dir(bi);

	while (1) {
		if (!src_iter.bi_size) {
			bi = bi->bi_next;
			if (!bi)
				break;
			src_iter = bi->bi_iter;
		}

		src_bv = bio_iter_iovec(bi, src_iter);
		bytes = src_bv.bv_len;
		src_p = kmap_atomic(src_bv.bv_page);

		err = create_hyp_mappings(src_p + src_bv.bv_offset,
				src_p + src_bv.bv_offset + bytes);

		kunmap_atomic(src_p);
		if (err) {
			tp_err("Failed to map a bio page");
			return;
		}

		tvm->protect.addr = (unsigned long) (src_p + src_bv.bv_offset);
		tvm->protect.size = bytes;
		if (rw == READ)
			tp_call_hyp( matsov_decrypt ,tvm);
		else
			tp_call_hyp( matsov_encrypt ,tvm);

		bio_advance_iter(bi, &src_iter, bytes);
	}
}

blk_qc_t truly_make_request_fn(struct request_queue *q,struct bio* bi)
{
	bio_map_data_to_hyp(this_cpu_ptr(&TVM), bi);
	return org_make_request(q, bi);
}

int truly_add_hook(struct gendisk *disk)
{
	struct request_queue *q;

	if (strcmp(disk->disk_name,"vda"))
			return -1;

	tp_info("hook disk %s\n",disk->disk_name);
	q = disk->queue;
	if (q == NULL){
		tp_err("failed to find a queue");
		return -1;
	}
	org_make_request = q->make_request_fn;
	q->make_request_fn = truly_make_request_fn;
	return 0;
}


void tp_run_vm(void *x)
{
	struct truly_vm *tvm = this_cpu_ptr(&TVM);
	unsigned long vbar_el2 = (unsigned long)KERN_TO_HYP(__truly_vectors);
	unsigned long vbar_el2_current;

	truly_map_tvm(); // debug
	vbar_el2_current = truly_get_vectors();
	if (vbar_el2 != vbar_el2_current) {
		tp_info("vbar_el2 should restore\n");
		truly_set_vectors(vbar_el2);
	}
	ttvm = tvm;
	tp_call_hyp(truly_run_vm, tvm);
}
