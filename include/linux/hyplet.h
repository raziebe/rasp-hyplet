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
#define USER_TO_HYP(uva)	(uva)
#define HYPLET_HCR_GUEST_FLAGS 	(HCR_RW | HCR_VM | HCR_IMO)

#define ESR_ELx_EC_SVC_64 0b10101
#define ESR_ELx_EC_SVC_32 0b10001

#define __hyp_text __section(.hyp.text) notrace

#define __int8  char
typedef unsigned __int8 UCHAR;

enum { ECB=0, CBC=1, CFB=2 };
enum { DEFAULT_BLOCK_SIZE=16 };
enum { MAX_BLOCK_SIZE=32, MAX_ROUNDS=14, MAX_KC=8, MAX_BC=8 };

typedef enum { HYPLET_MAP_CODE = 1,
	   HYPLET_MAP_STACK = 2,
	   HYPLET_MAP_ANY= 3,
	   HYPLET_TRAP_IRQ = 4,
	   HYPLET_UNTRAP_IRQ = 5,
}hyplet_ops;

#define USER_CODE_MAPPED		UL(1) << 1
#define USER_STACK_MAPPED		UL(1) << 2
#define USER_MEM_ANON_MAPPED	UL(1) << 3
#define RUN_HYPLET				UL(1) << 4

struct hyplet_map_addr {
	unsigned long addr;
	int size;
};


struct hyplet_ctrl {
	int cmd  __attribute__ ((packed));
	union  {
		struct hyplet_map_addr addr;
		int irq;
	}__action  __attribute__ ((packed));
};

struct hyp_addr {
	struct list_head lst;
	unsigned long addr;
	int size;
};

struct hyplet_vm {

	unsigned long gic_irq;
	unsigned long irq_to_trap;
	unsigned long hyplet_stack;
	unsigned long hyplet_code;
	unsigned long ttbr0_el1;

	void *task_struct;
	unsigned long hcr_el2;

	unsigned int  hstr_el2;
 	unsigned long vttbr_el2;
 	unsigned int  vtcr_el2;
 	unsigned long mdcr_el2;
 	unsigned long elr_el2;
 	unsigned long el2_sp;
 	unsigned long el1_sp;
 	unsigned long ich_hcr_el2;
 	struct list_head hyp_addr_lst;
 	unsigned int state;

 	unsigned long initialized; 	
 	unsigned long id_aa64mmfr0_el1;
   	void* pg_lvl_one;
   	char print_buf[1024];
} __attribute__ ((aligned (8)));

static inline struct hyplet_vm *hyplet_get_vm(void){
	struct hyplet_vm *tv;
    asm("mrs %0,tpidr_el2\n":"=r"(tv));
	return tv;
}

extern char __hyplet_vectors[];

unsigned long get_el1_irq(void);
struct hyplet_vm* hyplet_vm(void);
int  hyplet_init(void);
void hyplet_clone_vm(void *);
void hyplet_smp_run_hyp(void);
void hyplet_run_vm(void *);
void hyplet_prepare_vm(void *);
long hyplet_call_hyp(void *hyper_func, ...);
void hyplet_exit_el1(void *hyp_func,...);
void hyplet_enter_el1(void *hyp_func,...);
unsigned long hyplet_get_tcr_el1(void);
unsigned long hyplet_get_hcr_el2(void);
unsigned long hyplet_get_ttbr0_el2(void);
void hyplet_set_vectors(unsigned long vbar_el2);
unsigned long hyplet_get_vectors(void);
int create_hyp_mappings(void *, void *);
unsigned long hyplet_create_pg_tbl(void *cxt);
void make_vtcr_el2(struct hyplet_vm *tvm);
unsigned long kvm_uaddr_to_pfn(unsigned long uaddr);
int hyplet_map_user_data(hyplet_ops ,  void *action);
int hyplet_trap_irq(int irq);
int hyplet_untrap_irq(int irq);
int hyplet_start(void);
void hyplet_reset(struct task_struct *tsk);
void hyplet_invld_tlb(unsigned long);
void hyplet_free_mem(void);
void hyplet_reset(struct task_struct *tsk);
void hyp_user_unmap(unsigned long umem,int size);

#define PAGE_HYP_USER	( PROT_DEFAULT  | PTE_ATTRINDX(0) ) // not shared,
extern int __create_hyp_mappings(pgd_t *pgdp,
				 unsigned long start, unsigned long end,
				 unsigned long pfn, pgprot_t prot);



long hyplet_get_vgic_ver(void);
void hyplet_enable_imo(void);
void hyplet_imo(void);

#define hyplet_info(fmt, ...) \
		pr_info("hyplet %s [%i]: " fmt, __func__,raw_smp_processor_id(), ## __VA_ARGS__)

#define hyplet_err(fmt, ...) \
		pr_err("hyplet [%i]: " fmt, raw_smp_processor_id(), ## __VA_ARGS__)

#endif
