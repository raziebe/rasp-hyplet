#ifndef __USER_HYPLET_H_
#define __USER_HYPLET_H_

enum { 	
	HYPLET_MAP_CODE = 1,
	HYPLET_MAP_STACK = 2,
	HYPLET_MAP_ANY = 3,
	HYPLET_TRAP_IRQ = 4,
	HYPLET_UNTRAP_IRQ = 5,
	HYPLET_TEST_OPEN_TIMER = 6,
	HYPLET_TEST_WAIT_TIMER = 7,
};

struct hyplet_map_addr {
	unsigned long addr;
	int size;
};

struct hyplet_irq_action {
	int action;
	int irq;
};

struct hyplet_ctrl {
	int cmd  __attribute__ ((packed));
	union  {
		struct hyplet_map_addr addr;
		int irq;
	}__action  __attribute__ ((packed));
};

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
