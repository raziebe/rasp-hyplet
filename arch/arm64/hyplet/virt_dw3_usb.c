#include <linux/module.h>
#include <linux/highmem.h>
#include <linux/hyplet.h>
#include <linux/delay.h>

#include "hyp_mmu.h"
#include "malware_trap.h"



void prepare_special_addresses(struct hyplet_vm *vm)
{
	int i;
	struct hyplet_driver_handler* hyphnd;

	for (i = 0 ;i < FAULT_MAX_HANDLERS; i++){
		hyphnd = &vm->dev_access->hyphnd[i];
		hyphnd->offset = -1;
		hyphnd->action = NULL;
	}
	/*
	 * Now prepare the real actions
	 */
	hyphnd = &vm->dev_access->hyphnd[0];
	hyphnd->offset = 0x10;
	hyphnd->action = dwc3_mmio_abrt_action10;
}


void malware_prep_mmio(char *addr)
{
	memset(addr, 0xeeeeffff, PAGE_SIZE);
	mb();
}

void __hyp_text dwc3_mmio_abrt_action10(struct hyplet_vm *vm, struct hyplet_driver_handler *hyphnd)
{
	unsigned long *p;
	unsigned long el2_mmio_addr = el2_fault_address() | HYP_PAGE_OFFSET_LOW_MASK;

	p = (unsigned long *)(el2_mmio_addr + hyphnd->offset);
	if ( (*p & 0x1) )
		(	*p) &= ~(0x1);
		else
			(*p) |= 0x1;

}
