#define _GNU_SOURCE

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>

#include <linux/hyplet_user.h>
#include "hyplet_utils.h"


int main(int argc,char *argv[])
{
	long min = 1000000, max = 0, tot = 0, avg;
	long t1, rc, t3;
	long dt;
	int  iters = 1000;
	int  func_id = 0x17;
	int  i = 0; 

	// test failure
	rc = hyplet_rpc_call(0x8,0x09);
	if (rc == 0 ){
		printf("False positive \n");
	}else{
		printf("return value %ld: as expected \n",rc);
	}
	for ( ; i < iters; i++){
		t1 = cycles();
		rc = hyplet_rpc_call(func_id, t1);
		t3 = cycles();
		dt = t3 -t1;
		tot += dt;
		if (dt < min)
			min = dt;
		if (dt > max) 
			max = dt;
	}
	avg = tot/iters;
	printf("SENDER: round trip min,avg,max = %ld,%ld,%ld\n",
			min, avg, max);
}
