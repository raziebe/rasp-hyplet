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

void make_mdcr_el2(struct truly_vm *tvm)
{
	tvm->mdcr_el2 = 0x00;
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
		len += sprintf(page + len, "cpu %d brk count %ld\n", cpu,
			       tv->brk_count_el2);
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

int create_hyp_user_mappings(void *from, void *to);

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

//	make_vtcr_el2(_tvm);
//	make_hcr_el2(_tvm);
//	make_mdcr_el2(_tvm);

// map start kernel address +100MB


	for_each_possible_cpu(cpu) {
		struct truly_vm *tv = &per_cpu(TVM, cpu);
		if (tv != _tvm) {
			memcpy(tv, _tvm, sizeof(*_tvm));
		}
		INIT_LIST_HEAD(&tv->hyp_addr_lst);
	}
	init_procfs();
	return 0;
}

void truly_map_tvm(void *d)
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

int  truly_function(void)
{
	struct truly_vm *tvm = (struct truly_vm *)KERN_TO_HYP(ttvm);
	tvm->x30 = 101;
	return 111;
}

#define MLK(b, t) b, t, ((t) - (b)) >> 10
#define MLM(b, t) b, t, ((t) - (b)) >> 20
#define MLG(b, t) b, t, ((t) - (b)) >> 30
#define MLK_ROUNDUP(b, t) b, t, DIV_ROUND_UP(((t) - (b)), SZ_1K)

void tp_run_vm(void *x)
{
	int err;
	struct truly_vm *tvm = this_cpu_ptr(&TVM);
	unsigned long vbar_el2 = (unsigned long)KERN_TO_HYP(__truly_vectors);
	unsigned long vbar_el2_current;

	printk("HYP_PAGE_OFFSET_SHIFT=%lx\n"
			"HYP_PAGE_OFFSET_MASK=%lx\n"
			"HYP_PAGE_OFFSET=%lx\n"
			"PAGE_MASK=%lx\n"
			"PAGE_OFFSET=%lx \n",
			(long) HYP_PAGE_OFFSET_SHIFT,
			(long) HYP_PAGE_OFFSET_MASK,
			(long) HYP_PAGE_OFFSET,
			PAGE_MASK,
			PAGE_OFFSET );

	printk(	  "truly maps:\n"
			   "\t  fixed   : 0x%16lx - 0x%16lx   (%6ld KB) won't map\n"
			  "\t  PCI I/O : 0x%16lx - 0x%16lx   (%6ld MB) won't map\n"
			  "\t  modules : 0x%16lx - 0x%16lx   (%6ld MB) won't map\n"
			  "\t  memory  : 0x%16lx - 0x%16lx   (%6ld MB)\n"
			  "\t  .init : 0x%p" " - 0x%p" "   (%6ld KB)\n"
			  "\t  .text : 0x%p" " - 0x%p" "   (%6ld KB)\n"
			  "\t  .data : 0x%p" " - 0x%p" "   (%6ld KB)\n",
			  MLK(FIXADDR_START, FIXADDR_TOP),
			  MLM(PCI_IO_START, PCI_IO_END),
			  MLM(MODULES_VADDR, MODULES_END),
			  MLM(PAGE_OFFSET, (unsigned long)high_memory),
			  MLK_ROUNDUP(__init_begin, __init_end),
			  MLK_ROUNDUP(_text, _etext),
			  MLK_ROUNDUP(_sdata, _edata));


	err = create_hyp_mappings( _text ,_etext );
	if (err){
		tp_info("Failed to map kernel .text");
		return;
	}

	err = create_hyp_mappings( _sdata ,_edata );
	if (err){
		tp_info("Failed to map kernel .data");
		return;
	}

	err = create_hyp_mappings( __init_begin, __init_end);
	if (err){
		tp_info("Failed to map kernel .int section");
		return;
	}

	err = create_hyp_mappings( (void*)PAGE_OFFSET, high_memory);
	if (err){
		tp_info("Failed to map kernel addr");
		return;
	}

	vbar_el2_current = truly_get_vectors();
	if (vbar_el2 != vbar_el2_current) {
		tp_info("vbar_el2 should restore\n");
		truly_set_vectors(vbar_el2);
	}
	ttvm = tvm;
	tp_call_hyp(truly_function);
	tp_info("x30 =%ld\n",tvm->x30);
}

void set_mdcr_el2(void *dummy)
{
	struct truly_vm *tvm = this_cpu_ptr(&TVM);
	tvm->mdcr_el2 = 0x100;
	tp_call_hyp(truly_set_mdcr_el2);
}

void truly_set_trap(void)
{
	on_each_cpu(set_mdcr_el2, NULL, 0);
}

void reset_mdcr_el2(void *dummy)
{
	struct truly_vm *tvm = this_cpu_ptr(&TVM);
	tvm->mdcr_el2 = 0x00;
	tp_call_hyp(truly_set_mdcr_el2);
}

void truly_reset_trap(void)
{
	on_each_cpu(reset_mdcr_el2, NULL, 0);
}


EXPORT_SYMBOL_GPL(truly_get_vectors);
EXPORT_SYMBOL_GPL(truly_get_hcr_el2);
EXPORT_SYMBOL_GPL(tp_call_hyp);
EXPORT_SYMBOL_GPL(truly_init);
