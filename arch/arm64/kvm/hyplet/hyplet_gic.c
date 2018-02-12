#include <linux/module.h>
#include <linux/init.h>
#include <linux/highmem.h>
#include <linux/compiler.h>
#include <linux/linkage.h>

#include <linux/init.h>
#include <asm/sections.h>
#include <asm/page.h>
#include <linux/vmalloc.h>
#include <asm/fixmap.h>
#include <asm/memory.h>
#include <linux/irqchip.h>
#include <linux/irqchip/arm-gic-common.h>
#include <linux/irqchip/arm-gic-v3.h>
#include <asm/arch_gicv3.h>
#include <linux/irq.h>
#include <linux/irqdesc.h>

#include <linux/delay.h>
#include <linux/hyplet.h>
#include <linux/hyplet_user.h>

int hyplet_trapped_irq(void)
{
	struct hyplet_vm *tv = hyplet_get_vm();
	unsigned long gic_irq = tv->gic_irq;

	tv->gic_irq = 0;
	return (int)gic_irq;
}

int hyplet_ctl(unsigned long arg)
{
	struct hyplet_vm *tvm = hyplet_get_vm();
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
				tvm->hyplet_stack = 
					(long)(hplt.__action.addr.addr) + 
						hplt.__action.addr.size - PAGE_SIZE;
				break;

		case HYPLET_MAP_CODE:
				rc = hyplet_map_user_data(hplt.cmd , (void *)&hplt.__action);
				if ( rc )
					return -EINVAL;
				tvm->hyplet_code = hplt.__action.addr.addr;
				break;
		case HYPLET_TRAP_IRQ:
				// user provides the irq, we must find hw_irq
				return hyplet_trap_irq(hplt.__action.irq);

		case HYPLET_UNTRAP_IRQ:
				return hyplet_untrap_irq(hplt.__action.irq);
	
	   	case HYPLET_DUMP_HWIRQ:
				return hyplet_dump_irqs();
	}
	return rc;
}

int hyplet_dump_irqs(void)
{
	int i;
	for (i = 0; i < NR_IRQS; i++) {
		int irq = hyplet_hwirq_to_irq(i);
		printk("hwirq %d : irq %d\n", i, irq);
	}
	return 0;
}

int hyplet_search_irq(int lirq)
{
	int i;
	for (i = 0; i < NR_IRQS; i++) {
		int irq = hyplet_hwirq_to_irq(i);
		if (irq == lirq)
			return i;
	}
	return 0;
}

int hyplet_trap_irq(int irq)
{
	struct hyplet_vm *tv = hyplet_get_vm();
	int hwirq;

	hwirq = hyplet_search_irq(irq);
	if (hwirq == 0){
		hyplet_err("invalid irq %d\n",irq);
		return -EINVAL;
	}

	tv->tsk = current;
	if (!(tv->state & (USER_CODE_MAPPED | USER_STACK_MAPPED))){
		return -EINVAL;
	}
	tv->irq_to_trap = hwirq;
	hyplet_info("Trapping irq %d local irq %d\n", irq,hwirq);
	mb();
	return 0;
}

int hyplet_untrap_irq(int irq)
{
	hyplet_reset(current);
	return 0;
}

int hyplet_run(int hwirq)
{
	struct hyplet_vm *hyp;
	
	hyp = hyplet_get_vm();

	if (hyp->tsk && (hwirq == hyp->irq_to_trap)) {
		struct timespec64 tv;

		getnstimeofday64(&tv);
		hyp->ts = tv.tv_sec * NSEC_PER_SEC + tv.tv_nsec;
		isb();
		hyplet_call_hyp(hyplet_run_user);
	}
	return 0; // TODO
}

void hyplet_reset(struct task_struct *tsk)
{
	int cpu;
	struct hyplet_vm *tv;

	for_each_online_cpu(cpu) {
		tv =  hyplet_get(cpu);
		if (!tv->tsk)
			continue;
		if (tv->tsk->mm == tsk->mm){
			hyplet_stop(tv);
		}
	}
}

void hyplet_stop(void *info)
{
	struct hyplet_vm *tv = (struct hyplet_vm *)info;

	tv->tsk = NULL;
	tv->irq_to_trap = 0;
	mb();
	msleep(10);
	hyplet_free_mem(tv);
	tv->state  = 0;
}
