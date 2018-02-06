#ifndef __HYPLET_UTILS_H__
#define __HYPLET_UTILS_H__

static inline long cycles(void) {
	long cval;
	asm volatile ("isb \n");
	asm volatile ("mrs %0, cntvct_el0" : "=r" (cval));

	return cval;
}

int hyplet_ctl(int cmd,struct hyplet_ctrl *hplt);
int hyplet_trap_irq(int irq);
int hyplet_map(int cmd, void *addr,int size);
int hyplet_untrap_irq(int irq);

#endif

