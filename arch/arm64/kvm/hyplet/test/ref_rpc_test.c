#include <stdio.h>          /* printf()                 */
#include <stdlib.h>         /* exit(), malloc(), free() */
#include <unistd.h>
#include <sys/types.h>      /* key_t, sem_t, pid_t      */
#include <sys/wait.h>
#include <sys/shm.h>        /* shmat(), IPC_RMID        */
#include <errno.h>          /* errno, ECHILD            */
#include <semaphore.h>      /* sem_open(), sem_destroy(), sem_wait().. */
#include <fcntl.h>          /* O_CREAT, O_EXEC          */

struct hyplet_ctrl;

#include "hyplet_utils.h"

#define ITERS	1000

void null_func(void)
{
}

int main (int argc, char **argv)
{
    int iters = ITERS;		  /*      Number of Iterations    */
    long  min = 100000, max = 0;	  
    long  avg = 0;
    long  tot = 0;
    long  t1, t2;
    long dt;
    int i;                        /*      loop variables          */
    sem_t *sem_parent;            /*      synch semaphore         *//*shared */
    sem_t *sem_child;             /*      synch semaphore         *//*shared */
    pid_t pid;                    /*      fork pid                */
    unsigned int value;           /*      semaphore value         */

    value = 0;
    /* initialize semaphores for shared processes */
    sem_parent = sem_open ("parent", O_CREAT | O_EXCL, 0644, value); 
    sem_child = sem_open ("child", O_CREAT | O_EXCL, 0644, value); 
    /* name of semaphore is "pSem", semaphore is reached using this name */
    sem_unlink ("child");      
    sem_unlink ("parent");      
    /* unlink prevents the semaphore existing forever */
    /* if a crash occurs during the execution         */
    printf ("semaphores initialized.\n\n");

    /* fork child processes */
    pid = fork ();

    sleep(1);

    if (pid != 0){

    	while(--iters > 0) {
		t1 = cycles();
		sem_post (sem_child);           /* V operation */
		sem_wait (sem_parent);           /* P operation */
		t2 = cycles();
		dt  = t2 - t1;
		tot += dt;
		if (dt < min)
			min = dt;
		if (dt > max) 
			max = dt;
	}
	avg = tot/ITERS;
    	printf ("Parent new value of ticks=%ld. %ld,%ld,%ld\n", dt,min,avg,max);
	while (pid = waitpid (-1, NULL, 0)){
            if (errno == ECHILD)
                break;
        }
        printf ("\nParent: All children have exited.\n");
        /* cleanup semaphores */
        printf("sem_destroy return value:%d\n",sem_destroy (sem_parent));
        printf("sem_destroy return value:%d\n",sem_destroy (sem_child));
        exit (0);
    }

    while(--iters > 0) {
	t1 = cycles();
	sem_wait (sem_child); 
	null_func();
       	sem_post (sem_parent); 
	t2 = cycles();
	dt  = t2 - t1;
	tot += dt;
	if (dt < min)
		min = dt;
	if (dt > max) 
		max = dt;
    }

    printf ("Child new value of ticks=%ld.\n", dt);
}
