#pragma once

#include "tp_types.h"
#include "common.h"

#define CFLAT_FLG_TRAP_MAPPED	(1<<1)	/* bkpt code can be accessed from kernel space */
#define CFLAT_FLG_SET_TRAP		(1<<2)  /* manually triggered. set the trap to on */
#define CFLAT_FLG_UNSET_TRAP	(1<<3)  /* manually triggered. unset the trap and put nop */
#define CFLAT_STATE_TRAP_IS_ON	(1<<4)
#define CFLAT_FLG_NOP32			(1<<5)

struct _IMAGE_FILE {

	struct cflat_section trap;

	size_t 	code_base;
	size_t  image_base;
	int pid;
	int flags;
};


typedef struct _IMAGE_FILE IMAGE_FILE, *PIMAGE_FILE;

char* image_file_get_hooked_section(PIMAGE_FILE image_file);
char* image_file_get_function_at_offset(PIMAGE_FILE image_file, UINT64 assumed_virtual_address);
char* image_file_get_attest_section(PIMAGE_FILE image_file);
UINT32 image_file_get_attest_section_size(PIMAGE_FILE image_file);
void image_file_free(PIMAGE_FILE image_file);
PIMAGE_FILE image_file_init(void *data);
void image_file_init_bases(PIMAGE_FILE image_file, UINT64 actual_image_base);

UINT64 image_file_get_assumed_text_base(PIMAGE_FILE image_file);
size_t image_file_get_hooked_section_offset(PIMAGE_FILE image_file);
