#include <linux/module.h>
#include <linux/truly.h>
#include <linux/linkage.h>
#include <linux/init.h>
#include <linux/gfp.h>
#include <linux/highmem.h>
#include <linux/compiler.h>
#include <linux/init.h>
#include <asm/sections.h>
#include <linux/proc_fs.h>
#include <linux/slab.h>
#include <asm/page.h>
#include <linux/vmalloc.h>
#include <asm/fixmap.h>
#include <asm/memory.h>
#include <linux/blkdev.h>
#include <asm/pgtable.h>

DEFINE_PER_CPU(struct truly_vm, TVM);
struct truly_vm *ttvm; // debug
static struct request_queue *org_queue = NULL;

long truly_get_mfr(void)
{
	long e = 0;
    asm("mrs %0,id_aa64mmfr0_el1\n":"=r"(e));
	return e;
}

#define USB_ADDR 0x3f980000
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
 		 *  .leave stage 1 un-changed see 1795 
		 */
		l3_descriptor[i] = (DESC_AF) |
			(0b11 << DESC_SHREABILITY_SHIFT) |
			(0b11 << DESC_S2AP_SHIFT) | (0b1111 << 2) |
		   	 DESC_TABLE_BIT | DESC_VALID_BIT | (*addr);
#if 0		
                if ( (*addr) == USB_ADDR) {
                        long z = page_to_phys(virt_to_page(empty_zero_page));

                        tp_info("Hide %lx zpg=%lx\n", *addr ,z);
                        l3_descriptor[i] = (DESC_AF) | (0b11 << DESC_SHREABILITY_SHIFT) |
                                (0b11 << DESC_S2AP_SHIFT) | (0b1111 << 2) |
                                 DESC_TABLE_BIT | DESC_VALID_BIT | (long)z;
		}
#endif		
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

	for (i = 0; i < PAGE_SIZE / (sizeof(long)); i++) {
		// fill an entire 2MB of mappings
		pg_lvl_three = alloc_page(GFP_KERNEL | __GFP_ZERO);
		create_level_three(pg_lvl_three , addr);
		// calc the entry of this table
		l2_descriptor[i] =
		    (page_to_phys(pg_lvl_three)) | DESC_TABLE_BIT |
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

	for (i = 0; i < 2; i++) {
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
		    page_to_phys((struct page *) tvm->pg_lvl_one) | (vmid << 48);

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
	_tvm->hcr_el2 =  TP_HCR_GUEST_FLAGS;
	_tvm->mdcr_el2 = 0x00;

	for_each_possible_cpu(cpu) {
		struct truly_vm *tv = &per_cpu(TVM, cpu);
		if (tv != _tvm) {
			memcpy(tv, _tvm, sizeof(*_tvm));
		}
	}
	init_procfs();
	tp_info("Truly Registers intialized\n");
	return 0;
}

void truly_map_tvm(void)
{
	int err;
	struct truly_vm *tv = this_cpu_ptr(&TVM);

	err = create_hyp_mappings(tv, tv + 1);
	if (err) {
		tp_err("Failed to map tvm");
	} else {
		tp_info("Mapped tvm");
	}

}

int __hyp_text  matsov_encrypt(struct truly_vm *tv)
{
	int i;
	int bytes;
	uint8_t *start;
	struct truly_vm *tvm = (struct truly_vm *)KERN_TO_HYP(tv);

	bytes  = tvm->protect.size;
	start = (uint8_t *)KERN_TO_HYP(tvm->protect.addr);

	for (i = 0; i < bytes ; i++ ) {
		start[i] = 'y';
	}

	return bytes;
}

int __hyp_text  matsov_decrypt(struct truly_vm *tv)
{
	int i;
	unsigned long long bytes;
	uint8_t *start;
	struct truly_vm *tvm = (struct truly_vm *)KERN_TO_HYP(tv);

	bytes  = tvm->protect.size;
	start = (uint8_t *)KERN_TO_HYP(tvm->protect.addr);

//	el2_sprintf("EL2: %s bytes=%lld  start=%p\n", __func__ ,bytes,start);

	for (i = 0; i < bytes ; i++ ) {
		start[i] = 'x';
		//start[i] = (start[i] - 1);
	}

	return bytes;
}

int __hyp_text el2_vsprintf(char *buf, const char *fmt, va_list args)
{
	return vsnprintf(buf, INT_MAX, fmt, args);
}

int __hyp_text is_hyp(void)
{
        u64 el;
        asm("mrs %0,CurrentEL" : "=r" (el));
        return el == CurrentEL_EL2;
}


void __hyp_text el2_memcpy(char *dst,const char *src,int bytes)
{
	int i = 0;
	for (;i < bytes ; i++)
		dst[i] = src[i];
}
/*
 * Executed in EL2
 */
void __hyp_text el2_sprintf(const char *fmt, ...)
{
	va_list args;
	char *buf;
	int printed;
	struct truly_vm *tvm = el2_get_tvm();

	buf =  (char *)(&tvm->print_buf[0]);

	va_start(args, fmt);
	printed = el2_vsprintf(buf, fmt, args);
	va_end(args);
}

static void tp_bio_end_io(struct bio* tpbio)
{
	int i = 0;
	struct bio* bi;
	struct bio_vec *bvec;
	sector_t sector;
	
	sector = tpbio->bi_iter.bi_sector;

	bio_for_each_segment_all(bvec, tpbio, i) {
		unsigned int len = bvec->bv_len;
		char *s = kmap_atomic(bvec->bv_page);
		memset(s + bvec->bv_offset,'1', bvec->bv_len);
		printk("%d R sec %zd \n",i, sector);
		sector += len >> 9;
		kunmap_atomic(s);
	}
	bi = tpbio->bi_private;
//	bio_put(tpbio);
	if (bi && bi->bi_end_io)
		bi->bi_end_io(bi);
}

blk_qc_t truly_make_request_fn(struct request_queue *q,struct bio* bio)
{

	int i = 0;
	struct bio_vec *bvec;

	if (bio_data_dir(bio) == READ) {
		struct bio *tp_bio = bio_clone(bio, GFP_NOIO);

		tp_bio->bi_end_io = tp_bio_end_io;
		tp_bio->bi_private = bio;
		//generic_make_request(tp_bio);
		return // org_queue->make_request_fn(org_queue, tp_bio);
	}

// WRITE PATH
	bio_for_each_segment_all(bvec, bio, i) {
		sector_t sector = bio->bi_iter.bi_sector;
		unsigned int len = bvec->bv_len;

		memset(kmap(bvec->bv_page) + bvec->bv_offset,'1', bvec->bv_len);
		printk("%d W sec %zd \n",i, sector);
		sector += len >> 9;
		kunmap(bvec->bv_page);
	}

	return org_queue->make_request_fn(org_queue, bio);
}

int truly_add_hook(struct gendisk *disk)
{
	if (strcmp(disk->disk_name,"sda")){
		return -1;
	}

	tp_info("set hook disk %s\n",disk->disk_name);

	org_queue = kmalloc(sizeof(struct request_queue)  , GFP_NOIO);
	memcpy(org_queue, disk->queue, sizeof(struct request_queue));
	// replace the callback
	disk->queue->make_request_fn = truly_make_request_fn;

	return 0;
}

#define ul_virt_to_phys(x) (unsigned long)virt_to_phys((void*)x)
#define MLK(b, t) (b), (t), (((t) - (b)) >> 10)
#define PHYS_MLK(b, t) ul_virt_to_phys(b), ul_virt_to_phys(t), ((t) - (b)) >> 10
#define PHYS_MLM(b, t) ul_virt_to_phys(b)>>20, ul_virt_to_phys(t)>>20, ((t) - (b)) >> 20
#define PHYS_MLG(b, t) ul_virt_to_phys(b)>>30, ul_virt_to_phys(t)>>30, ((t) - (b)) >> 30
#define PHYS_MLK_ROUNDUP(b, t) b, t, DIV_ROUND_UP(((t) - (b)), SZ_1K)
int kernel_mapped = 0;

void tp_run_vm(void *x)
{
//	int err;
	struct truly_vm *tvm = this_cpu_ptr(&TVM);
	unsigned long vbar_el2 = (unsigned long)KERN_TO_HYP(__truly_vectors);
	unsigned long vbar_el2_current;

	if (tvm->initialized)
		return;
	tvm->initialized = 1;
	truly_map_tvm();

/*
 *      Need for the constants of printk
	err = create_hyp_mappings( _sdata ,_edata );
	if (err){
		tp_info("Failed to map kernel .data");
		return;
	}

	if (!kernel_mapped){
		err = create_hyp_mappings( (void*)PAGE_OFFSET, high_memory);
		if (err){
			tp_info("Failed to map kernel address");
			return;
		}
		kernel_mapped =1;
	}
*/
	vbar_el2_current = truly_get_vectors();
	if (vbar_el2 != vbar_el2_current) {
		tp_info("vbar_el2 should restore\n");
		truly_set_vectors(vbar_el2);
	}
	ttvm = tvm;
	tvm->ich_hcr_el2 = 1;
	tp_call_hyp(truly_run_vm, tvm, NULL);
	tp_info("Truly ON\n");
}

// call after gic_handle_irq
void tp_trigger_el2(void)
{
	struct truly_vm *tvm = this_cpu_ptr(&TVM);
	if (tvm->initialized){
		tp_call_hyp(route_to_el2, NULL, NULL);
	}
}
