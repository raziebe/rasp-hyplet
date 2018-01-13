#ifndef __TRULY_H_
#define __TRULY_H_

// page 1775
#define DESC_TABLE_BIT 			( UL(1) << 1 )
#define DESC_VALID_BIT 			( UL(1) << 0 )
#define DESC_XN	       			( UL(1) << 54 )
#define DESC_PXN	      		( UL(1) << 53 )
#define DESC_CONTG_BIT		 	( UL(1) << 52 )
#define DESC_AF	        		( UL(1) << 10 )
#define DESC_SHREABILITY_SHIFT		(8)
#define DESC_S2AP_SHIFT			(6)
#define DESC_MEMATTR_SHIFT		(2)

#define VTCR_EL2_T0SZ_BIT_SHIFT 	0
#define VTCR_EL2_SL0_BIT_SHIFT 		6
#define VTCR_EL2_IRGN0_BIT_SHIFT 	8
#define VTCR_EL2_ORGN0_BIT_SHIFT 	10
#define VTCR_EL2_SH0_BIT_SHIFT 		12
#define VTCR_EL2_TG0_BIT_SHIFT 		14
#define VTCR_EL2_PS_BIT_SHIFT 		16

#define SCTLR_EL2_EE_BIT_SHIFT		25
#define SCTLR_EL2_WXN_BIT_SHIFT		19
#define SCTLR_EL2_I_BIT_SHIFT		12
#define SCTLR_EL2_SA_BIT_SHIFT		3
#define SCTLR_EL2_C_BIT_SHIFT		2
#define SCTLR_EL2_A_BIT_SHIFT		1	
#define SCTLR_EL2_M_BIT_SHIFT		0

/* Hyp Configuration Register (HCR) bits */
#define HCR_ID		(UL(1) << 33)
#define HCR_CD		(UL(1) << 32)
#define HCR_RW_SHIFT	31
#define HCR_RW		(UL(1) << HCR_RW_SHIFT) //
#define HCR_TRVM	(UL(1) << 30)
#define HCR_HCD		(UL(1) << 29)
#define HCR_TDZ		(UL(1) << 28)
#define HCR_TGE		(UL(1) << 27)
#define HCR_TVM		(UL(1) << 26) // Trap Virtual Memory controls.
#define HCR_TTLB	(UL(1) << 25)
#define HCR_TPU		(UL(1) << 24)
#define HCR_TPC		(UL(1) << 23)
#define HCR_TSW		(UL(1) << 22)  // Trap data or unified cache maintenance
#define HCR_TAC		(UL(1) << 21)  // Trap Auxiliary Control Register
#define HCR_TIDCP	(UL(1) << 20)  // Trap IMPLEMENTATION DEFINED functionality
#define HCR_TSC		(UL(1) << 19)  // Trap SMC
#define HCR_TID3	(UL(1) << 18)
#define HCR_TID2	(UL(1) << 17)
#define HCR_TID1	(UL(1) << 16)
#define HCR_TID0	(UL(1) << 15)
#define HCR_TWE		(UL(1) << 14) // traps Non-secure EL0 and EL1 execution of WFE instructions to
#define HCR_TWI		(UL(1) << 13) // traps Non-secure EL0 and EL1 execution of WFI instructions to EL2,
#define HCR_DC		(UL(1) << 12)
#define HCR_BSU		(3 << 10)
#define HCR_BSU_IS	(UL(1) << 10) // Barrier Shareability upgrade
#define HCR_FB		(UL(1) << 9)  // Force broadcast.
#define HCR_VA		(UL(1) << 8)
#define HCR_VI		(UL(1) << 7)
#define HCR_VF		(UL(1) << 6)
#define HCR_AMO		(UL(1) << 5) //Physical SError Interrupt routing.
#define HCR_IMO		(UL(1) << 4)
#define HCR_FMO		(UL(1) << 3)
#define HCR_PTW		(UL(1) << 2)
#define HCR_SWIO	(UL(1) << 1) // Set/Way Invalidation Override
#define HCR_VM		(UL(1) << 0)

#define HYP_PAGE_OFFSET_SHIFT	VA_BITS
#define HYP_PAGE_OFFSET_MASK	((UL(1) << HYP_PAGE_OFFSET_SHIFT) - 1)
#define HYP_PAGE_OFFSET		(PAGE_OFFSET & HYP_PAGE_OFFSET_MASK)
#define KERN_TO_HYP(kva)	((unsigned long)kva - PAGE_OFFSET + HYP_PAGE_OFFSET)

#define TP_HCR_GUEST_FLAGS ( HCR_RW | HCR_VM)

#define ESR_ELx_EC_SVC_64 0b10101
#define ESR_ELx_EC_SVC_32 0b10001

#define __hyp_text __section(.hyp.text) notrace

#define __int8  char
typedef unsigned __int8 UCHAR;

enum { ECB=0, CBC=1, CFB=2 };
enum { DEFAULT_BLOCK_SIZE=16 };
enum { MAX_BLOCK_SIZE=32, MAX_ROUNDS=14, MAX_KC=8, MAX_BC=8 };

struct matsov_protect {
	unsigned long addr;
	int size;
};

struct truly_vm {
 	struct matsov_protect protect;
	unsigned long hcr_el2;
 	unsigned int  hstr_el2;
 	unsigned long vttbr_el2;
 	unsigned int  vtcr_el2;
 	unsigned long tpidr_el2;
 	unsigned long mdcr_el2;
 	unsigned long elr_el2;
 	unsigned long el2_sp;
 	unsigned long el1_sp;
 	unsigned long ich_hcr_el2;
 /*	unsigned long ich_vmcr_el2; control VM */

 /* 	unsigned long ich_eisr_el2;  end of interrupt status register */
/* 	unsigned long ich_misr_el2; maintenance ISR */
/* 	ICH_ELSR_EL2 */
/*	ICH_HCR_EL2, xzr
	x21, ICH_VTR_EL2
*/
 	unsigned long regs[30];
 	unsigned long initialized; 	
 	unsigned long id_aa64mmfr0_el1;
   	void* pg_lvl_one;
   	char print_buf[1024];
} __attribute__ ((aligned (8)));

static inline struct truly_vm *el2_get_tvm(void)
{
	struct truly_vm *tv;
    asm("mrs %0,tpidr_el2\n":"=r"(tv));
	return tv;
}

extern char __truly_vectors[];
int truly_init(void);
void truly_clone_vm(void *);
void truly_smp_run_hyp(void);
void tp_run_vm(void *);
void truly_run_vm(void *);
long tp_call_hyp(void *hyper_func, ...);
void truly_exit_el1(void *hyp_func,...);
void truly_enter_el1(void *hyp_func,...);
unsigned long truly_get_tcr_el1(void);
unsigned long truly_get_hcr_el2(void);
unsigned long tp_get_ttbr0_el2(void);
void truly_set_vectors(unsigned long vbar_el2);
unsigned long truly_get_vectors(void);
int create_hyp_mappings(void *, void *);
void __hyp_text el2_sprintf(const char *fmt, ...);
int __hyp_text el2_printk(const char *fmt, ...);
long get_vgic_ver(void);
void route_to_el2(void);
void unroute_to_el2(void);

#define tp_info(fmt, ...) \
	pr_info("truly %s [%i]: " fmt, __func__,raw_smp_processor_id(), ## __VA_ARGS__)

#define tp_err(fmt, ...) \
	pr_err("truly [%i]: " fmt, raw_smp_processor_id(), ## __VA_ARGS__)

#endif
