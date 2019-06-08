#pragma once

#include "tp_types.h"


struct cflat_section{
	char*   uaddr;
	size_t  offset;
	char*   kaddr;
	char*   kaddr_copy;
	int 	size;
};

struct _IMAGE_FILE {

	struct cflat_section hooked;
	struct cflat_section attest;


	size_t 	code_base;
	size_t  image_base;
	int pid;
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
