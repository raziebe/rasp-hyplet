#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <signal.h>
#include <dlfcn.h>


#include "../utils/hyplet_user.h"
#include "../utils/hyplet_utils.h"

#define NR_OPCODES	500000

int func_id = 8;

__attribute__ ((section("hyp_rw")))  unsigned int cnt = 0;
__attribute__ ((section("hyp_rw")))  unsigned int nr_opcodes = 0;
__attribute__ ((section("hyp_rw")))  unsigned int dump_data = 0;
__attribute__ ((section("hyp_rw")))  unsigned long instr[NR_OPCODES][5] = {0};

__attribute__ ((section("hyp_rx")))  long record_opcode(long a1,long a2,long a3,long a4)
{
	instr[cnt][0] = hyp_gettime();
	instr[cnt][1] = a1;
	instr[cnt][2] = a2;
	instr[cnt][3] = a3;
	instr[cnt][4] = a4;

	cnt++;
	if (a1 == 101) {
 		dump_data = 1;
		__sync_synchronize();
		nr_opcodes = cnt;
		__sync_synchronize();
		cnt = 0;
		return 0;
	}
   	cnt = cnt % NR_OPCODES;
	return 0;
}

__attribute__ ((section("hyp_x"))) long  user_log(unsigned long instr,long type) 
{
	return 0;
}

/*
 * This code is executed in an hyplet context
 */
static int hyplet_start(int argc, char *argv[])
{
	int rc;
	int cpu = -1;
	int stack_size = sysconf(_SC_PAGESIZE) * 2;
	void *stack_addr;

	long hyp_sec_x;
	int  hyp_sec_x_size;

	long hyp_sec_rw;
	int  hyp_sec_rw_size;

	long hyp_sec_rx;
	int hyp_sec_rx_size;

  	if (Elf_parser_load_memory_map(argv[0])){
		printf("Failed to ELF parser\n");
		return -1;
  	}

	if (hyplet_map_all(cpu)) {
		fprintf(stderr, "hyplet: Failed to map all\n");
		return -1;
	}

	rc = posix_memalign(&stack_addr,
			    sysconf(_SC_PAGESIZE), stack_size);
	if (rc < 0) {
		fprintf(stderr, "hyplet: Failed to allocate a stack\n");
		return -1;
	}
//
// must fault the stack
//
	memset(stack_addr, 0x00, stack_size);
	printf("Setting stack at %p\n", stack_addr);
	if (hyplet_set_stack(stack_addr, stack_size, cpu)) {
		fprintf(stderr, "hyplet: Failed to map a stack\n");
		return -1;
	}

	if (hyplet_set_stack(stack_addr, stack_size, cpu)) {
		fprintf(stderr, "hyplet: Failed to set a stack\n");
		return -1;
	}

	if (hyplet_set_callback(record_opcode, cpu)) {
		fprintf(stderr, "hyplet: Failed to map test opcode\n");
		return -1;
	}

	if (hyplet_rpc_set(record_opcode, func_id, cpu)) {
		fprintf(stderr, "hyplet: Failed to assign opcode\n");
		return -1;
	}

	fprintf(stderr, "hyplet: set mdcr on XXXXXXX\n");
	return 0;
}

/*
 * it is essential affine the program to the same 
 * core where it runs.
*/
int main(int argc, char *argv[])
{
    int rc;
    int i;
    int iter = 0;
    int run = 1;
    FILE* file;
    char fname[256];
    char buf[256];
    FILE *fp;

    for (i = 0 ; i < NR_OPCODES; i++) {
	instr[i][0] = -1;
	instr[i][1] = -1;
	instr[i][2] = -1;
	instr[i][3] = -1;
    }

    if (hyplet_start(argc, argv)) {
        return -1;
    }

    printf("\nHyplet set. Prepare to run trap\n");

    while(run) {
	
       sleep(1);
       if (dump_data) {

	    dump_data = 0;
	    __sync_synchronize();
	    sprintf(fname,"/tmp/result_%d", iter++);
            fp =  fopen(fname,"w+");
	    printf("Start to dump data\n");
            for (i = 0; i < nr_opcodes ; i++) {
     	        sprintf(buf,"%d,%ld,%lx,%lx,%lx,%lx\n",i, 
			instr[i][0], instr[i][1],  
			instr[i][2], instr[i][3], instr[i][4] );
		fwrite(buf,strlen(buf),1,fp);
            }
	 fclose(fp);
   	 printf("wrote %s\n",fname);
    	}
   }
   return 0;
}
