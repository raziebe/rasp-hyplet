#ifndef __HYPLET_USER_H__
#define __HYPLET_USER_H__

typedef enum {
	   HYPLET_SET_CALLBACK = 1,
	   HYPLET_MAP_STACK = 2,
	   HYPLET_MAP_ALL = 3,
	   HYPLET_TRAP_IRQ = 4,
	   HYPLET_UNTRAP_IRQ = 5,
	   HYPLET_REGISTER_BH = 6, // register the task to wake up
	   HYPLET_SET_RPC  = 7,
	   HYPLET_IMP_TIMER = 8,
}hyplet_ops;


struct hyplet_map_addr {
	unsigned long addr  __attribute__ ((packed));
	int size   __attribute__ ((packed));
};

struct hyplet_irq_action {
	int action;
	int irq __attribute__ ((packed));
};

struct hyplet_rpc_set {
	long func_addr;
	int func_id __attribute__ ((packed));
};

struct hyplet_ctrl {
	int cmd  __attribute__ ((packed));
	union  {
		struct hyplet_map_addr 	addr ;
		struct hyplet_rpc_set 	rpc_set_func;
		int irq;
	}__action  __attribute__ ((packed));
};

#endif

