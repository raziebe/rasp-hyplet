/*

 * test_hyplet.c
 *
 *  Created on: Jan 22, 2018
 *      Author: raz
 *
 *      This code is a comparision tool between hyplets to user space that 
 *	on a hardware timer
 */

#define _GNU_SOURCE

#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>

#include "user_hyplet.h"

int irq = 0;
int some_global = 0;
int loops = 10000;
int count = 0;
long *times;

long user_hyplet(void *opaque)
{
	if (count < loops)
		times[count++] = cycles();
}

int take_options(int argc, char *argv[])
{
	int opt;

	while ((opt = getopt(argc, argv, "l:i:")) != -1) {
		switch (opt) {
		case 'i':
			irq = atoi(optarg);
			break;
		case 'l':
			loops = atoi(optarg);
			break;
		default:	/* '?' */
			fprintf(stderr,
				"Usage: %s [-l loops ] -i <irq> \n",
				argv[0]);
			exit(EXIT_FAILURE);
		}
	}
	if (irq == 0)
		return -1;
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

	if (hyplet_map(HYPLET_MAP_ANY, &some_global, -1)) {
		fprintf(stderr, "hyplet: Failed to map a stack\n");
		return -1;
	}

	heap_sz = sizeof(long) * loops;
	times = malloc(heap_sz);
	memset(times, 0x00, heap_sz);
	if (hyplet_map(HYPLET_MAP_ANY, times, heap_sz)) {
		fprintf(stderr, "hyplet: Failed to map the heap\n");
		return -1;
	}

	if (hyplet_trap_irq(irq)) {
		printf("hyplet: Failed to map user's data\n");
		return -1;
	}

}

int main(int argc, char *argv[])
{
	int rc;

	rc = take_options(argc, argv);
	if (rc < 0){
		return -1;
	}

	hyplet_start();

	while (1) {
		sleep(1);
		if (count >= loops)
			break;
		printf("count %d loops %d\n",count,loops);
	}
}
