#include <linux/linkage.h>
#include <linux/irqchip/arm-gic.h>

#include <asm/assembler.h>
#include <asm/memory.h>
#include <asm/asm-offsets.h>
#include <asm/kvm.h>
#include <asm/kvm_asm.h>
#include <asm/kvm_arm.h>
#include <asm/kvm_mmu.h>
#include <asm/alternative.h>
#include <asm/cpufeature.h>
#include <linux/irqchip/arm-gic-v3.h>


.text
.pushsection	.hyp.text, "ax" // borrow  hyp.text
.align	PAGE_SHIFT


.macro invalid_vector   label,num
.align 7
\label:
	mov	x6, \num
        b __hyplet_panic
ENDPROC(\label)
.endm

.macro push_registers
	push x0,x1
	push x2,x3
	push x4,x5
	push x6,x7
	push x8,x9
	push x10,x11
	push x12,x13
	push x14,x15
	push x16,x17
	push x18,x19
	push x20,x21
	push x22,x23
	push x24,x25
	push x26,x27
	push x28,x29
	push x30,xzr
.endm

.macro pop_registers
	pop x30,xzr
	pop x28,x29
	pop x26,x27
	pop x24,x25
	pop x22,x23
	pop x20,x21
	pop x18,x19
	pop x16,x17
	pop x14,x15
	pop x12,x13
	pop x10,x11
	pop x8,x9
	pop x6,x7
	pop x4,x5
	pop x2,x3
	pop x0,x1
.endm

EL1_sync:
	push	x0, x1
	push	x2, x3

	mrs		x1, esr_el2
	lsr		x2, x1, #ESR_ELx_EC_SHIFT	// Syndrom register shift by 26 bits

	cmp	x2, #ESR_ELx_EC_HVC64	// If not 10110 then we have a trap
	beq	2f
	cmp x2,#ESR_ELx_EC_SYS64
	b.eq do_msr
	b 3f

	/* Here, we're pretty sure the host called HVC */
2:	pop		x2, x3
	pop		x0, x1

	/* Check for __hyp_get_vectors */
	cbnz	x0, 1f
	mrs		x0, vbar_el2
	b		2f

1:	push	lr, xzr	
	/*
	 * Compute the function address in EL2, and shuffle the parameters.
	 */
	kern_hyp_va	x0
	mov		lr, x0		// function address
	mov		x0, x1		// the context
	mov		x1, x2
	mov		x2, x3
	blr		lr

	pop	lr, xzr
	eret

3:
	pop x2,x3
	pop x0,x1

2:	eret
ENDPROC(EL1_sync)

/* Check if this is a hyp controlleed interrupt
  *    if not then just exit

*/
do_msr:
    mrs	 	x0, tpidr_el2
    mrs_s  	x1, ICC_IAR1_EL1
    isb
	mov		 x2, xzr
	ldr      x2,[x0, HYPLET_TRAP_IRQ]
	cmp		 x2, x1
	bne		 2f
/*
 *	User has set an hyplet
 *  prepare to jump to hyplet
*/
	push_registers
	mrs		x1, ttbr0_el1
	mrs		x2, sp_el0
	push    x1, x2

	ldr		x1,[x0, HYPLET_TTBR0_EL1]
	msr		ttbr0_el1, x1
	ldr		x1,[x0, HYPLET_STACK]
	msr		sp_el0,x1
	ldr		x30,[x0, HYPLET_CODE]

/*  EL2t. Switch stacks */
    mrs  x1, spsel
    and  w1,w1,#0xFFFFFFFE
    msr  spsel,x1

	blr x30 // Execute hyplet in EL2

/*
 * switch stacks back
*/
    mrs x1, spsel
    orr w1, w1, #0x1
    msr spsel,x1

	pop     x1,x2
	msr		ttbr0_el1,x1
	msr		sp_el0, x2
	pop_registers

/*
 * Disable access untill the next
 * interrupt will take place
*/
2:
    str    	x1,[x0, #HYPLET_GIC_IRQ]
	mrs		x1, elr_el2
	add		x1, x1, #4
	msr		elr_el2,x1
	msr_s	ICH_HCR_EL2,xzr

1:
	pop x2,x3
	pop x0,x1

	eret
ENDPROC(do_msr)

/* borrowed from kvm */
__hyplet_panic:
	adr	x0, __hyp_panic_str
	adr	x1, 2f // adr generates a register-relative address in the destination register
	ldp	x2, x3, [x1] // Load to Pair of Registers from two dwords starting from memory at [x1] 
	sub	x0, x0, x2
	add	x0, x0, x3
	mrs	x1, spsr_el2
	mrs	x2, elr_el2
	mrs	x3, esr_el2
	mrs	x4, far_el2
	mrs	x5, hpfar_el2
	mrs	x7, tpidr_el2

	mov	lr, #(PSR_F_BIT | PSR_I_BIT | PSR_A_BIT | PSR_D_BIT |\
		      PSR_MODE_EL1h)
	msr	spsr_el2, lr
	ldr	lr, =panic
	msr	elr_el2, lr
	eret

	.align	3
2:	.quad	HYP_PAGE_OFFSET
	.quad	PAGE_OFFSET
ENDPROC(__hyplet_panic)
__hyp_panic_str:
	.ascii	"Truly panic:\nCode:%08x PC:%016x ESR:%08x\nFAR:%016x" \
	" HPFAR:%016x RAZDBG:%p\nTrulyCxt:%p\n\0"

ENTRY(hyplet_get_tcr_el1)
       mrs     x0, tcr_el1
       ret
ENDPROC(hyplet_get_tcr_el1)

ENTRY(hyplet_invld_tlb)
		tlbi vae2, x0
        tlbi vale2, x0
		ret
ENDPROC(hyplet_invld_tlb)

/*
* This procedure calls the default hypervisor vector and
* and sets truly vector. This is because when the cpu drops
*  Linux calls smc and vbar_el2 resets.
*/
ENTRY(hyplet_get_vectors)
	mov	x0,xzr
ENTRY(hyplet_set_vectors)
	hvc #0
	ret
ENDPROC(hyplet_set_vectors)


/*
 * Call into the vgic backend for state saving
 */
ENTRY(hyplet_get_vgic_ver)
alternative_if_not ARM64_HAS_SYSREG_GIC_CPUIF
        mov     x0,#2
alternative_else
		mov x0,#3
alternative_endif
	ret
ENDPROC(hyplet_get_vgic_ver)


ENTRY(hyplet_call_hyp)
        hvc	#0
        ret
ENDPROC(hyplet_call_hyp)

/*
 * call right after EOI.
*/
ENTRY(hyplet_enable_imo)
		mov		w1, #0x1
		mov		x0, #1
		lsl		w0, w0, #31
		orr		x0, x0, x1
		orr		x0, x0, #16
		msr		hcr_el2, x0
  		ret
ENDPROC(hyplet_enable_imo)


ENTRY(hyplet_run_vm)

	pop	lr, xzr

	push x0,x1
	push x3,x4

	mov	x4, lr		// save the link register of EL1 before losing it.

	kern_hyp_va  x0	// grab tvm
	msr	tpidr_el2, x0	// save tvm context

	ldr		x1, [x0, #HYPLET_HCR_EL2]
    msr     hcr_el2, x1

 	ldr 	x1, [x0, #HYPLET_HSTR_EL2]
	msr 	hstr_el2, x1

    ldr		x1, [x0, #HYPLET_VTCR_EL2]
	msr		vtcr_el2, x1

    ldr     x1, [x0, #HYPLET_VTTBR_EL2]
    msr 	vttbr_el2, x1

	mov	lr,x4

	pop x3,x4
	pop x0,x1

	eret
ENDPROC(hyplet_run_vm)

/*
 * All interrupts walks through this section
  * Turn on msr trap.. Turn off IMO
  and leave
  *
*/
EL1_64_irq:
		push x0,x1
/*
* 	I rely that the next msr  on this processor would be
*	to read the interrypt
*/
		mov     x0,#(ICH_EL2_TALL1)
		msr_s	ICH_HCR_EL2,x0
/*
 * disable hcr IMO
*/
       	mov     w1, #0x1
       	mov     x0, #1
       	lsl     x0, x0, #31
       	orr     x0, x0, x1
       	msr     hcr_el2, x0

	   	pop x0,x1
       	eret
ENDPROC(EL1_64_irq)

.align 11
ENTRY(__hyplet_vectors)
        invalid_vector	EL2_sync,#1                        //Current EL with SP0
        invalid_vector  EL2_irq_invalid,#2                 // IRQ EL2t
        invalid_vector  EL2_fiq_invalid,#3                // FIQ EL2t
        invalid_vector  EL2_error_invalid,#4             // Error EL2t

        invalid_vector  ELx_sync, #5  	              						// Current EL with SPx
        invalid_vector  EL2_irq_invalidELSpx,#6                 // IRQ EL2h
        invalid_vector  EL2_fiq_invalidELSpx,#7                 // FIQ EL2h
        invalid_vector  EL2_error_invalidELspx,#8               // Error EL2h

        ventry  		EL1_sync                        			// Synchronous 64-bit EL1
        ventry  		EL1_64_irq
        invalid_vector  EL1_fiq_invalid, #9                // FIQ 64-bit EL1
        invalid_vector  EL1_error_invalid ,#10             // Error 64-bit EL1

        invalid_vector	EL132_irq,#17  	          					// Synchronous 32-bit EL1
        invalid_vector  EL1_irq_invalid, #11                 		// IRQ 32-bit EL1
        invalid_vector  EL1_fiq_invalidLowEL32 ,#12               // FIQ 32-bit EL1
        invalid_vector  EL1_error_invalidLowEL32,#13               // Error 32-bit EL1
ENDPROC(__hyplet_vectors)

.popsection
