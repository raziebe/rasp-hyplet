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


unsigned long long prev_ts = 0;
unsigned long long next_ts = 0;
int irq = 0;
int loops = 10000;
int count = 0;
int _count = 0;
int dropped = 0;
long* hist = NULL;
long* hist_neg = NULL;
int hist_size	= 10000;

#define abs(x) ((x)<0 ? -(x) : (x))
#define TICK_US 1000LL
#define TICK_NS 998000LL // tick might be early

/*
 * Implemented as timer0 hyplet
*/
long user_hyplet(void)
{
	long long times_offset= 0;
	s64 dt = 0;
	s64 ts = 0;

	ts = cntvoffel2();
	if (ts < next_ts){
		return 0;
	}
	if (prev_ts != 0) {
		dt = ts - prev_ts;
		prev_ts = ts;
		next_ts = ts + TICK_NS;
	} else{
		prev_ts = ts;
		next_ts = ts + TICK_NS;
		return 0;
	}
/* calc histogram  */
	times_offset = (dt - TICK_NS)/1000;
	if (times_offset >= 0){
		if (times_offset < hist_size )
			hist[times_offset]++;
		else 
			dropped++;
		goto hyplet_out;
	} else{
		times_offset=abs(times_offset);
		if (times_offset < hist_size)
			hist_neg[times_offset]++;
		else 
			dropped++;
	}
hyplet_out:
	count++;
	if (times_offset <= 1)
		next_ts = ts + TICK_NS;
	return 0;
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

