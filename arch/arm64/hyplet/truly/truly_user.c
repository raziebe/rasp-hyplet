#include <linux/module.h>
#include <linux/truly.h>
#include <linux/linkage.h>
#include <linux/init.h>
#include <linux/gfp.h>
#include <linux/highmem.h>
#include <linux/compiler.h>
#include <linux/linkage.h>
#include <linux/linkage.h>
#include <linux/init.h>
#include <asm/sections.h>
#include <linux/proc_fs.h>
#include <linux/slab.h>
#include <asm/page.h>
#include <linux/truly.h>
#include <linux/delay.h>
#include "Aes.h"
#include "ImageFile.h"

DECLARE_PER_CPU(struct truly_vm, TVM);



int __hyp_text truly_is_protected(struct truly_vm *tv)
{
	if (tv == NULL)
		tv = get_tvm();
	return tv->protected_pgd == truly_get_ttbr0_el1();
}


int __hyp_text truly_pad(struct truly_vm *tv)
{
	char fault_cmd[4];
	int line = 0,lines = 0;
	unsigned char *pad;
	struct encrypt_tvm *enc;
	long t1;

	t1 = cycles();
	enc = (struct encrypt_tvm *) KERN_TO_HYP(tv->enc);

	pad = enc->seg[0].pad_data;
	lines = enc->seg[0].size / 4;

	/*
	* if the fault was in the padded function
	* we must put back the the command that generated the fault
	* ( for example svc ) and re commence it.
	*/
	if (tv->save_cmd < (unsigned long)pad){
		tv->save_cmd = 0;
	}

	if (tv->save_cmd > ( (unsigned long)pad + enc->seg[0].size) ){
		tv->save_cmd = 0;
	}

	if (tv->save_cmd != 0)
		tp_hyp_memcpy(fault_cmd, (char *)tv->save_cmd, sizeof(fault_cmd));

	for (line = 0 ; line < lines ; line++ ) {
		int* p = (int*)&pad[4*line];
		*p  = 0xd4200060;
	}
	tv->pad_time = cycles() - t1;
	if (!tv->save_cmd)
		return 0xA;
	//
	// Must put back the old command
	//
	tp_hyp_memcpy((char *)tv->save_cmd, fault_cmd, sizeof(fault_cmd));

	return 0xB;
}
