
static inline  void hyp_rpc_call(int a1,int a2,int a3,int a4)
{
    __asm("mov r0,%0\n"
		: 
		:"r" (a1)
		);

    __asm("mov r1,%0\n"
		: 
		:"r" (a2)
		);

    __asm("mov r2,%0\n"
		: 
		:"r" (a3)
		);

    __asm("mov r3,%0\n"
		: 
		:"r" (a4)
		);

    __asm("bkpt #3\n":::);
}



void main()
{
        printf("Send 101\n");
        hyp_rpc_call(101, -1, -1, -1);
}
