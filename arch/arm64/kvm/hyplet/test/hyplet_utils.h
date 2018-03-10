#ifndef __HYPLET_UTILS_H__
#define __HYPLET_UTILS_H__

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

static inline long cycles(void) {
	long cval;
	cval = ARCH_TIMER_READ("cntvct_el0"); 		
	return cval;
}

static inline long cycles_us(void) {

	struct timeval t;
	
	gettimeofday(&t,NULL);
	
	return t.tv_sec*1000000 + t.tv_usec;

}

static inline u64 cycles_to_ns()
{
	u64 t = cycles();
	return t * 52;
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

int hyplet_ctl(int cmd,struct hyplet_ctrl *hplt);
int hyplet_trap_irq(int irq);
int hyplet_map(int cmd, void *addr,int size);
int hyplet_untrap_irq(int irq);
int hyplet_rpc_set(void *user_hyplet,int func_id);
long hyplet_rpc_call(int func_id,...);
int hyplet_map_all(void);
int hyplet_set_stack(unsigned long addr,int size);

#endif

