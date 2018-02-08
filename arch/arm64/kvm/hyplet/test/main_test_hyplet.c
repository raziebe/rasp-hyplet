
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
extern int* hist;
extern int hist_size;

int main(int argc, char *argv[])
{
	int i;
	int rc;

	rc = take_options(argc, argv);
	if (rc < 0){
		return -1;
	}

	printf("PI3 test configuration: LPJ is 19.z Mhz , ie; 52ns a tick\n");

	hyplet_start(HYPLET_SIZE);

	while (1) {
		sleep(1);
		if (count >= loops)
			break;
	}

	for (i =0 ; i < hist_size; i++) {
		if (hist[i] != 0)
			printf("hist[%d ns] = %ld samples\n",i *52, hist[i]);
	}
}
