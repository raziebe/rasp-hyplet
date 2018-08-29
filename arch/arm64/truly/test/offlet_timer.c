#define _GNU_SOURCE
 
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>

#include <linux/hyplet_user.h>
#include "hyplet_utils.h"

int cpu = 0;
int interval_ns = 100000;
int iters  = 0;
unsigned long next = 0;
/*
 * This code is executed in an hyplet context
 */
long user_timer(void *opaque)
{
	if (next==0)
		next = __ns();
	while (__ns()  < next);
	next +=  interval_ns;
/*
	Put whatever you want here
*/
	iters++;
	return 0;
}


static int hyplet_start(void)
{
	int rc;
	int stack_size = sysconf(_SC_PAGESIZE) * 50;
	void *stack_addr;
	/*
	 * Create a stack
	 */
	rc = posix_memalign(&stack_addr,
			    sysconf(_SC_PAGESIZE), stack_size);
	if (rc < 0) {
		fprintf(stderr, "hyplet: Failed to allocate a stack\n");
		return -1;
	}
// must fault it
	memset(stack_addr, 0x00, stack_size);
	if (hyplet_map_all(cpu)) {
		fprintf(stderr, "hyplet: Failed to map a stack\n");
		return -1;
	}

	if (hyplet_set_stack((long)stack_addr, stack_size, cpu)) {
		fprintf(stderr, "hyplet: Failed to map a stack\n");
		return -1;
	}

	if (hyplet_assign_offlet(cpu, user_timer)) {
		fprintf(stderr, "hyplet: Failed to map code\n");
		return -1;
	}

	return 0;
}

/*
 * it is essential affine the program to the same 
 * core where it runs.
*/
int main(int argc, char *argv[])
{
    int rc;

    if (argc <= 2){
        printf("%s <cpu> <interval ns> ",argv[0]);
        return -1;
    }

    cpu = atoi(argv[1]);
    interval_ns = atoi(argv[2]);

    printf("setting offlet to cpu %d interval %d\n", cpu, interval_ns);
    hyplet_start();
    printf("Waiting for offlet %d for 5 seconds\n",cpu);
    sleep(5);
    printf("cycles %d\n", iters);
}
