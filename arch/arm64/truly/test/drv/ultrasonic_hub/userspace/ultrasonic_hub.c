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

static int usonic_mode  = USONIC_ECHO;
static int iter = 1;
static int bit1_delay = 0;
static int bit0_delay = 0;
static int cpu = 1;
static int run = 1;
static int state = 0;
static int bit = 0;
static long echo_start_ns = 0;
static long echo_end_ns = 0;
float supersonic_speed_us = 0.0343;// centimeter/microsecond;	


long usonic_trig(long time_ns)
{
	long end = 0;

	if (state == USONIC_TRIG_START){
		char cmd = USONIC_TRIG;
		short cmd_val = ((short)1 << 8) ;
		long rc = cmd_val | cmd;

		// Start bit transmit
		state = USONIC_TRIG_END; 
		return rc;
	}

	if (state == USONIC_TRIG_END){
		//  End the transmit, according to bit value
		if (bit == 0)
			end = time_ns + bit0_delay;
		if (bit == 1)
			end = time_ns + bit1_delay;

		state = USONIC_TRIG_START; 
		while (hyp_gettime() < end && run);
		return  USONIC_TRIG; // End Bit transmit
	}
	hyp_print("usonic_trig: should not be here\n");
	return -1;
}

long usonic_echo(long time_ns)
{
	long end = 0;

echo_start:
	if (!run)
		return 0;

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
		float distance;
		long dt;
		// Save the time of transition from 1 to 0
		echo_end_ns = time_ns;
		dt = (echo_end_ns - echo_start_ns)/1000;
		distance  = ((float)dt * supersonic_speed_us)/2;

		hyp_print("#%d us = %ld distance=%2.2f bit=%d\n", 
			iter++, dt, bit);
	//	bit = !bit; /* 1010101...*/
		state = USONIC_ECHO_START;
		goto echo_start;
	}

	hyp_print("echo should not be here\n");
	return -1;
}

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
long  usonic_transducer(long time_ns,long a2,long a3,long a4)
{
	switch(usonic_mode)
	{
		case USONIC_TRIG:
			return usonic_trig(time_ns);
		case USONIC_ECHO:
			return usonic_echo(time_ns);
	}
	hyp_print("Usonic ilegal mode\n");
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

	if (hyplet_assign_offlet(cpu, usonic_transducer)) {
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
    char *mode;

    if (argc < 4){
        printf("%s <cpu> <bit delay us> <mode=[TRIG=1,ECHO=2]>\n",
			argv[0]);
        return -1;
    }

    cpu = atoi(argv[1]);
    bit0_delay = atoi(argv[2]);
   // trig_intergap_ns = atoi(argv[3])*1000;
    mode = argv[3];

    if (!strcasecmp(mode,"trig"))
	usonic_mode = USONIC_TRIG;    

    if (!strcasecmp(mode,"echo"))
	usonic_mode = USONIC_ECHO;    
 
    if (usonic_mode != USONIC_TRIG && usonic_mode != USONIC_ECHO){
	printf("Ilegal mode\n");
	return -1;
    }

    if ( usonic_mode == USONIC_TRIG ) {
	    if (bit0_delay < 0) {
		printf("must provide a sane bit delay");
		return 0;
	    }
	bit0_delay *= 1000; // to nanosecond
	bit1_delay = bit0_delay;
	state = USONIC_TRIG_START;
    } else{
	state = USONIC_ECHO_START;
    }

    if (hyplet_drop_cpu(cpu) < 0 ){
	printf("Failed to drop processor\n");
	return -1;
    }

    printf("Cpu %d delay %d,%d\n",
		cpu, bit1_delay, bit0_delay);
    hyplet_start();
 
    while (1) {
	    print_hyp();
	    usleep(10000);
    }
    run  = 0;
    sleep(1);
}

