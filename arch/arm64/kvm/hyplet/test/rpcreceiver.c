#define _GNU_SOURCE

#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>

#include <linux/hyplet_user.h>
#include "hyplet_utils.h"


long t1 = 0;
long t2 = 0;
int run = 1;
/*
 * collect the time stamp from sender
 * and save my own
 */
long user_hyplet(long _t1)
{
	t2 = cycles();
	t1 = _t1;
	return t2;
}


static int hyplet_rpc_start(void)
{
	int rc;
	int stack_size = sysconf(_SC_PAGESIZE) * 2;
	void *stack_addr;
	int heap_sz;
	int func_id = 0x17;
	int func_size = 0x92;

	if (hyplet_map(HYPLET_MAP_HYPLET, user_hyplet, func_size)) {
		fprintf(stderr, "hyplet: Failed to map code\n");
		return -1;
	}

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

	if (hyplet_map(HYPLET_MAP_ANY, &t1, -1)) {
		fprintf(stderr, "hyplet: Failed to map a stack\n");
		return -1;
	}

	rc = hyplet_rpc_set(user_hyplet, func_id);
	if (rc){
		fprintf(stderr, "hyplet: Failed to set rpc\n");
		return -1;
	}

	return 0;
}

int main(int argc, char *argv[])
{
	hyplet_rpc_start();

	while(run){
	//	printf("RECV:%d Waiting..run %d\n",getpid(),run);
		usleep(100000);
	}
	printf("receiver exit: t1 %ld t2 %ld t2-t1=%ld\n",t1,t2, t2-t1);

}
