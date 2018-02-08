#define _GNU_SOURCE

#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <sys/time.h>

#include <linux/hyplet_user.h>
#include "hyplet_utils.h"


unsigned long prev_ts = 0;
unsigned long next_ts = 0;
int irq = 0;
int loops = 10000;
int count = 0;
int dropped = 0;
long* hist = NULL;
long* hist_neg = NULL;
int hist_size	= 10000;

#define TICK_NS 1000000

long user_hyplet(void)
{
	int times_offset= 0;
	unsigned long dt = 0;
	unsigned long ts = 0;

	ts = cntvoffel2()/1000;
	prev_ts = ts;

/* PI3 tends to generate too many interrupts */
	if (ts < next_ts)
		return;
/* stash the next iteration */
	next_ts = ts + 1000;
/* calc */
	if (count < hist_size)
		hist[count++] =  ts;
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

	if (hyplet_map(HYPLET_MAP_ANY, &prev_ts, -1)) {
		fprintf(stderr, "hyplet: Failed to map a stack\n");
		return -1;
	}

	heap_sz = sizeof(long) * hist_size;
	hist = malloc(heap_sz);
	memset(hist, 0x00, heap_sz);
	if (hyplet_map(HYPLET_MAP_ANY, hist, heap_sz)) {
		fprintf(stderr, "hyplet: Failed to map a heap\n");
		return -1;
	}

	hist_neg = malloc(heap_sz);
	memset(hist_neg, 0x00, heap_sz);
	if (hyplet_map(HYPLET_MAP_ANY, hist_neg, heap_sz)) {
		fprintf(stderr, "hyplet: Failed to map a heap\n");
		return -1;
	}
	/*
	if (hyplet_map(HYPLET_MAP_CODE, gettimeofday, 4096)) {
		fprintf(stderr, "hyplet: Failed to map a heap\n");
		return -1;
	}
*/
	if (hyplet_trap_irq(irq)) {
		printf("hyplet: Failed to map user's data\n");
		return -1;
	}

}

