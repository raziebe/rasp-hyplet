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

struct hyplet_vm* hyplet_get(int cpu)
{
	return &per_cpu(HYPLETS, cpu);
}

struct hyplet_vm* hyplet_get_vm(void)
{
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
		struct hyplet_vm *t = &per_cpu(HYPLETS, cpu);
		if (tv != t) {
			memcpy(t, tv, sizeof(*t));
		}
		INIT_LIST_HEAD(&t->hyp_addr_lst);
	}
	return 0;
}

void hyplet_map_tvm(void)
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

	hyplet_map_tvm();
	
	vbar_el2_current = hyplet_get_vectors();
	if (vbar_el2 != vbar_el2_current) {
		hyplet_info("vbar_el2 should restore\n");
		hyplet_set_vectors(vbar_el2);
	}
	hyplet_call_hyp(hyplet_on, hyp);
}

int is_hyplet_on(void)
{
	struct hyplet_vm *tv = hyplet_get_vm();
	return (tv->irq_to_trap != 0);
}

void close_hyplet(void *task)
{
	struct task_struct *tsk;
	struct hyplet_vm *hyp = hyplet_get_vm();

	if (!hyp->tsk) {
		return;
	}

	tsk =  (struct task_struct *)task;
	if (hyp->tsk->mm != tsk->mm)
		return;
	hyplet_call_hyp(hyplet_trap_off);
	hyp->tsk = NULL;
	hyp->irq_to_trap = 0;
	hyp->hyplet_id = 0;
	hyp->user_hyplet_code = 0;
	hyplet_free_mem(hyp);
	hyp->state  = 0;
	hyp->elr_el2 = 0;
	hyp->hyplet_stack = 0;
	hyp->user_hyplet_code = 0;
	hyp->hyplet_id  =0 ;
	hyplet_info("Close hyplet\n");
}

/*
 * call on each process shutdown
 */
void hyplet_reset(struct task_struct *tsk)
{
	on_each_cpu(close_hyplet, tsk, 1);
}

int hyplet_set_rpc(struct hyplet_ctrl* hplt)
{
	struct hyplet_vm *hyp = hyplet_get_vm();
	/*
	 * check that the function exists
	*/
	if ( hyp->user_hyplet_code != hplt->__action.rpc_set_func.func_addr ){

		hyplet_err("User hyplet is incorrect\n");
		return -EINVAL;
	}

	hyp->hyplet_id = hplt->__action.rpc_set_func.func_id;
	hyp->tsk = current;
	hyplet_call_hyp(hyplet_trap_on);
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

	switch (hplt.cmd)
	{
		case HYPLET_MAP_ALL:
				return hyplet_map_user();

		case HYPLET_MAP_STACK: // If the user won't map the stack we use the current sp_el0
				rc = hyplet_check_mapped((void *)&hplt.__action);
				if ( rc < 0){
					return -EINVAL;
				}
				hyp->hyplet_stack =
					(long)(hplt.__action.addr.addr) +
						hplt.__action.addr.size - PAGE_SIZE;
				break;

		case HYPLET_SET_CALLBACK:
				rc = hyplet_check_mapped((void *)&hplt.__action);
				if (rc == 0) {
					hyplet_info("Warning: Hyplet was not mapped\n");
				}
				hyp->user_hyplet_code = hplt.__action.addr.addr;
				break;
		case HYPLET_TRAP_IRQ:
				return hyplet_trap_irq(hplt.__action.irq);

		case HYPLET_UNTRAP_IRQ:
				return hyplet_untrap_irq(hplt.__action.irq);

		case HYPLET_IMP_TIMER:
				return hyplet_imp_timer();

		case HYPLET_SET_RPC:
				return hyplet_set_rpc(&hplt);

	}
	return rc;
}
