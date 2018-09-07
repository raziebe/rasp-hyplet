/*
 * Copyright (C) 2012 - TrulyProtect Jayvaskula University Findland
 * Author: Raz
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License, version 2, as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */

#include <linux/init.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/arm-cci.h>
#include <linux/errno.h>
#include <linux/err.h>
#include <linux/list.h>
#include <linux/module.h>
#include <linux/vmalloc.h>
#include <linux/fs.h>

#include <linux/sched.h>
#include <asm/tlbflush.h>

#include <asm/virt.h>
#include <asm/sections.h>

#include <linux/hyplet.h>
#include <linux/truly.h>
#include "hyp_mmu.h"


static DEFINE_PER_CPU(unsigned long, tp_arm_hyp_stack_page);

static inline void __cpu_init_hyp_mode(phys_addr_t boot_pgd_ptr,
                                       phys_addr_t pgd_ptr,
                                       unsigned long hyp_stack_ptr,
                                       unsigned long vector_ptr)
{
	hyplet_call_hyp((void *)boot_pgd_ptr, (void*)pgd_ptr, (void*)hyp_stack_ptr, vector_ptr);
}

unsigned long get_hyp_vector(void)
{
	return (unsigned long)__hyplet_vectors;
}

static void cpu_init_hyp_mode(void *discard)
{
	phys_addr_t pgd_ptr;
	phys_addr_t boot_pgd_ptr;
	unsigned long hyp_stack_ptr;
	unsigned long stack_page;
	unsigned long vector_ptr;

	/* Switch from the HYP stub to our own HYP init vector */
	__hyp_set_vectors(tp_get_idmap_vector());

	pgd_ptr = tp_mmu_get_httbr();
	stack_page = __this_cpu_read(tp_arm_hyp_stack_page);
	boot_pgd_ptr = tp_mmu_get_boot_httbr();
	hyp_stack_ptr = stack_page + PAGE_SIZE;
	vector_ptr = get_hyp_vector();
	printk("assign truly vector %lx\n",hyp_stack_ptr);
	__cpu_init_hyp_mode(boot_pgd_ptr, pgd_ptr, hyp_stack_ptr, vector_ptr);
	hyplet_setup();
}

static int init_subsystems(void)
{
	on_each_cpu(cpu_init_hyp_mode, NULL,1);
	return 0;
}

/**
 * Inits Hyp-mode on all online CPUs
 */
static int init_hyp_mode(void)
{
	int cpu;
	int err = 0;
	/*
	 * Allocate Hyp PGD and setup Hyp identity mapping
	 */
	err = tp_mmu_init();
	if (err)
		goto out_err;

	/*
	 * Allocate stack pages for Hypervisor-mode
	 */
	for_each_possible_cpu(cpu) {
		unsigned long stack_page;

		stack_page = __get_free_page(GFP_KERNEL);
		if (!stack_page) {
			err = -ENOMEM;
			goto out_err;
		}

		per_cpu(tp_arm_hyp_stack_page, cpu) = stack_page;
	}
	/*
	 * Map the Hyp-code called directly from the host
	 */
	err = create_hyp_mappings(__hyp_text_start, __hyp_text_end, PAGE_HYP_EXEC);
	if (err) {
		printk("Cannot map world-switch code\n");
		goto out_err;
	}

	err = create_hyp_mappings(__hyp_idmap_text_start, 
			 __hyp_idmap_text_end, PAGE_HYP_EXEC);
	if (err) {
            printk("Cannot map world-switch code\n");
            return -1;
	}

	err = create_hyp_mappings(__bss_start, __bss_stop, PAGE_HYP);
	if (err) {
		printk("Cannot map bss section\n");
		goto out_err;
	}

	/*
	 * Map the Hyp stack pages
	 */
	for_each_possible_cpu(cpu) {
		char *stack_page = (char *)per_cpu(tp_arm_hyp_stack_page, cpu);
		err = create_hyp_mappings(stack_page, stack_page + PAGE_SIZE, PAGE_HYP);
		if (err) {
			printk("Cannot map hyp stack\n");
			goto out_err;
		}
	}

	printk("Hyp mode initialized successfully\n");
	return 0;

out_err:
	printk("error initializing Hyp mode: %d\n", err);
	return err;
}

#if 0 // older kernels
int __attribute_const__ tp_target_cpu(void)
{
	switch (read_cpuid_part()) {
	case ARM_CPU_PART_CORTEX_A7:
		return TP_ARM_TARGET_CORTEX_A7;
	case ARM_CPU_PART_CORTEX_A15:
		return TP_ARM_TARGET_CORTEX_A15;
	default:
		return -EINVAL;
	}
}

#else

#define ARM_TARGET_GENERIC_V8 1

int __attribute_const__ tp_target_cpu(void)
{
        unsigned long implementor = read_cpuid_implementor();
        unsigned long part_number = read_cpuid_part_number();

        switch (implementor) {
        	case ARM_CPU_IMP_ARM:
                switch (part_number) {
                case ARM_CPU_PART_AEM_V8:
                        return ARM_CPU_PART_AEM_V8;
                case ARM_CPU_PART_FOUNDATION:
                		return ARM_CPU_PART_FOUNDATION;
                case ARM_CPU_PART_CORTEX_A53:
                        return ARM_CPU_PART_CORTEX_A53;
                case ARM_CPU_PART_CORTEX_A57:
                        return ARM_CPU_PART_CORTEX_A57;
                }
                break;
            case ARM_CPU_IMP_APM:
                        switch (part_number) {
                        case APM_CPU_PART_POTENZA:
                                return APM_CPU_PART_POTENZA;
                        };
                        break;
         };
        /* Return a default generic target */
        return ARM_TARGET_GENERIC_V8;
}

#endif

static void check_tp_target_cpu(void *ret)
{
	*(int *)ret = tp_target_cpu();
}

/**
 * Initialize Hyp-mode and memory mappings on all CPUs.
 */
static int tp_arch_init(void)
{
	int err;
	int ret, cpu;

	if (!is_hyp_mode_available()) {
		printk("HYP mode not available\n");
		return -ENODEV;
	}

	for_each_online_cpu(cpu) {
		smp_call_function_single(cpu, check_tp_target_cpu, &ret, 1);
		if (ret < 0) {
			printk("Error, CPU %d not supported!\n", cpu);
			return -ENODEV;
		}
	}

	printk("HYP mode is available rc-22\n");
	err = init_hyp_mode();
	if (err)
		return -1;

	err = hyplet_init();
	if (err)
		return err;

	err = init_subsystems();
	if (err)
		return -1;

	return 0;
}


static int truly_boot_start(void)
{
	int rc = 0;
	rc = tp_arch_init();
	return rc;
}

module_init(truly_boot_start);
