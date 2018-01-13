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
        b __truly_panic
ENDPROC(\label)
.endm

.macro save_registers

	mrs	 x0, 	 tpidr_el2
	add  x0,x0,   #TP_REGS
	stp	 x1,x2,  [x0]
	stp  x2,x3,  [x0 , #16]
	stp  x4,x5,  [x0 , #32]
	stp  x6,x7,  [x0 , #48]
	stp  x8,x9,  [x0 , #64]
	stp  x10,x11,[x0 , #80]
	stp x12,x13, [x0 , #96]
	stp x14,x15, [x0 , #112]
	stp x16,x17, [x0 , #128]
	stp x18,x19, [x0 , #144]
	stp x20,x21, [x0 , #160]
	stp x22,x23, [x0 , #176]
	stp x24,x25, [x0 , #192]
	stp x26,x27, [x0 , #208]
	stp x28,x29, [x0 , #224]
	str	x30,     [x0,  #232]
.endm


.macro restore_registers
	// restore stack pointer
	mrs	 x0, 	 tpidr_el2
	add  x0,x0,   #TP_REGS
	ldp	 x1,x2,  [x0]
	ldp  x2,x3,  [x0 , #16]
	ldp  x4,x5,  [x0 , #32]
	ldp  x6,x7,  [x0 , #48]
	ldp  x8,x9,  [x0 , #64]
	ldp  x10,x11,[x0 , #80]
	ldp x12,x13, [x0 , #96]
	ldp x14,x15, [x0 , #112]
	ldp x16,x17, [x0 , #128]
	ldp x18,x19, [x0 , #144]
	ldp x20,x21, [x0 , #160]
	ldp x22,x23, [x0 , #176]
	ldp x24,x25, [x0 , #192]
	ldp x26,x27, [x0 , #208]
	ldp x28,x29, [x0 , #224]
	ldr	x30,     [x0,  #232]

.endm

EL1_sync:
	push	x0, x1
	push	x2, x3

	mrs	x1, esr_el2
	lsr	x2, x1, #ESR_ELx_EC_SHIFT	// Syndrom register shift by 26 bits

	cmp	x2, #ESR_ELx_EC_HVC64	// If not 10110 then we have a trap
	b.ne	3f

	/* Here, we're pretty sure the host called HVC */
	pop	x2, x3
	pop	x0, x1

	/* Check for __hyp_get_vectors */
	cbnz	x0, 1f
	mrs	x0, vbar_el2
	b	2f

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
	pop x0,x1
	pop x2,x3

2:	eret
ENDPROC(EL1_sync)


__truly_panic:
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
ENDPROC(__truly_panic)
__hyp_panic_str:
	.ascii	"Truly panic:\nCode:%08x PC:%016x ESR:%08x\nFAR:%016x" \
	" HPFAR:%016x RAZDBG:%p\nTrulyCxt:%p\n\0"

ENTRY(truly_get_tcr_el1)
       mrs     x0, tcr_el1
       ret
ENDPROC(truly_get_tcr_el1)

/*
* This procedure calls the default hypervisor vector and
* and sets truly vector. This is because when the cpu drops
*  Linux calls smc and vbar_el2 resets.
*/
ENTRY(truly_get_vectors)
	mov	x0,xzr
ENTRY(truly_set_vectors)
	hvc #0
	ret
ENDPROC(truly_set_vectors)

ENTRY(tp_call_hyp)
        hvc	#0
        ret
ENDPROC(tp_call_hyp)

ENTRY(route_to_el2)
		mov		w1, #0x1
		mov		x0, #1
		lsl		w0, w0, #31
		orr		x0, x0, x1
		orr		x0, x0, #16
		msr		hcr_el2, x0
  		ret
ENDPROC(route_to_el2)


ENTRY(truly_run_vm)
	
	pop	lr, xzr
	mov	x4, lr		// save the link register of EL1 before losing it.
	
	kern_hyp_va  x0	// grab tvm
	msr	tpidr_el2, x0	// save tvm context

	msr	mdcr_el2, xzr

	ldr	x1, [x0, #TP_HCR_EL2]
    	msr     hcr_el2, x1
	
 	ldr 	x1, [x0, #TP_HSTR_EL2]
   	msr 	hstr_el2, x1

    	ldr	x1, [x0, #TP_VTCR_EL2]
	msr	vtcr_el2, x1
 
    	ldr     x1, [x0, #TP_VTTBR_EL2]
    	msr 	vttbr_el2, x1
	// Clear cntvoff for the host
	msr	cntvoff_el2, xzr

	mov	lr,x4

	eret
ENDPROC(truly_run_vm)

EL1_64_irq:
	push 	x0,x1
	mov		w1, #0x1
	mov		x0, #1
	lsl		x0, x0, #31
	orr		x0, x0, x1
	msr		hcr_el2, x0
	pop		x0,x1
	eret
ENDPROC(EL1_64_irq)


// http://infocenter.arm.com/help/index.jsp?topic=/com.arm.doc.den0024a/CHDEEDDC.html
.align 11
ENTRY(__truly_vectors)
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
ENDPROC(__truly_vectors)

.popsection
