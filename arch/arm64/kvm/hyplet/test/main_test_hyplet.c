
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>

#include "test_hyplet.h"
#include "user_hyplet.h"

extern long dt_max,
	dt_min,
	dt_zeros;
extern int loops;
extern int count;
extern int* hist;

int main(int argc, char *argv[])
{
	int i;
	int rc;

	rc = take_options(argc, argv);
	if (rc < 0){
		return -1;
	}

	hyplet_start(HYPLET_SIZE);

	while (1) {
		sleep(1);
		if (count >= loops)
			break;
	}

	for (i =0 ; i < HIST_SIZE; i++)
		if (hist[i] != 0)
			printf("hist[%d] = %ld\n",i,hist[i]);
}
