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
int loops = 10000;
int count = 0;
long prev_tick = 0;
long dt_max = 0;
long dt_min = 100000;
long dt_zeros = 0;

#define DT_HIKEY 1200

long user_hyplet(void *opaque)
{
	long dt;
	long curtick = cycles();

	if (prev_tick != 0){
		dt = curtick - prev_tick;		
	}
	prev_tick = curtick;
	if (dt_max < dt)
		dt_max  = dt;
	if (dt_min >  dt)
		dt_min = dt;
	if (dt == DT_HIKEY)
		dt_zeros++;
	count++;
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


int hyplet_start(int hyplet_code_size)
{
	int rc;
	int stack_size = sysconf(_SC_PAGESIZE) * 50;
	void *stack_addr;
	int heap_sz;

	if (hyplet_map(HYPLET_MAP_CODE, user_hyplet, hyplet_code_size)) {
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

	if (hyplet_map(HYPLET_MAP_ANY, &prev_tick, -1)) {
		fprintf(stderr, "hyplet: Failed to map a stack\n");
		return -1;
	}

	if (hyplet_trap_irq(irq)) {
		printf("hyplet: Failed to map user's data\n");
		return -1;
	}

}

