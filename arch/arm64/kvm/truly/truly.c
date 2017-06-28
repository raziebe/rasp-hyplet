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


DEFINE_PER_CPU(struct truly_vm, TVM);


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

/*
 * construct page table
*/
int truly_init(void)
{
	int err;
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
	make_hcr_el2(_tvm);
	make_mdcr_el2(_tvm);

// map start kernel address +100MB
	err = create_hyp_mappings(PAGE_OFFSET,PAGE_OFFSET + ( 0x1000000 ) );

	for_each_possible_cpu(cpu) {
		struct truly_vm *tv = &per_cpu(TVM, cpu);
		if (tv != _tvm) {
			memcpy(tv, _tvm, sizeof(*_tvm));
		}
		INIT_LIST_HEAD(&tv->hyp_addr_lst);
	}


	tp_info("HYP_PAGE_OFFSET_SHIFT=%x "
			"HYP_PAGE_OFFSET_MASK=%lx "
			"HYP_PAGE_OFFSET=%lx "
			"PAGE_MASK=%lx"
			"PAGE_OFFSET=%lx \n",
			(long) HYP_PAGE_OFFSET_SHIFT,
			(long) HYP_PAGE_OFFSET_MASK,
			(long) HYP_PAGE_OFFSET,
			PAGE_MASK,
			PAGE_OFFSET);

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

void tp_run_vm(void *x)
{
	struct truly_vm t;
	struct truly_vm *tvm = this_cpu_ptr(&TVM);
	unsigned long vbar_el2;
	unsigned long vbar_el2_current =
	    (unsigned long) (KERN_TO_HYP(__truly_vectors));

	truly_map_tvm(NULL);
	vbar_el2 = truly_get_vectors();
	if (vbar_el2 != vbar_el2_current) {
		tp_info("vbar_el2 should restore\n");
		truly_set_vectors(vbar_el2);
	}
	t = *tvm;
	tp_call_hyp(truly_run_vm, tvm);
	*tvm = t;
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
