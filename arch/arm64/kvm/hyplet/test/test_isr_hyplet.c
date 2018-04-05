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


u64 prev_ts = 0;
u64 next_ts = 0;
int irq = 0;
int loops = 10000;
int count = 0;
int _count = 0;
int dropped = 0;
long* hist = NULL;
long* hist_neg = NULL;
int hist_size	= 10000;
int offset_ns = 0;

#define abs(x) ((x)<0 ? -(x) : (x))
#define TICK_US 1000LL
#define TICK_NS 1000000LL // tick might be early

/*
 * Implemented as timer0 hyplet
*/
long isr_user_hyplet(void)
{
	long long times_offset= 0;
	s64 dt = 0;
	s64 ts = 0;

	ts = cycles_to_ns();
	if (ts < next_ts)
		return 0;

	next_ts = ts + TICK_NS - offset_ns;
	if (prev_ts != 0) {
		dt = ts - prev_ts;
		prev_ts = ts;
	} else{
		prev_ts = ts;
		return 0;
	}
/* calc histogram  */
	times_offset = (dt - TICK_NS)/1000;
	if (times_offset >= 0){
		if (times_offset < hist_size)
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
	return 0;
}

int take_options(int argc, char *argv[])
{
	int opt;

	while ((opt = getopt(argc, argv, "l:o:i:a")) != -1) {
		switch (opt) {
		case 'i':
			irq = atoi(optarg);
			break;
		case 'l':
			loops = atoi(optarg);
			break;
		case 'a':
			irq =  HYPLET_IMP_TIMER;
			break;
		case 'o':
			offset_ns = atoi(optarg);
			break;
		default:	/* '?' */
			fprintf(stderr,
				"Usage: %s [-l loops ] -i <irq> -a[do timer]\n",
				argv[0]);
			exit(EXIT_FAILURE);
		}
	}
	if (irq == 0)
		return -1;
}


int hyplet_isr_start(void)
{
	int rc;
	int stack_size = sysconf(_SC_PAGESIZE) * 50;
	void *stack_addr;
	int heap_sz;

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

	printf("TICK_NS %lld at %lld offset %lld\n",
		TICK_NS,cycles_to_ns(), offset_ns);
// create the heap
	heap_sz = sizeof(long) * hist_size;
	hist = malloc(heap_sz);
	memset(hist, 0x00, heap_sz);

	hist_neg = malloc(heap_sz);
	memset(hist_neg, 0x00, heap_sz);

	if (hyplet_map_all()) { // map all possible vmas
		fprintf(stderr, "hyplet: Failed to map a vmas\n");
		return -1;
	}

	if (hyplet_set_stack((unsigned long)stack_addr, stack_size)) {
		fprintf(stderr, "hyplet: Failed to map a stack\n");
		return -1;
	}

	if (hyplet_set_callback(isr_user_hyplet) ){
		fprintf(stderr, "hyplet: Failed to map code\n");
		return -1;
	}

	if (irq  ==  HYPLET_IMP_TIMER) {
		if (hyplet_trap_all_irqs()) {
			printf("hyplet: Failed to set timer trap\n");
			return -1;
		}
		return 0;
	}

	if (hyplet_trap_irq(irq)) {
		printf("hyplet: Failed to trap irq %d\n",irq);
		return -1;
	}
	return 0;
}

