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

int some_global = 0;

enum { HYPLET_MAP_CODE = 1,
	   HYPLET_MAP_STACK = 2,
	   HYPLET_MAP_ANY = 3,
	   HYPLET_TRAP_IRQ = 4,
	   HYPLET_UNTRAP_IRQ = 5,

};

struct hyplet_map_addr {
	unsigned long addr;
	int size;
};

struct hyplet_irq_action {
	int action;
	int irq;
};

struct hyplet_ctrl {
	int cmd  __attribute__ ((packed));
	union  {
		struct hyplet_map_addr addr;
		int irq;
	}__action  __attribute__ ((packed));
};


int hyplet_ctl(int cmd,struct hyplet_ctrl *hplt)
{
	const int SYSCALL_HYPLET  = 285;
	hplt->cmd = cmd;
	return syscall(SYSCALL_HYPLET,hplt);
}

/*
 * This code is executed in an hyplet context
 * The attribute is only used to caluclated the exact size of the function.
 */
__attribute__((noinline, section("hyplet"))) long user_hyplet(void *opaque)
{
	some_global++;
}

int   main(int argc, char *argv[])
{
	struct hyplet_ctrl hplt;
	int rc;
	int irq;
	int stack_size = sysconf(_SC_PAGESIZE) * 50;
	void *stack_addr;
	int function_size = 4 * 4;

	if (argc <= 1 ){
		printf("hyplet: must supply an irq , "
			"please look in /proc/interrupts \n");
		return -1;
	}
	irq = atoi(argv[1]);
	printf("xxxxx hyplet=0x%x xxxxxxxxx\n",
		(unsigned long)user_hyplet);
		
	/*
	 * map the user hyplet
	 */
	hplt.__action.addr.addr = (unsigned long)user_hyplet;
	hplt.__action.addr.size = function_size; //hyplet_start - hyplet_end;
	rc = hyplet_ctl(HYPLET_MAP_CODE, &hplt);
	if (rc < 0){
		printf("hyplet: Failed to map code\n");
		return -1;
	}

	/*
	 * create a stack
	 */
	rc = posix_memalign(&stack_addr,
			sysconf(_SC_PAGESIZE),
			stack_size);
	if (rc < 0 ){
		perror("hyplet: Failed to allocate a stack\n");
		exit(0);
	}

	memset(stack_addr, 0x00, stack_size);
	printf("xxxxx stk=0x%lx xxxxxxxxx\n",
		stack_addr);

	hplt.__action.addr.addr = (unsigned long)stack_addr;
	hplt.__action.addr.size = stack_size;
	rc = hyplet_ctl(HYPLET_MAP_STACK, &hplt);
	if (rc < 0){
		printf("hyplet: Failed to map stack\n");
		return -1;
	}

	hplt.__action.addr.addr = (unsigned long)&some_global;
	hplt.__action.addr.size = -1;
	rc = hyplet_ctl(HYPLET_MAP_ANY , &hplt);
	if (rc < 0){
		printf("hyplet: Failed to map user's data\n");
		return -1;
	}

	hplt.__action.irq = irq;
	rc = hyplet_ctl( HYPLET_TRAP_IRQ , &hplt);
	if (rc < 0){
		printf("hyplet: Failed assign irq\n");
		return -1;
	}

	sleep(2);
	printf("some global %d\n",some_global);
}

