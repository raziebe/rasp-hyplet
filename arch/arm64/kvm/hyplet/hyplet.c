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
//#include <linux/slab.h>
//#include <asm/page.h>
//#include <linux/vmalloc.h>
//#include <asm/fixmap.h>
//#include <asm/memory.h>
#include <linux/delay.h>

#include <linux/hyplet.h>
#include <linux/hyplet_user.h>

DEFINE_PER_CPU(struct hyplet_vm, HYPLETS);
static struct proc_dir_entry *procfs = NULL;


struct hyplet_vm* hyplet_get(int cpu)
{
	struct hyplet_vm* hyp = &per_cpu(HYPLETS, 0);
	if (hyp->state & USER_SMP)
		return hyp;
	return &per_cpu(HYPLETS, cpu);
}

struct hyplet_vm* hyplet_get_vm(void)
{
	struct hyplet_vm* hyp = &per_cpu(HYPLETS, 0);

	if (hyp->state & USER_SMP)
		return hyp;
	return this_cpu_ptr(&HYPLETS);
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
		struct hyplet_vm *tv = &per_cpu(HYPLETS, cpu);
		len += sprintf(page + len, "cpu%d cnt=%d"
				" irq=%d dbg=%d\n", 
				   cpu,
				   tv->int_cnt,
			       	   tv->gic_irq, tv->dbg);
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
	struct hyplet_vm *tv;
	int cpu = 0;

	tv = hyplet_get_vm();
	memset(tv, 0x00, sizeof(*tv));

	for_each_possible_cpu(cpu) {
		struct hyplet_vm *t = &per_cpu(HYPLETS, cpu);
		if (tv != t) {
			memcpy(t, tv, sizeof(*t));
		}
		INIT_LIST_HEAD(&t->hyp_addr_lst);
	}

	init_procfs();
	return 0;
}

void hyplet_map_tvm(void)
{
	int err;
	struct hyplet_vm *hyp = hyplet_get_vm();

	if (hyp->initialized)
		return;
	err = create_hyp_mappings(hyp,hyp + 1);
	if (err) {
		hyplet_err("Failed to map hyplet state");
		return;
	} else {
		hyplet_info("Mapped hyplet state");
	}
	hyp->initialized = 1;
	mb();

}

int __hyp_text is_hyp(void)
{
        u64 el;
        asm("mrs %0,CurrentEL" : "=r" (el));
        return el == CurrentEL_EL2;
}

void hyplet_setup(void)
{
	struct hyplet_vm *tv = hyplet_get_vm();
	unsigned long vbar_el2 = (unsigned long)KERN_TO_HYP(__hyplet_vectors);
	unsigned long vbar_el2_current;

	hyplet_map_tvm();
	
	vbar_el2_current = hyplet_get_vectors();
	if (vbar_el2 != vbar_el2_current) {
		hyplet_info("vbar_el2 should restore\n");
		hyplet_set_vectors(vbar_el2);
	}
	tv->gic_irq = 0;
	hyplet_call_hyp(hyplet_on, tv, NULL);
}

int is_hyplet_on(void)
{
	struct hyplet_vm *tv = hyplet_get_vm();
	return (tv->gic_irq != 0);
}

void hyplet_reset_smp(void* ret)
{
	int cpu;

	struct hyplet_vm *hyp;
	cpu = raw_smp_processor_id();
	hyp = hyplet_get(cpu);
	hyplet_call_hyp(hyplet_set_cxt, hyp, hyp->ttbr0_el2);
}

void close_hyplet(struct hyplet_vm *hyp)
{
	hyp->tsk = NULL;
	hyp->irq_to_trap = 0;
	barrier();
	hyp->gic_irq = 0;
	hyp->hyplet_id = 0;
	hyp->user_hyplet_code = 0;
	barrier();
	msleep(10);
	hyplet_free_mem(hyp);
	hyp->state  = 0;
}

void hyplet_reset(struct task_struct *tsk)
{
	int cpu;
	struct hyplet_vm *hyp = hyplet_get_vm();

	if (hyp->state & USER_SMP) {
		if (hyp->tsk != NULL && hyp->tsk->mm == tsk->mm)
			goto hyplet_exit_smp;
	}

	for_each_online_cpu(cpu) {
		hyp =  hyplet_get(cpu);
		if (!hyp->tsk)
			continue;
		if (hyp->tsk->mm == tsk->mm)
				close_hyplet(hyp);
	}

	return;

hyplet_exit_smp:
	for_each_online_cpu(cpu) {
		struct hyplet_vm *tmp =  &per_cpu(HYPLETS, cpu);
		tmp->hyplet_id  = 0;
		barrier();
		close_hyplet(tmp);
	}
	on_each_cpu(hyplet_reset_smp, NULL, 1);
}

int hyplet_set_func(struct hyplet_ctrl* hplt)
{
	struct hyplet_vm *hyp = hyplet_get_vm();
	/*
	 * check that the function exists
	*/
	if ( hyp->user_hyplet_code == 0 ){
		hyplet_err("User hyplet is not set\n");
		return -EINVAL;
	}

	if ( hyp->user_hyplet_code !=
			hplt->__action.rpc_set_func.func_addr ){

		hyplet_err("User hyplet is incorrect\n");
		return -EINVAL;
	}

	hyp->hyplet_id = hplt->__action.rpc_set_func.func_id;
	return 0;
}

void  hvc_set_smp(void* ret)
{
	struct hyplet_vm *hyp = hyplet_get(0);
	hyplet_info("hvc_set_smp %p\n",hyp);
	hyplet_call_hyp(hyplet_set_cxt, hyp,hyp->ttbr0_el2);
}

/*
 * All processors points to the same hyplet_vm
*/
long hyplet_set_smp(struct hyplet_smp *hplt_smp)
{
	int cpu;
	struct hyplet_vm *hyp;

	hplt_smp->nr_cpus = 0;
	for_each_online_cpu(cpu) {
		hyp = hyplet_get(cpu);
		hyp->state |= USER_SMP;
		hplt_smp->nr_cpus++;
	}
	on_each_cpu(hvc_set_smp, NULL, 1);
	hyplet_info("set smp %d",hplt_smp->nr_cpus);
	return hplt_smp->nr_cpus;
}

int hyplet_ctl(unsigned long arg)
{
	struct hyplet_vm *hyp = hyplet_get_vm();
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
				hyp->hyplet_stack =
					(long)(hplt.__action.addr.addr) +
						hplt.__action.addr.size - PAGE_SIZE;
				break;

		case HYPLET_MAP_HYPLET:
				rc = hyplet_map_user_data(hplt.cmd , (void *)&hplt.__action);
				if ( rc )
					return -EINVAL;
				hyp->user_hyplet_code = hplt.__action.addr.addr;
				break;
		case HYPLET_TRAP_IRQ:
				return hyplet_trap_irq(hplt.__action.irq);

		case HYPLET_UNTRAP_IRQ:
				return hyplet_untrap_irq(hplt.__action.irq);

	   	case HYPLET_DUMP_HWIRQ:
				return hyplet_dump_irqs();

		case HYPLET_IMP_TIMER:
				return hyplet_imp_timer();

		case HYPLET_SET_FUNC:
				return hyplet_set_func(&hplt);

		case HYPLET_SET_SMP:
				hyplet_set_smp(&hplt.__action.smp);
				return copy_to_user((void *)arg,  &hplt, sizeof(hplt));
	}
	return rc;
}


