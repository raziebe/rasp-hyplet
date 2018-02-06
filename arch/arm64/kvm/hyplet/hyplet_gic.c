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
	struct irq_desc *desc;
	
	for ( i = 0; i < NR_IRQS; i++){ 
		desc  = irq_to_desc(i);
		if (!desc) {
			hyplet_err("Invalid irqs %d\n",i);
			continue;
		}
		printk("%d : %ld\n", i,  desc->irq_data.hwirq);
	}
	return 0;
}

int hyplet_trap_irq(int irq)
{
	struct irq_desc *desc;
	struct hyplet_vm *tv = hyplet_get_vm();

	desc  = irq_to_desc(irq);
	if (!desc) {
		hyplet_err("Incorrect irq %d\n",irq);
		return -EINVAL;
	}

	tv->task_struct = current;
	if (!(tv->state & (USER_CODE_MAPPED | USER_STACK_MAPPED))){
		return -EINVAL;
	}
	tv->state |= RUN_HYPLET;
	tv->irq_to_trap = desc->irq_data.hwirq;
	mb();
	hyplet_info("Trapping global irq %d : local irq is %d,%ld\n"
			,irq ,tv->irq_to_trap, desc->irq_data.hwirq);
	return 0;
}

int hyplet_untrap_irq(int irq)
{
	struct irq_desc *desc;
	struct hyplet_vm *tv = hyplet_get_vm();

	desc  = irq_to_desc(irq);
	if (!desc) {
		hyplet_err("Incorrect irq %d\n",irq);
		return -EINVAL;
	}

	if (desc->irq_data.hwirq != tv->irq_to_trap){
		hyplet_err("Incorrect hwirq %d because"
					" local irq is %ld\n"
				,tv->irq_to_trap,
				desc->irq_data.hwirq);
		return -EINVAL;
	}
	hyplet_reset(current);
	return 0;
}

int hyplet_run(int hwirq)
{
	struct hyplet_vm *tv = hyplet_get_vm();

	if (tv->task_struct && hwirq == 8)
        	hyplet_call_hyp(hyplet_run_user);

	return 0; // TODO
}

void hyplet_reset(struct task_struct *tsk)
{
	struct hyplet_vm *tv = hyplet_get_vm();

	if (tv->task_struct != tsk)
		return;

	tv->irq_to_trap = 0;
	mb();
	hyplet_free_mem();
	tv->state  = 0;
	tv->task_struct = NULL;
	hyplet_info("reset pid=%d\n",tsk->pid);
}
