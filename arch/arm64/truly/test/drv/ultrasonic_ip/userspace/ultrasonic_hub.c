#define _GNU_SOURCE
 
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <linux/hyplet_user.h>

#include "hyplet_utils.h"

#define USONIC_TRIG	19
#define USONIC_ECHO	29

// States
#define  USONIC_ECHO_START	4
#define  USONIC_ECHO_END	5
#define  USONIC_TRIG_START	6
#define  USONIC_TRIG_END	7
#define  USONIC_BIT_DONE	8

#define  DELAY_1_BIT 20000
#define  DELAY_0_BIT 10000


static int cpu = 1;
static int run = 1;
static int state = USONIC_TRIG_START;
static int bit = 0;
static long echo_start_ns = 0;
static long echo_end_ns = 0;

/*
    Return value is broken to:
	long  cmd:8;  USONIC_ECHO/USONIC_TRIG
	long  cmd_val:8; // echo/trig 1 or 0
	long  pad:48;	
*/


/* Local Send/Receive Tester
 * The kerne offlet act upton our command.
 * It may trigger an echo or end an echo.
 * It may start an echo read.
*/
long user_print(long time_ns,long a2,long a3,long a4)
{
bit_start:	
	if (!run){
		return 0;
	}

	if (state == USONIC_TRIG_START){
		char cmd = USONIC_TRIG;
		short cmd_val = ((short)1 << 8) ;
		long rc = cmd_val | cmd;

		// Start bit transmit
		state = USONIC_TRIG_END; 
		
		return rc;
	}

	if (state == USONIC_TRIG_END){
		long end = 0;
		//  End the transmit, according to bit value
		if (bit == 0)
			end = time_ns + DELAY_0_BIT;
		if (bit == 1)
			end = time_ns + DELAY_1_BIT;

		state = USONIC_ECHO_START; 
		while (hyp_gettime() < end && run);
		return  USONIC_TRIG ; // End Bit transmit
	}

	if (state == USONIC_ECHO_START){
		state = USONIC_ECHO_END;
		// wait untill echo changes to ECHO 1
		return  USONIC_ECHO;
	}

	if (state == USONIC_ECHO_END){
		char cmd = USONIC_ECHO;
		short cmd_val = ((short)1 << 8) ;
		// Save the time of transition from 0 to 1
		echo_start_ns = time_ns;
		// wait until echo would reset back to 0, bug : wait forever ?
		long rc = cmd_val | cmd;
		state = USONIC_BIT_DONE;

		return rc;
	}

	if (state == USONIC_BIT_DONE) {
		// Save the time of transition from 1 to 0
		echo_end_ns = time_ns;
		hyp_print("dt us = %ld bit=%d\n", (echo_end_ns - echo_start_ns)/1000, bit);
	//	bit = !bit; /* 1010101...*/
		state = USONIC_TRIG_START;
		goto bit_start;
	}

	return -1;
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
    
    while (1) {
	    print_hyp();
	    usleep(10000);
    }
//	printf("#%d: dt=%ld \n",
//		sample, (times[i][1] - times[i][0])/1000);
    run  = 0;
    sleep(1);
}

