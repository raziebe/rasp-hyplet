/*

 * user_hyplet.c
 *
 *  Created on: Jan 22, 2018
 *      Author: raz
 *
 *      This is an example of a user hyplet
 */

#define _GNU_SOURCE
 
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>

#include "linux/hyplet_user.h"
#include "hyplet_utils.h"


int irq = 0;
unsigned long some_global = 0;

/*
 * This code is executed in an hyplet context
 * The attribute is only used to caluclated the exact size of the function.
 */
__attribute__((noinline, section("hyplet"))) long user_hyplet(void *opaque)
{
	some_global = cntvoffel2();
}


int hyplet_start(void)
{
	int rc;
	int stack_size = sysconf(_SC_PAGESIZE) * 50;
	void *stack_addr;
	int heap_sz;
	int func_size = 4 * 4;

	if (hyplet_map(HYPLET_MAP_CODE, user_hyplet, func_size)) {
		fprintf(stderr, "hyplet: Failed to map code\n");
		return -1;
	}
	/*
	 * create a stack
	 */
	rc = posix_memalign(&stack_addr,
			    sysconf(_SC_PAGESIZE), stack_size);
	if (rc < 0) {
		fprintf(stderr, "hyplet: Failed to allocate a stack\n");
		return -1;
	}

	memset(stack_addr, 0x00, stack_size);
	if (hyplet_map(HYPLET_MAP_STACK, stack_addr, stack_size)) {
		fprintf(stderr, "hyplet: Failed to map a stack\n");
		return -1;
	}

	if (hyplet_map(HYPLET_MAP_ANY, &some_global, 8)) {
		fprintf(stderr, "hyplet: Failed to map a stack\n");
		return -1;
	}

	if (hyplet_trap_irq(irq)) {
		printf("hyplet: Failed to map user's data\n");
		return -1;
	}
	return 0;
}

int main(int argc, char *argv[])
{
	int rc;

	printf("size of hyplet_ctrl %d\n",sizeof(struct hyplet_ctrl));
        if (argc <= 1 ){
		struct hyplet_ctrl hyp;
                printf("hyplet: must supply an irq , "
                        "please look in /proc/interrupts \n");
		hyplet_ctl( HYPLET_DUMP_HWIRQ , &hyp );
                return -1;
        }
        irq = atoi(argv[1]);
	hyplet_start();
	while(1) {
		usleep(10000);
		printf("%ld us\n",some_global/1000 );
	}
}
