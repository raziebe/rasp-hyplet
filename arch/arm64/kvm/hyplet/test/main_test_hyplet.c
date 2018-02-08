
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>

#include "test_hyplet.h"
#include <linux/hyplet_user.h>

extern long 	dt_max,
		dt_min,
		dt_zeros;
extern int loops;
extern int count;
extern long* hist;
extern long* hist_neg;
extern int hist_size;
extern int dropped;

int main(int argc, char *argv[])
{
	struct timeval tv1,tv2;
	int tot =0;
	int i = 0;
	int rc = 0;
	int dt_us = 0;

	rc = take_options(argc, argv);
	if (rc < 0){
		return -1;
	}

	printf("PI3 test configuration: LPJ is 19.z Mhz , ie; 52ns a tick\n");

	hyplet_start(HYPLET_SIZE);
	gettimeofday(&tv1, NULL);

	while (1) {
		if (count >= loops)
			break;
		usleep(1000);
	}
	for (i =0 ; i < hist_size; i++) {
		if (hist[i] != 0) {
			printf("hist[%d us] = %lld samples\n",i, hist[i]);
			tot+= hist[i];
		}
	}
	for (i =0 ; i < hist_size; i++) {
		if (hist_neg[i] != 0){
			printf("hist_neg[%d us] = %lld samples\n",i , hist_neg[i]);
			tot+=hist_neg[i];
		}
	}
	gettimeofday(&tv2, NULL);
	dt_us = (tv2.tv_sec -tv1.tv_sec)*1000000 + (tv2.tv_usec - tv1.tv_usec); 
	printf("dropped %d count=%d duration %d us\n",
			dropped,tot, dt_us);
}
