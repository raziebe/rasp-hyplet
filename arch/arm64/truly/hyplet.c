#include <linux/module.h>
#include <linux/init.h>
#include <linux/gfp.h>
#include <linux/highmem.h>
#include <linux/compiler.h>
#include <linux/linkage.h>
#include <linux/init.h>
#include <asm/sections.h>
#include <linux/proc_fs.h>
#include <linux/delay.h>

#include <linux/hyplet.h>
#include <linux/hyplet_user.h>
#include <linux/tp_mmu.h>

DEFINE_PER_CPU(struct hyplet_vm, HYPLETS);

struct hyplet_vm* hyplet_get(int cpu){
	return &per_cpu(HYPLETS, cpu);
}

static struct hyplet_vm* hyplet_get_vm(void){
	return this_cpu_ptr(&HYPLETS);
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
		struct hyplet_vm *hyp = &per_cpu(HYPLETS, cpu);
		if (tv != hyp) {
			memcpy(hyp, tv, sizeof(*tv));
		}
		INIT_LIST_HEAD(&hyp->hyp_addr_lst);
		hyp->state = HYPLET_OFFLINE_ON;
	}
	return 0;
}

void hyplet_map(void)
{
	int err;
	struct hyplet_vm *hyp = hyplet_get_vm();

	err = create_hyp_mappings(hyp, hyp + 1, PAGE_HYP);
	if (err) {
		hyplet_err("Failed to map hyplet state");
		return;
	} else {
		hyplet_info("Mapped hyplet state");
	}

}

int __hyp_text is_hyp(void)
{
        u64 el;
        asm("mrs %0,CurrentEL" : "=r" (el));
        return el == CurrentEL_EL2;
}

void hyplet_setup(void)
{
	struct hyplet_vm *hyp = hyplet_get_vm();
	unsigned long vbar_el2 = (unsigned long)KERN_TO_HYP(__hyplet_vectors);
	unsigned long vbar_el2_current;

	hyplet_map();
	
	vbar_el2_current = hyplet_get_vectors();
	if (vbar_el2 != vbar_el2_current) {
		hyplet_info("vbar_el2 should restore\n");
		hyplet_set_vectors(vbar_el2);
	}
	hyplet_call_hyp(hyplet_on, hyp);
}

int is_hyplet_on(void)
{
	struct hyplet_vm *hyp = hyplet_get_vm();
	return (hyp->irq_to_trap != 0);
}

void __close_hyplet(void *task, struct hyplet_vm *hyp)
{
	int offlet_mode = 0;
	struct task_struct *tsk;
	tsk =  (struct task_struct *)task;

	if (hyp->tsk->mm != tsk->mm)
		return;

	if (!(hyp->state & HYPLET_OFFLINE_ON))
		hyplet_call_hyp(hyplet_trap_off);

	hyp->tsk = NULL;
	smp_mb();
	while (hyp->state & HYPLET_OFFLINE_RUN) {
		offlet_mode = 1;
		msleep(1);
		hyplet_debug("Waiting for offlet to exit..\n");
	}

	hyp->irq_to_trap = 0;
	hyp->hyplet_id = 0;
	hyp->user_hyplet_code = 0;
	if (!offlet_mode)
		hyplet_free_mem(hyp);
	hyp->state  = HYPLET_OFFLINE_ON;
	smp_mb();
	hyp->elr_el2 = 0;
	hyp->hyplet_stack = 0;
	hyp->user_hyplet_code = 0;
	smp_mb();
	hyp->hyplet_id  = 0;
	hyplet_info("Close hyplet\n");
}

void close_hyplet(void *task)
{
	struct hyplet_vm *hyp = hyplet_get_vm();

	if (!hyp->tsk) {
		return;
	}
	__close_hyplet(task, hyp);
}

/*
 * call on each process shutdown
*/
void hyplet_reset(struct task_struct *tsk)
{
	int i;
	struct hyplet_vm *hyp;

	on_each_cpu(close_hyplet, tsk, 1);

	for (i = 0 ; i < num_possible_cpus(); i++){
		hyp = hyplet_get(i);

		if (hyp->state  & HYPLET_OFFLINE_RUN) {
				printk("hyplet offlet discoverred on cpu %d %p\n",
					i, hyp->tsk);
			__close_hyplet(tsk, hyp);
		}
	}
}

void hyplet_offlet(unsigned int cpu)
{
	struct hyplet_vm *hyp;

	hyp = hyplet_get_vm();
	printk("offlet : Enter %d\n",cpu);

	while (hyp->state & HYPLET_OFFLINE_ON) {
		/*
		 * Wait for an assignment.
		 */
		while (hyp->tsk == NULL
				|| hyp->user_hyplet_code == 0x00){
			cpu_relax();
		}

		hyp->state |= HYPLET_OFFLINE_RUN;

		printk("hyplet offlet: Start run\n");
		while (hyp->tsk != NULL) {
			hyplet_call_hyp(hyplet_run_user);
			cpu_relax();
		}
		hyplet_free_mem(hyp);
		hyp->state &= ~(HYPLET_OFFLINE_RUN);
		smp_mb();
		printk("hyplet offlet : Ended\n");
	}
	printk("offlet : Exit %d\n",cpu);
}

int hyplet_set_rpc(struct hyplet_ctrl* hplt,struct hyplet_vm *hyp)
{
	/*
	 * check that the function exists
	*/
	if ( hyp->user_hyplet_code !=
			hplt->__action.rpc_set_func.func_addr ){
		hyplet_err("User hyplet is incorrect\n");
		return -EINVAL;
	}

	hyp->hyplet_id = hplt->__action.rpc_set_func.func_id;
	hyp->tsk = current;
	hyplet_call_hyp(hyplet_trap_on);
	return 0;
}

int offlet_assign(int cpu,struct hyplet_ctrl* target_hplt,struct hyplet_vm *src_hyp)
{
	struct hyplet_vm *hyp = hyplet_get(cpu);

	if (hyp == NULL){
		hyplet_err("Failed to assign hyplet in %d\n",cpu);
		return -1;
	}

	printk("offlet %p %p\n",target_hplt, src_hyp);
	hyp->state  = src_hyp->state;
	smp_mb();
	hyp->tsk = current;
	hyp->user_hyplet_code = target_hplt->__action.addr.addr;
	hyp->hyplet_stack = src_hyp->hyplet_stack;
	smp_mb();
	printk("offlet: hyplet assigned to cpu %d\n",cpu);
	return 0;
}

int hyplet_ctl(unsigned long arg)
{
	struct hyplet_vm *hyp = hyplet_get_vm();
	struct hyplet_ctrl hplt;
	int rc = -1;

	if (hyp->tsk
			&& hyp->tsk->mm != current->mm) {
		hyplet_err(" hyplet busy\n");
		return -EBUSY;
	}

	if ( copy_from_user(&hplt, (void *) arg, sizeof(hplt)) ){
		hyplet_err(" failed to copy from user");
		return -1;
	}

	if (hplt.__resource.cpu >= 0)
			hyp = hyplet_get(hplt.__resource.cpu);

	printk("offlet: assigning to cpu  %d \n",hplt.__resource.cpu);

	switch (hplt.cmd)
	{
		case HYPLET_MAP_ALL:
				return hyplet_map_user(hyp);

		case HYPLET_MAP_STACK: // If the user won't map the stack we use the current sp_el0
				rc = hyplet_check_mapped(hyp, (void *)&hplt.__action);
				if ( rc < 0){
					return -EINVAL;
				}
				hyp->hyplet_stack =
					(long)(hplt.__action.addr.addr) +
						hplt.__action.addr.size - PAGE_SIZE;
				break;

		case HYPLET_SET_CALLBACK:
				rc = hyplet_check_mapped(hyp, (void *)&hplt.__action);
				if (rc == 0) {
					hyplet_info("Warning: Hyplet was not mapped\n");
				}
				hyp->user_hyplet_code = hplt.__action.addr.addr;
				break;

		case OFFLET_SET_CALLBACK: // must be called in the processor in which the stack was set.
				rc = hyplet_check_mapped(hyp, (void *)&hplt.__action);
				if (rc == 0) {
					hyplet_info("Warning: Hyplet was not mapped\n");
				}
				return offlet_assign(hplt.__resource.cpu, &hplt, hyp);

		case HYPLET_TRAP_IRQ:
				return hyplet_trap_irq(hyp,hplt.__resource.irq);

		case HYPLET_UNTRAP_IRQ:
				return hyplet_untrap_irq(hyp, hplt.__resource.irq);

		case HYPLET_IMP_TIMER:
				return hyplet_imp_timer(hyp);

		case HYPLET_SET_RPC:
				return hyplet_set_rpc(&hplt, hyp);

	}
	return rc;
}
