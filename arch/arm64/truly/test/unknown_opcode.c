#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <signal.h>
#include <dlfcn.h>

#include "hyplet_user.h"
#include "hyplet_utils.h"

#define NR_OPCODES	100

__attribute__ ((section("hyp_rw")))  
		unsigned long unknown_instr[NR_OPCODES] ={0};
__attribute__ ((section("hyp_rw")))  unsigned long last_instr = 0;
__attribute__ ((section("hyp_rw")))  long test_opcode(long g);
__attribute__ ((section("hyp_rw")))  long start_instr = 0;
__attribute__ ((section("hyp_rw")))  long end_instr = 0;
__attribute__ ((section("hyp_rw")))  int cnt = 0;
__attribute__ ((section("hyp_rw")))  int cpu = 1;
__attribute__ ((section("hyp_rw")))  int run_hyp = 1;
__attribute__ ((section("hyp_rw")))  int tot = 0;
__attribute__ ((section("hyp_rw")))  int nr_opcodes = 1;

/*
 * 
 * type = 0 -> mmu fault
 * type = 1 -> did not fault
 * return 1 to continue run, returns 0 to stop run
 * type = 2, return the starting instruction
*/
__attribute__ ((section("hyp_x"))) long  user_log(unsigned long instr,long type) 
{
	if (run_hyp < 0)
		return 1;

	if (end_instr >= last_instr && last_instr) {
		run_hyp = -2;
		return 1;
	}

	if (type == 2){
		return start_instr;
	}

	tot++;
	if (type == 0) { //record the instruction
		last_instr = instr & 0xFFFFFFFF;
		return 0;
	}
	
	if (cnt == nr_opcodes){
		run_hyp = -1;
		return 1;
	}

	unknown_instr[cnt++] = instr & 0xFFFFFFFF;
	return 0;
}

/*
 * This code is executed in an hyplet context
 */
static int hyplet_start(void)
{
	int rc;
	int stack_size = sysconf(_SC_PAGESIZE) * 1;
	void *stack_addr;
	long hyp_sec_x;
	int  hyp_sec_x_size;
	long hyp_sec_rw;
	int  hyp_sec_rw_size;

	if (!get_section_addr("hyp_x", &hyp_sec_x, &hyp_sec_x_size)) {
		fprintf(stderr, "hyp_x: \n");
		return -1;
	}	
	
	if (!get_section_addr("hyp_rw", &hyp_sec_rw, &hyp_sec_rw_size)) {
		fprintf(stderr, "hyp_rw:\n");
		return -1;
	}	
	
	if (hyplet_map_vma((void *)hyp_sec_rw, cpu)) {
		fprintf(stderr, "hyplet: Failed to hyp_rw\n");
		return -1;
	}


	if (hyplet_map_vma((void*)hyp_sec_x, cpu)) {
		fprintf(stderr, "hyplet: Failed to map sec_x\n");
		return -1;
	}

	rc = posix_memalign(&stack_addr,
			    sysconf(_SC_PAGESIZE), stack_size);
	if (rc < 0) {
		fprintf(stderr, "hyplet: Failed to allocate a stack\n");
		return -1;
	}

// must fault it
	memset(stack_addr, 0x00, stack_size);

	if (hyplet_map(test_opcode, 1,cpu)) {
		fprintf(stderr, "hyplet: Failed to map test opcode\n");
		return -1;
	}

	fprintf(stderr, "hyplet: stack addr %x\n", stack_addr);
	if (hyplet_map(stack_addr, stack_size, cpu)) {
		fprintf(stderr, "hyplet: Failed to map a stack\n");
		return -1;
	}

	if (hyplet_set_stack(stack_addr, stack_size, cpu)) {
		fprintf(stderr, "hyplet: Failed to set a stack\n");
		return -1;
	}

        if (hyplet_assign_offlet(cpu, test_opcode)) {
             fprintf(stderr, "hyplet: Failed to assign opcode\n");
             return -1;
        }

	if (hyplet_set_print(user_log ,cpu)){
		fprintf(stderr, "hyplet: Failed to set print\n");
		return -1;
	}

	return 0;
}

void signal_ctrlc(int signum)
{
	run_hyp = 0;
}

void help(char *argv[])
{
	printf("Usage %s: -s <starting instruction> "
			"-e <end instruction> -c #opcodes"
		" <end instruction>Ver 1.2 \n",argv[0]);
	exit(0);
}

int get_params(int argc, char *argv[])
{
   int  opt;

   while ((opt = getopt(argc, argv, "e:s:c:")) != -1) {
           switch (opt) {
 
              case 'e':
  		   sscanf(optarg,"0x%x", &end_instr);
                   break;

               case 's':
    		   sscanf(optarg,"0x%x", &start_instr);
                   break;

               case 'c':
    		   nr_opcodes = atoi(optarg);
		   if (nr_opcodes >= NR_OPCODES) {
		   	nr_opcodes = NR_OPCODES;
			printf("#opcodes=%d\n",nr_opcodes);
		   }
                   break;

               default: /* '?' */
		   help(argv);
		   break;	
     }
  }

  if (start_instr < end_instr) {
	printf("");
	exit(0);
  }

  if (start_instr == 0)
	help(argv);
}

/*
 * it is essential affine the program to the same 
 * core where it runs.
*/
int main(int argc, char *argv[])
{
    int rc;
    int i;

    if ( get_nprocs() == 0) {
    	printf("Cannot use a single processor\n");
    	return -1;
    }

    get_params(argc, argv);
    
    signal( SIGINT , signal_ctrlc);
    printf("starting at 0x%x and end at 0x%x\n", 
			start_instr, end_instr);

    if (Elf_parser_load_memory_map(argv[0])){
	printf("Failed to ELF parser\n");
	return -1;
    }

    if (hyplet_start()) {
	return -1;
    }

    if (hyplet_drop_cpu(cpu) < 0 ){
         printf("Failed to drop processor\n");
         return -1;
    }

    do {
    	printf("Got %d/%d opcodes last opcode=0x%lx\n",
    				cnt, tot, last_instr);

    	sleep(1);
    } while(run_hyp > 0);

    printf("Exiting from hyplet cnt=%d run_hyp=%d\n",cnt,run_hyp);
    for (i = 0 ; i < cnt; ++i)
    	printf("0x%lx\n",unknown_instr[i]);
}
