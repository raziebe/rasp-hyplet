#include <linux/linkage.h>
#include <asm/assembler.h>
#include <asm/memory.h>
#include <asm/asm-offsets.h>
#include "hyp_mmu.h"
#include "tp_hyp_flags.h"

.text
.pushsection	.hyp.text, "ax" // borrow  hyp.text
.align	PAGE_SHIFT

.macro zero_all
	mov x0, xzr
	mov x1,xzr
	mov x2,xzr
	mov x3,xzr
	mov x4,xzr
	mov x5,xzr
	mov x6,xzr
	mov x7,xzr
	mov x8,xzr
	mov x9,xzr
	mov x10,xzr
	mov x11,xzr
	mov x12,xzr
	mov x13,xzr
	mov x14,xzr
	mov x15,xzr
	mov x16,xzr
	mov x17,xzr
	mov x18,xzr
	mov x19,xzr
	mov x20,xzr
	mov x21,xzr
	mov x22,xzr
	mov x23,xzr
	mov x24,xzr
	mov x25,xzr
	mov x26,xzr
	mov x27,xzr
	mov x28,xzr
	mov x29,xzr
.endm

.macro invalid_vector   label,num
.align 7
\label:
	mov	x6, \num
        b __hyplet_panic
ENDPROC(\label)
.endm

.macro  push, xreg1, xreg2
        stp     \xreg1, \xreg2, [sp, #-16]!
.endm

.macro  pop, xreg1, xreg2
        ldp     \xreg1, \xreg2, [sp], #16
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

	mrs	x1, esr_el2
	lsr	x2, x1, #ESR_ELx_EC_SHIFT	// Syndrom register shift by 26 bits

	cmp x2, #ESR_ELx_EC_BRK64	// user space performed watchpoint
	b.eq 4f
	cmp	x2, #ESR_ELx_EC_HVC64	// If not 10110 then we have a trap
	b.ne 	3f

	/* Here, we're pretty sure the host called HVC */
2:	pop	x2, x3
	pop	x0, x1

	/* Check for __hyp_get_vectors */
	cbnz	x0, 1f
	mrs	x0, vbar_el2
	b	2f

1:	push	lr, xzr	
	/*
	 * Compute the function address in EL2, and shuffle the parameters.
	 * The only registers pushed are lr,xzr
	 */
	kern_hyp_va	x0
	mov	lr, x0		// function address
	mov	x0, x1		// the context
	mov	x1, x2
	mov	x2, x3
	blr	lr

	pop	lr, xzr
	eret

4:
	pop x2,x3
	pop x0,x1
	b  hyplet_rpc
3:
	pop x2,x3
	pop x0,x1

2:	eret
ENDPROC(EL1_sync)


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
	.ascii	"hyplet panic:\nCode:%08x PC:%016x ESR:%08x\nFAR:%016x" \
	" HPFAR:%016x RAZDBG:%p\nTrulyCxt:%p\n\0"

ENTRY(hyplet_flush_el2_dcache)
	dc      civac, x0     // clean & invalidate D line / unified line
   	dsb     ish
	ret
ENDPROC(hyplet_flush_el2_dcache)

ENTRY(hyplet_flush_el2_icache)
    ic      ivau, x0
   	dsb     ish
	ret
ENDPROC(hyplet_flush_el2_icache)

ENTRY(hyplet_invld_tlb)
	tlbi vae2, x0
	dsb sy
    tlbi vale2, x0
	dsb sy
	ret
ENDPROC(hyplet_invld_tlb)

ENTRY(hyplet_invld_all_tlb)
	tlbi alle2
	dsb sy
	ret
ENDPROC(hyplet_invld_all_tlb)

/* 
  x0 address
  x1 size
*/
ENTRY(hyplet_clear_cache)

        push   x1, x0
        push   x2, x3

        mrs    x3, ctr_el0
        ubfx   x3, x3,#16,#4  // x3  = DminLine
        mov    x2, #4         // x2=word size
        lsl    x2, x2, x3     // x2 = smallet cache line size

        add     x1, x0, x1    // end
        tlbi    vaae1 ,x0

1:      dc      civac, x0     // clean & invalidate D line / unified line
        ic      ivau, x0
        dsb     ish
        add     x0, x0, x2
        cmp     x0, x1
        b.lo    1b

        pop x2,        x3
        pop x1,        x0
        isb
        ret
ENDPROC(hyplet_clear_cache)

// Called by a brk trap

// x1..4  - arg 1 .. arg4
// x0 = hyplet_id
ENTRY(hyplet_rpc)

	push_registers

	mrs	x5, tpidr_el2

	str	x1,[x5, #HYPLET_ARG1]
	str	x2,[x5, #HYPLET_ARG2]
	str	x3,[x5, #HYPLET_ARG3]
	str	x4,[x5, #HYPLET_ARG4]

// save current stack
	mrs x2, sp_el0
	str	x2,[x5, #HYPLET_SP_EL0]
// check func id
	mrs	x1, tpidr_el2
	bl get_hyplet_addr // get_hyplet_addr(func id, hyp )
	cmp	x0, xzr
	bne 2f
	mov	x0, #(-1)
	msr	cntvoff_el2,x0
	b 3f
/*
* Local hyplet found . prepare el0 stack ,code and arguments
*/
2:
	mrs	x4, tpidr_el2
	ldr	x2, [x4, #HYPLET_STACK]
	msr	sp_el0,x2

	mrs x1, spsel
	and w1,w1,#0xFFFFFFFE
	msr spsel,x1

// prepare to call rpc
	ldr	x30,[ x4, #HYPLET_CODE]
	ldr	x0, [ x4, #HYPLET_ARG1]
	ldr	x1, [ x4, #HYPLET_ARG2]
	ldr	x2, [ x4, #HYPLET_ARG3]
	ldr	x3, [ x4, #HYPLET_ARG4]

	blr x30

// save return value
	msr	cntvoff_el2,x0
//
// reset stack back
	mrs x1, spsel
	orr w1, w1, #0x1
	msr spsel,x1

3:	mrs	x0, elr_el2
	add	x0, x0, #4
	msr	elr_el2,x0

	mrs x4,tpidr_el2
	ldr x3,[x4, #HYPLET_SP_EL0]
	msr sp_el0, x3

	pop_registers

// restore return value
	mrs     x0, cntvoff_el2
	msr 	cntvoff_el2, xzr
	eret
ENDPROC(hyplet_rpc)

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

ENTRY(hyplet_call_hyp)
	hvc	#0
	ret
ENDPROC(hyplet_call_hyp)

/*
 * turn on brk trap
*/
ENTRY(hyplet_trap_on)
	mov	x0, #0x100
	msr mdcr_el2,x0
	ret
ENDPROC(hyplet_trap_on)
/*
 * turn off brk trap
*/
ENTRY(hyplet_trap_off)
	msr mdcr_el2,xzr
	ret
ENDPROC(hyplet_trap_off)

/*
 *	Executing ISR hyplet
*/
ENTRY(hyplet_run_user)
/*
 * User had set an hyplet
 *  prepare to jump to hyplet
*/
	push_registers
/* sp_el0 is the kernel's current */
	mrs	 x9, tpidr_el2

/* sp_el0 holds current, save it */
	mrs  x0, sp_el0
	str	 x0,[x9,#HYPLET_SP_EL0]

	mrs     x1, elr_el2
    str     x1, [x9, #HYPLET_ELR_EL2]

/* store the stack (sp_el2) incase where it gets corrupted */
	mov  x1, sp
	str	 x1,[x9, #HYPLET_SP_EL2]

    mrs x1, spsel
   	str x1, [x9, #HYPLET_SPSEL_EL2]
    mrs x1,spsr_el2
	str	x1, [x9, #HYPLET_SPSR_EL2]


	mov x1, #2
	mrs	 x9, tpidr_el2
	ldr	x2, [x9, #HYPLET_PRINT]
	blr x2
	mov x1, x0 /* stash the next instruction  in x1 */

/*   put it back in the code */
	ldr	x30,[x9, #HYPLET_OPCODE]

/*	flush cache */
	ic      ivau, x30
    dsb     ish

/*
 	generate the program
*/
	mov		x3, #0xd049
	movk	x3, #0xd53c, lsl #16
	lsl		x3, x3, #32

	mrs	 	x9, tpidr_el2
	ldr		x2, [x9, #HYPLET_OPCODE]
	and 	x1, x1, #0xFFFFFFFF
	orr		x0, x3, x1
/*   put it back in the code */
	ldr	x30,[x9, #HYPLET_OPCODE]

/*	flush cache */
	ic      ivau, x30
    dsb     ish
/* store the instruction */
	str	x0,	[x30, #0]
	ldr	x30,[x9, #HYPLET_CODE]
exec_hyp:
	zero_all
	/* zero stack */
	mov sp, x0
	blr 	x30 /* Execute hyplet in EL2, if LR is manipulated by the opcode, then we drop to abort */
/*	the hyplet did not fault */
	mrs	 x9, tpidr_el2
/* restore stack, for cases where the opcode manipulates the sp */
	ldr x1,[x9,#HYPLET_SP_EL2]
	mov	sp, x1

/*	load the instruction address */
	ldr	x3, [x9, #HYPLET_OPCODE]

/*	flush cache */
	ic      ivau, x3
    dsb     ish

/*	record non faulted opcode */
	ldr	x0, [x3, #0]
	mov x1, #1
	ldr	x2, [x9, #HYPLET_PRINT]
	blr x2
/*	if return 1 then shoud stop */
	cbnz x0,3f
/*
 	generate the next program
    generate the "mrs r9,tpidr_el2" opcode */
	mov		x3, #0xd049
	movk	x3, #0xd53c, lsl #16
	lsl		x3,x3, #32

/*	move to next opcode to test */
	ldr		x2, [x9, #HYPLET_OPCODE]
	ldr		x1, [x2, #0]
	and 	x1, x1, #0xFFFFFFFF
	sub		x1, x1, #1
	orr		x0, x3, x1

/*   put it back in the code */
	ldr		x30,[x9, #HYPLET_OPCODE]
	str		x0,	[x30, #0]
	ldr		x30,[x9, #HYPLET_CODE]
/*	go back and execute the new program */
	b exec_hyp

3:
    mrs 	x9 , tpidr_el2
    ldr		x0, [x9, #HYPLET_ELR_EL2]
    msr		elr_el2, x0
	str	 	xzr, [x9, #HYPLET_CODE]

	ldr		x1, [x9, #HYPLET_SPSR_EL2]
    msr	 	spsr_el2,x1

   	ldr 	x1, [x9, #HYPLET_SPSEL_EL2]
    msr		spsel,x1


	/* restore the stack (sp_el2) as we entered */
	ldr	 x1,[x9, #HYPLET_SP_EL2]
	mov  sp,x1

    pop_registers
    pop	lr, xzr
	eret

ENDPROC(hyplet_run_user)

hyplet_abort_isr:
	mrs x9 , tpidr_el2

/* restore stack, just in case */
	ldr x1,[x9,#HYPLET_SP_EL2]
	mov	sp, x1

/* x4 holds the instruction address */
	ldr	x4, [x9, #HYPLET_OPCODE]

/*	load the instruction opcode in x3 */
	ldr	x3, [x4, #0]
	mov x8, x3	/* stash for later use */
/*	flush the cache line */
	ic      ivau, x4
    dsb     ish

/*	generate the next program d53cd049  */
	mov		x2, x3
	mov		x3, #0xd049
	movk	x3, #0xd53c, lsl #16
	lsl		x3, x3, #32

/*	move to next opcode to test */
	and 	x2, x2, #0xFFFFFFFF
	sub		x2, x2, #1
	orr		x0, x3, x2

/* store the new command */
	str	 x0,	[x4, #0]
	ldr	x30,[x9, #HYPLET_CODE]

/* record the command */
	ldr	 x2, [x9, #HYPLET_PRINT]
	mov  x0, x8	 /* */
	mov  x1, xzr /* we had an mmu abort */
	push x3, x4
	blr  x2
	pop x3, x4

	ldr x30,[x9, #HYPLET_CODE]
	zero_all
/* zero the stack */
	mov sp, x0
/* perform the opcode  */
	blr	x30
/*	We're back, this command did not abort, record it. */
	mrs x9 , tpidr_el2
/* restore the stack */
	ldr x1,[x9,#HYPLET_SP_EL2]
	mov	sp,x1

	ldr	x4, [x9, #HYPLET_OPCODE]
	ldr	x0, [x4, #0]
	mov x1, #1 /* type 1 opcode */
	ldr	x2, [x9, #HYPLET_PRINT]
	blr x2

/*	if return 1 then shoud stop */
	cbnz x0,1f
	b hyplet_abort_isr

1: /* return to the kernel */
    mrs 	x0 , tpidr_el2
    ldr		x0, [x0, #HYPLET_ELR_EL2]
    msr		elr_el2, x0

	ldr		x1, [x9, #HYPLET_SPSR_EL2]
    msr 	spsr_el2,x1

   	ldr 	x1, [x9, #HYPLET_SPSEL_EL2]
    msr 	spsel,x1

	mrs		x9, tpidr_el2
	str		x0, [x9,#HYPLET_ARG1]

/* force exit from offline mode */
	str xzr, [x9, #HYPLET_CODE]
 /* restore state */
  //  pop x0,xzr
    pop_registers
    pop	lr, xzr

   eret

ENDPROC(hyplet_abort_isr)
__unknown_log:
	.ascii	"%lx\n\0"

ENTRY(hyplet_on)

	pop	lr, xzr

	push 	x0,x1
	push 	x2,x3

	mov	x3, lr		// save the link register of EL1 before losing it.

	kern_hyp_va  x0	// grab tvm
	msr	tpidr_el2, x0	// save tvm context

// enable MMU
	mov		x0, #1
	lsl     x0, x0, #HCR_RW_SHIFT
	msr		hcr_el2, x0

	msr 	mdcr_el2, xzr

	pop 	x2,x3
	pop 	x0,x1

	eret
ENDPROC(hyplet_on)



.align 11
ENTRY(__hyplet_vectors)
	ventry		hyplet_abort_isr
        invalid_vector  EL2_irq_invalid,#2                 // IRQ EL2t
        invalid_vector  EL2_fiq_invalid,#3                // FIQ EL2t
        invalid_vector  EL2_error_invalid,#4             // Error EL2t

        ventry  hyplet_abort_isr	// Current EL with SPx
        invalid_vector  EL2_irq_invalidELSpx,#6                 // IRQ EL2h
        invalid_vector  EL2_fiq_invalidELSpx,#7                 // FIQ EL2h
        invalid_vector  EL2_error_invalidELspx,#8               // Error EL2h

        ventry		EL1_sync       	// Synchronous 64-bit EL1
        invalid_vector  EL1_64_irq,#14
        invalid_vector  EL1_fiq_invalid, #9                // FIQ 64-bit EL1
        invalid_vector  EL1_error_invalid ,#10             // Error 64-bit EL1

        invalid_vector	EL132_irq,#17  		// Synchronous 32-bit EL1
        invalid_vector  EL1_irq_invalid, #11                 		// IRQ 32-bit EL1
        invalid_vector  EL1_fiq_invalidLowEL32 ,#12               // FIQ 32-bit EL1
        invalid_vector  EL1_error_invalidLowEL32,#13               // Error 32-bit EL1
ENDPROC(__hyplet_vectors)

.popsection
