#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>


int func_id = 8;

static inline  void hyplet_rpc_call(int funcid,int a1,int a2,int a3,int a4)
{
    __asm("mov r0,%0\n"
		: 
		:"r" (funcid)
		);
    __asm("mov r1,%0\n"
		: 
		:"r" (a1)
		);
    __asm("mov r2,%0\n"
		: 
		:"r" (a2)
		);
    __asm("mov r3,%0\n"
		: 
		:"r" (a3)
		);
    __asm("mov r4,%0\n"
		: 
		:"r" (a4)
		);
    __asm("bkpt #3\n":::);
}


/*
 * it is essential affine the program to the same 
 * core where it runs.
*/
int main(int argc, char *argv[])
{
    hyplet_rpc_call(8, 6, 7, 9,111);
}
