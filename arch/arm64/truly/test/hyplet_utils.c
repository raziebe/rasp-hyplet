#define _GNU_SOURCE

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

#include "hyp_spinlock.h"
#include "hyplet_user.h"
#include "hyplet_utils.h"

static struct hyp_state hypstate;
const int SYSCALL_HYPLET  = 292;

int hyplet_ctl(int cmd,struct hyplet_ctrl *hplt)
{
	hplt->cmd = cmd;
	return syscall(SYSCALL_HYPLET,hplt);
}


int __hyplet_map(int cmd, void* addr,int size,int cpu)
{
	int rc;
	struct hyplet_ctrl hplt;

	hplt.__resource.cpu = cpu;
	hplt.__action.addr.addr = (unsigned long)addr;
	hplt.__action.addr.size = size;
	rc = hyplet_ctl(cmd, &hplt);
	if (rc < 0){
		printf("hyplet: Failed to map code\n");
		return -1;
	}
	return 0;
}


int hyplet_set_callback(void *addr,int cpu)
{
	int rc;
	struct hyplet_ctrl hplt;

	hplt.__action.addr.addr = (unsigned long)addr;
	hplt.__resource.cpu = cpu;
	rc = hyplet_ctl(HYPLET_SET_CALLBACK, &hplt);
	if (rc < 0){
		printf("hyplet: Failed to map code\n");
		return -1;
	}
	return 0;
}

int hyplet_run(void)
{
	int rc;
	struct hyplet_ctrl hplt;

	rc = hyplet_ctl(HYPLET_EXECUTE, &hplt);
	if (rc < 0){
		printf("hyplet: Failed to map code\n");
		return -1;
	}
	return 0;
}

int hyplet_trap_all_irqs(int irq,int cpu)
{
	int rc;
	struct hyplet_ctrl hplt;

	hplt.__resource.irq = 0xFFFF;
	hplt.__resource.cpu = cpu;
	rc = hyplet_ctl( HYPLET_IMP_TIMER , &hplt);
	if (rc < 0){
		printf("hyplet: Failed assign irq\n");
		return -1;
	}
	return 0;
}

int hyplet_drop_cpu(int cpu)
{
	int bytes;
	char status[2]={0};
	char cpustr[256];
	int fd;
	int ret = 0;

	sprintf(cpustr,"/sys/devices/system/cpu/cpu%d/online",cpu);
	fd = open(cpustr,O_RDWR);
	if (fd < 0 ){
		perror("open:");
		return -1;
	}

	bytes = read(fd,status,sizeof(status));
	if (bytes <= 0 ){
		perror("read:");
		close(fd);
		return -1;
	}

	if (!strncmp("0", status,1)){
		printf("Cpu %d already down\n",cpu);
		close(fd);
		return 0;
	}

	if (!strncmp("1",status,1)){
		bytes = write(fd,"0",1);
		if (bytes <= 0) {
			printf("Failed to drop proccessor \n");
			ret = -1;
		}
		close(fd);
		return ret;
	}

	printf("insane status %s\n",status);
	close(fd);
	return -1;
}


int hyplet_assign_offlet(int cpu,void* addr)
{
	int rc;
	struct hyplet_ctrl hplt;

	hplt.__action.addr.addr = (unsigned long)addr;
	hplt.__resource.cpu = cpu;
	rc = hyplet_ctl(OFFLET_SET_CALLBACK , &hplt);
	if (rc < 0){
		printf("hyplet: Failed assign irq\n");
		return -1;
	}
	return 0;
}


int hyplet_trap_irq(int irq,int cpu)
{
	int rc;
	struct hyplet_ctrl hplt;

	hplt.__resource.irq = irq;
	hplt.__resource.cpu = cpu;
	rc = hyplet_ctl( HYPLET_TRAP_IRQ , &hplt);
	if (rc < 0){
		printf("hyplet: Failed assign irq\n");
		return -1;
	}
	return 0;
}

int hyplet_untrap_irq(int irq,int cpu)
{
	int rc;
	struct hyplet_ctrl hplt;

	hplt.__resource.irq = irq;
	hplt.__resource.cpu = cpu;
	rc = hyplet_ctl( HYPLET_UNTRAP_IRQ , &hplt);
	if (rc < 0){
		printf("hyplet: Failed assign irq\n");
		return -1;
	}
	return 0;
}

int hyplet_set_stack(void* addr,int size,int cpu)
{
	return __hyplet_map(HYPLET_MAP_STACK, addr, size, cpu);
}

static void hyplet_init_print(void)
{
	memset(&hypstate, 0x00, sizeof(hypstate));
}

int hyplet_map_all(int cpu)
{
	struct hyplet_ctrl hplt;

	hyplet_init_print();
	hplt.__resource.cpu = cpu;
	return hyplet_ctl(HYPLET_MAP_ALL, &hplt);
}

int hyplet_set_print(void *addr,int cpu)
{
	int rc;
	struct hyplet_ctrl hplt;

	hplt.__resource.cpu = cpu;
	hplt.__action.addr.addr = (unsigned long)addr;
	rc = hyplet_ctl(HYPLET_REGISTER_PRINT , &hplt);
	if (rc < 0){
		printf("hyplet: Failed map print func\n");
		return -1;
	}
	return 0;
}

int hyplet_map_vma(void *addr,int cpu)
{
	int rc;
	struct hyplet_ctrl hplt;

	hplt.__resource.cpu = cpu;
	hplt.__action.addr.addr = (unsigned long)addr;
	hplt.__action.addr.size = 1;
	rc = hyplet_ctl(HYPLET_MAP_VMA , &hplt);
	if (rc < 0){
		printf("hyplet: Failed map print func\n");
		return -1;
	}
	return 0;
}

int hyplet_map(void *addr,int size,int cpu)
{
	int rc;
	struct hyplet_ctrl hplt;

	hplt.__resource.cpu = cpu;
	hplt.__action.addr.addr = (unsigned long)addr;
	hplt.__action.addr.size = size;
	rc = hyplet_ctl(HYPLET_MAP , &hplt);
	if (rc < 0){
		printf("hyplet: Failed map print func\n");
		return -1;
	}
	return 0;
}

/*
 * associate a function with an id
 */
int hyplet_rpc_set(void *user_hyplet,int func_id,int cpu)
{
	int rc;
	struct hyplet_ctrl hplt;

	hplt.__action.rpc_set_func.func_addr = (long)user_hyplet;
	hplt.__action.rpc_set_func.func_id = func_id;
	hplt.__resource.cpu = cpu;
	rc = hyplet_ctl(HYPLET_SET_RPC, &hplt);
	if (rc < 0 ){
		printf("hyplet: Failed assign irq\n");
		return -1;
	}
	return 0;
}

int hyp_wait(int cpu,int ms)
{
	struct hyplet_ctrl hplt;

     	hplt.__resource.timeout_ms = ms;
	hplt.__resource.cpu = cpu;
	return hyplet_ctl(HYPLET_WAIT, &hplt);
}

/*
 * hyp_strlen and others can be refrained by statiticly linking
*/
size_t hyp_strlen(const char *str)
{
	int i = 0;

	for (; *str != 0 ; i++)
		str++;
	return i;
}

int hyp_memcpy(char *dst, const char *src,int len)
{
	int i = 0;

	for (; i < len; i++)
		dst[i] = src[i];

	return i;
}

char* hyp_strncpy(char *dst, const char *src,int n)
{
	int i = 0;

	for (; i < n; i++)
		dst[i] = src[i];
	return &dst[0];
}

char* hyp_strcpy(char *dst, const char *src)
{
	int i = 0;
	int len;

	len = strlen(src);
	for (; i < len; i++)
		dst[i] = src[i];
	return &dst[0];
}

/*
 * Collect & Cache the arguments 
*/
int hyp_print(const char *fmt, ...)
{
     va_list ap;
     int i = 0,f = 0;
     int idx = hypstate.fmt_idx;

     hyp_memcpy(&hypstate.fmt[idx].fmt[0], fmt, hyp_strlen(fmt) );

     va_start(ap, fmt);
     while (*fmt) {

         if ( *fmt != '%') {
		fmt++;
		continue;				
	  } else{
		fmt++;
	  }

         switch (*fmt++) {
           case 's':  /* string */
                   hypstate.fmt[idx].i[i++] = (long)va_arg(ap, char *);
                   break;

	   case 'f': /* float */
                   hypstate.fmt[idx].f[f++] = (double)va_arg(ap, double);
                   break;

	   case 'l':  /* long */
                   hypstate.fmt[idx].i[i++] = va_arg(ap, long );
                   break;

           case 'd':  /* int */
                   hypstate.fmt[idx].i[i++] = va_arg(ap, int );
                   break;

           case 'c':  /* char */
                   hypstate.fmt[idx].i[i++] = (char) va_arg(ap, int);
                   break;
           }
           va_end(ap);
     }

     spin_lock(&hypstate.sync);
     hypstate.fmt_idx =  (hypstate.fmt_idx + 1) % PRINT_LINES;
     spin_unlock(&hypstate.sync);
}


static void __print_hyp(int idx) {

	struct hyp_fmt fmt;

	spin_lock(&hypstate.sync);
	memcpy(&fmt,&hypstate.fmt[idx],sizeof(fmt));
	spin_unlock(&hypstate.sync);

	hyp_print2(&hypstate.fmt[idx]);
}

void print_hyp(void) 
{
	__print_hyp(hypstate.fmt_idx);
}
