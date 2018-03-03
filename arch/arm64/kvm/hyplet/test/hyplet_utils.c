#define _GNU_SOURCE
 
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>

#include <linux/hyplet_user.h>

const int SYSCALL_HYPLET  = 285;

int hyplet_ctl(int cmd,struct hyplet_ctrl *hplt)
{
	hplt->cmd = cmd;
	return syscall(SYSCALL_HYPLET,hplt);
}


int hyplet_map(int cmd, unsigned long addr,int size)
{
	int rc;
	struct hyplet_ctrl hplt;

	hplt.__action.addr.addr = addr;
	hplt.__action.addr.size = size;
	rc = hyplet_ctl(cmd, &hplt);
	if (rc < 0){
		printf("hyplet: Failed to map code\n");
		return -1;
	}
	return 0;
}


int hyplet_set_callback(void *addr)
{
	int rc;
	struct hyplet_ctrl hplt;

	hplt.__action.addr.addr = (unsigned long)addr;

	rc = hyplet_ctl(HYPLET_SET_CALLBACK, &hplt);
	if (rc < 0){
		printf("hyplet: Failed to map code\n");
		return -1;
	}
	return 0;
}

int hyplet_trap_all_irqs(int irq)
{
	int rc;
	struct hyplet_ctrl hplt;

	hplt.__action.irq = 0xFFFF;
	rc = hyplet_ctl( HYPLET_IMP_TIMER , &hplt);
	if (rc < 0){
		printf("hyplet: Failed assign irq\n");
		return -1;
	}
	return 0;
}

int hyplet_trap_irq(int irq)
{
	int rc;
	struct hyplet_ctrl hplt;

	hplt.__action.irq = irq;
	rc = hyplet_ctl( HYPLET_TRAP_IRQ , &hplt);
	if (rc < 0){
		printf("hyplet: Failed assign irq\n");
		return -1;
	}
	return 0;
}

int hyplet_untrap_irq(int irq)
{
	int rc;
	struct hyplet_ctrl hplt;

	hplt.__action.irq = irq;
	rc = hyplet_ctl( HYPLET_UNTRAP_IRQ , &hplt);
	if (rc < 0){
		printf("hyplet: Failed assign irq\n");
		return -1;
	}
	return 0;
}

int hyplet_set_stack(unsigned long addr,int size)
{
	return hyplet_map(HYPLET_MAP_STACK, addr, size);
}

int hyplet_map_all(void)
{
	struct hyplet_ctrl hplt;
	return hyplet_ctl(HYPLET_MAP_ALL, &hplt);
}

/*
 * associate a function with an id
 */
int hyplet_rpc_set(void *user_hyplet,int func_id)
{
	int rc;
	struct hyplet_ctrl hplt;

	hplt.__action.rpc_set_func.func_addr = (long)user_hyplet;
	hplt.__action.rpc_set_func.func_id = func_id;

	rc = hyplet_ctl(HYPLET_SET_RPC, &hplt);
	if (rc < 0 ){
		printf("hyplet: Failed assign irq\n");
		return -1;
	}
	return 0;
}
