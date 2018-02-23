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

int hyplet_imp_timer(void)
{
	struct hyplet_vm *hyp = hyplet_get_vm();

	hyp->irq_to_trap = IRQ_TRAP_ALL;

	if (!(hyp->state & (USER_CODE_MAPPED | USER_STACK_MAPPED))){
		return -EINVAL;
	}
	hyp->tsk = current;
	mb();
	hyplet_info("Implement timer\n");
	return 0;
}


int hyplet_dump_irqs(void)
{
	int i;
	for (i = 0; i < 0x3FF; i++) {
		int irq = hyplet_hwirq_to_irq(i);
		if (irq != 0)
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
	if (hyp->tsk == NULL)
		return 0; 

	if (hwirq == hyp->irq_to_trap
			|| hyp->irq_to_trap ==  IRQ_TRAP_ALL) {

		struct timespec64 tv;


		getnstimeofday64(&tv);
		hyp->ts = tv.tv_sec * NSEC_PER_SEC + tv.tv_nsec;
		hyplet_call_hyp(hyplet_run_user);
	}
	return 0; // TODO
}

