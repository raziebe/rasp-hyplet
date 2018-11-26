#ifndef __HYPLET_UTILS_H__
#define __HYPLET_UTILS_H__

#include "hyp_spinlock.h"

typedef unsigned long long u64;
typedef signed long long s64;
static const int arm_arch_timer_reread = 1;

#define cycles_to_nano(val) (val * 52)

/* QorIQ errata workarounds */
#define ARCH_TIMER_REREAD(reg) ({ \
	u64 _val_old, _val_new; \
	int _timeout = 200; \
	do { \
		asm volatile("mrs %0, " reg ";" \
			     "mrs %1, " reg \
			     : "=r" (_val_old), "=r" (_val_new)); \
		_timeout--; \
	} while (_val_old != _val_new && _timeout); \
	_val_old; \
})

#define ARCH_TIMER_READ(reg) ({ \
	u64 _val; \
	if (arm_arch_timer_reread) \
		_val = ARCH_TIMER_REREAD(reg); \
	else \
		asm volatile("mrs %0, " reg : "=r" (_val)); \
	_val; \
})

static inline long __cycles(void) {
	long cval;
	cval = ARCH_TIMER_READ("cntvct_el0"); 		
	return cval;
}

// 19.2Mhz clock divisor
#define CLOCK_DIVISOR	52.083

static inline u64 hyp_gettime() {
	u64 t = __cycles();
	return t * CLOCK_DIVISOR;
}

static inline long cntvoff_el2(void)
{
	long val;
	asm ("mrs   %0, cntvoff_el2" : "=r" (val) );
	return val;
}

static inline void set_cntvoff_el2(long val)
{
	asm ("msr  cntvoff_el2, %0" : "=r" (val) );
}

#define PRINT_LINES	10

struct hyp_fmt {
	char fmt[128];
	long i[7];
	double f[7];
};


struct hyp_state {
	spinlock_t sync;
	int fmt_idx;
	struct hyp_fmt fmt[PRINT_LINES];
};

int hyplet_ctl(int cmd,struct hyplet_ctrl *hplt);
int hyplet_trap_irq(int irq,int cpu);
int hyplet_map(void *addr,int size,int cpu);
int hyplet_untrap_irq(int irq,int cpu);
int hyplet_rpc_set(void *user_hyplet,int func_id,int cpu);
long hyplet_rpc_call(int func_id,...);
int hyplet_map_all(int cpu);
int hyplet_set_stack(void* addr,int size,int cpu);
int hyplet_assign_offlet(int cpu, void* addr);
int hyp_print(const char *format, ...);
int hyp_print2(struct hyp_fmt *format);
void print_hyp(void);
int hyp_wait(int cpu,int ms);
int hyplet_set_print(void *addr, int cpu);
int hyplet_map_vma(void *addr,int cpu);
int get_section_addr(char *secname,long *addr, int *size);
int Elf_parser_load_memory_map(char *prog);

#endif

