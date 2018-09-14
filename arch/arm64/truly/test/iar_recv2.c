#define _GNU_SOURCE
 
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>

#include <linux/hyplet_user.h>
#include "hyplet_utils.h"

int interval_ns = 100000;
static int cpu = 1;
static int sample  = 0;
static long next_ns = 0;
#define SAMPLES_NR	10

long times[SAMPLES_NR][2]; 
/*
 * read the value from the offlet.	
*/
long user_print(long a1,long a2,long a3,long a4)
{
	if (next_ns == 0) {
		next_ns = hyp_gettime() + interval_ns;
		return 0;
	}

	hyp_print("array [%ld,%ld = %ld]\n",
		 a1, a2, a2 - a1);

	while (hyp_gettime()  < next_ns);

	next_ns +=  interval_ns;

	if (sample > SAMPLES_NR )
		return 0;
	times[sample][0]   = a1;
	times[sample++][1] = a2;
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
	
	if (hyplet_set_stack(stack_addr, stack_size, cpu)) {
		fprintf(stderr, "hyplet: Failed to map a stack\n");
		return -1;
	}

	if (hyplet_assign_offlet(cpu, user_print)) {
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
    int i;
    int rc;

    if (argc <= 1){
        printf("%s <cpu>\n",argv[0]);
        return -1;
    }
   
    cpu = atoi(argv[1]);
    if (hyplet_drop_cpu(cpu) < 0 ){
		printf("Failed to drop processor\n");
		return -1;
    }

    hyplet_start();
    printf("Waiting for offlet %d for 100 useconds\n",cpu);

    
    for (;i < 10; i++) {
	print_hyp();
    	sleep(1);
    }
}


