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
#include <linux/hyplet.h>


DEFINE_PER_CPU(struct hyplet_vm, TVM);
static struct proc_dir_entry *procfs = NULL;

struct hyplet_vm* hyplet_get_vm(void)
{
	return this_cpu_ptr(&TVM);
}

long hyplet_get_mfr(void)
{
	long e = 0;
	asm("mrs %0,id_aa64mmfr0_el1\n":"=r"(e));
	return e;
}

unsigned long hyplet_get_ttbr0_el1(void)
{
      unsigned long ttbr0_el1;
      asm("mrs %0,ttbr0_el1\n":"=r" (ttbr0_el1));
      return ttbr0_el1;
}

void make_hcr_el2(struct hyplet_vm *tvm)
{
	tvm->hcr_el2 =  HCR_IMO | HCR_RW;
}

void make_mdcr_el2(struct hyplet_vm *tvm)
{
	tvm->mdcr_el2 = 0x00;
}

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

	for_each_online_cpu(cpu) {
		struct hyplet_vm *tv = &per_cpu(TVM, cpu);
		len += sprintf(page + len, "cpu%d cnt=%ld init %ld irq=%ld\n", 
				   cpu,
				   tv->int_cnt,
				   tv->initialized,
			       	   tv->gic_irq);
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
	    proc_create_data("hyplet_stats", O_RDWR, NULL, &proc_ops, NULL);
}

/*
 * construct page table
*/
int hyplet_init(void)
{
	struct hyplet_vm *_tvm;
	int cpu = 0;

	_tvm = hyplet_get_vm();
	memset(_tvm, 0x00, sizeof(*_tvm));
	make_hcr_el2(_tvm);

	for_each_possible_cpu(cpu) {
		struct hyplet_vm *tv = &per_cpu(TVM, cpu);
		if (tv != _tvm) {
			memcpy(tv, _tvm, sizeof(*_tvm));
		}
		INIT_LIST_HEAD(&tv->hyp_addr_lst);
	}
	hyplet_info("sizeof hyplet %zd\n",sizeof(struct hyplet_ctrl));
	init_procfs();
	return 0;
}

void hyplet_map_tvm(void)
{
	int err;
	struct hyplet_vm *tv = this_cpu_ptr(&TVM);

	if (tv->initialized)
		return;
	err = create_hyp_mappings(tv, tv + 1);
	if (err) {
		hyplet_err("Failed to map hyplet vm");
	} else {
		hyplet_info("Mapped hyplet vm");
	}
	tv->initialized = 1;
	mb();

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
	struct hyplet_vm *tvm = hyplet_get_vm();

	buf =  (char *)(&tvm->print_buf[0]);

	va_start(args, fmt);
	printed = el2_vsprintf(buf, fmt, args);
	va_end(args);
}

void hyplet_prepare_vm(void *x)
{
	struct hyplet_vm *tvm = this_cpu_ptr(&TVM);
	unsigned long vbar_el2 = (unsigned long)KERN_TO_HYP(__hyplet_vectors);
	unsigned long vbar_el2_current;

	hyplet_map_tvm();

	vbar_el2_current = hyplet_get_vectors();
	if (vbar_el2 != vbar_el2_current) {
		hyplet_info("vbar_el2 should restore\n");
		hyplet_set_vectors(vbar_el2);
	}
	tvm->gic_irq = 0;
	hyplet_call_hyp(hyplet_run_vm, tvm, NULL);
}

int is_hyplet_on(void)
{
	struct hyplet_vm *tvm = this_cpu_ptr(&TVM);
	return (tvm->gic_irq != 0);
}

