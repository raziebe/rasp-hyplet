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

#include <asm/page.h>
#include <linux/irqchip.h>
#include <linux/irqchip/arm-gic-common.h>
#include <linux/irqchip/arm-gic-v3.h>
#include <asm/arch_gicv3.h>
#include <linux/hyplet.h>
#include <linux/irq.h>
#include <linux/irqdesc.h>

DEFINE_PER_CPU(struct hyplet_vm, TVM);
static struct proc_dir_entry *procfs = NULL;

struct hyplet_vm* hyplet_vm(void)
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
		struct hyplet_vm *tv = &per_cpu(TVM, cpu);
		len += sprintf(page + len, "cpu %d initialized %ld\n", cpu,
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
	long long tcr_el1;
	int t0sz;
	int t1sz;
	int ips;
	int pa_range;
	long id_aa64mmfr0_el1;
	struct hyplet_vm *_tvm;
	int cpu = 0;

	if  ( hyplet_get_vgic_ver() == 2){
		hyplet_info("Hyplet ARM are applicable only with GiCv3");
		return -1;
	}

	id_aa64mmfr0_el1 = hyplet_get_mfr();
	tcr_el1 = hyplet_get_tcr_el1();

	t0sz = tcr_el1 & 0b111111;
	t1sz = (tcr_el1 >> 16) & 0b111111;
	ips = (tcr_el1 >> 32) & 0b111;
	pa_range = id_aa64mmfr0_el1 & 0b1111;

	_tvm = this_cpu_ptr(&TVM);
	memset(_tvm, 0x00, sizeof(*_tvm));
    	hyplet_create_pg_tbl(_tvm);
	make_vtcr_el2(_tvm);
	_tvm->hstr_el2 = 0;

	INIT_LIST_HEAD(&  _tvm->hyp_addr_lst );

	/* B4-1583 */
	_tvm->hcr_el2 =  HYPLET_HCR_GUEST_FLAGS;
	_tvm->mdcr_el2 = 0x00;
	_tvm->ich_hcr_el2 = 0;

	for_each_possible_cpu(cpu) {
		struct hyplet_vm *tv = &per_cpu(TVM, cpu);
		if (tv != _tvm) {
			memcpy(tv, _tvm, sizeof(*_tvm));
		}
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

u64 hyp_gic_read_iar(void)
{
	struct hyplet_vm *tvm = this_cpu_ptr(&TVM);
	unsigned long long gic_irq = tvm->gic_irq;

	tvm->gic_irq = 0;
	return gic_irq;
}

// call after gic_handle_irq
void hyplet_imo(void)
{
	struct hyplet_vm *tvm = this_cpu_ptr(&TVM);
	if (tvm->initialized){
		hyplet_call_hyp(hyplet_enable_imo, NULL, NULL);
	}
}


/*
  * 1. A user maps a stack and execution code
  *      of a an available thread.
  * 2. hyplet_ctl responsibilities
  *   Map function
  *   Map stack
  *   hyplet start
  *   	Set the trapped irq
  *   	Cache hyp_uthread task_struct
  *   	Cache ttbr0_el1
  *
  * Please see hyplet_user.c for example
*/
int hyplet_ctl(unsigned long arg)
{
	struct hyplet_vm *hypletvm = this_cpu_ptr(&TVM);
	struct hyplet_ctrl hplt;
	int rc = -1;

	if ( copy_from_user(&hplt, (void *) arg, sizeof(hplt)) ){
		hyplet_err(" failed to copy from user");
		return -1;
	}

	switch (hplt.cmd)
	{
		case HYPLET_MAP_ANY:
				return hyplet_map_user_data(hplt.cmd , (void *)&hplt.__action);

		case HYPLET_MAP_STACK:
				rc = hyplet_map_user_data(hplt.cmd , (void *)&hplt.__action);
				if ( rc )
						return -EINVAL;
				hypletvm->hyplet_stack = (long)(hplt.__action.addr.addr) + hplt.__action.addr.size - PAGE_SIZE;
				break;

		case HYPLET_MAP_CODE:
				rc = hyplet_map_user_data(hplt.cmd , (void *)&hplt.__action);
				if ( rc )
					return -EINVAL;
				hypletvm->hyplet_code = hplt.__action.addr.addr;
				break;
		case HYPLET_TRAP_IRQ:
				// user provides the irq, we must find hw_irq
				return hyplet_trap_irq(hplt.__action.irq);

		case HYPLET_UNTRAP_IRQ:
				return hyplet_untrap_irq(hplt.__action.irq);
	}
	return rc;
}

asmlinkage long sys_hyplet(long d)
{
	return hyplet_ctl (d);
}

int hyplet_trap_irq(int irq)
{
	struct irq_desc *desc;
	struct hyplet_vm *tv = this_cpu_ptr(&TVM);

	desc  = irq_to_desc(irq);
	if (!desc) {
		hyplet_err("Incorrect irq %d\n",irq);
		return -EINVAL;
	}
	// save context
	tv->ttbr0_el1 = hyplet_get_ttbr0_el1();
	tv->task_struct = current;
	if (!(tv->state & (USER_CODE_MAPPED | USER_STACK_MAPPED))){
		return -EINVAL;
	}
	tv->state |= RUN_HYPLET;
	tv->irq_to_trap = desc->irq_data.hwirq;
	mb();
	return 0;
}

int hyplet_untrap_irq(int irq)
{
	struct irq_desc *desc;
	struct hyplet_vm *tv = this_cpu_ptr(&TVM);

	desc  = irq_to_desc(irq);
	if (!desc) {
		hyplet_err("Incorrect irq %d\n",irq);
		return -EINVAL;
	}

	if (desc->irq_data.hwirq != tv->irq_to_trap){
		hyplet_err("Incorrect hwirq %ld because"
					" local irq is %ld\n"
				,tv->irq_to_trap,
				desc->irq_data.hwirq);
		return -EINVAL;
	}

	tv->irq_to_trap = 0;
	mb();
	return 0;
}
