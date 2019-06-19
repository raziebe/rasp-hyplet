#include <linux/module.h>    // included for all kernel modules
#include <linux/kernel.h>    // included for KERN_INFO
#include <linux/init.h>      // included for __init and __exit macros
#include <linux/slab.h>
#include <linux/pid.h>
#include <linux/kthread.h>
#include <linux/sched.h>
#include <linux/wait.h>
#include <linux/path.h>
#include <linux/mm_types.h>
#include <linux/fs.h>
#include <asm/uaccess.h>

#include "ImageFile.h"
#include "osal.h"
#include "linux_kernfile.h"
#include "elf.h"



static size_t read_from_file(void* file, UINT64 offset, char* dest, size_t amount)
{
	if (!file)
		return 0;

	return file_read((struct file*)file, offset, dest, amount);
}

PIMAGE_FILE image_file_init(void* data)
{
	PIMAGE_FILE image_file;
	struct file* file;
	size_t trap_offset, sz;
	void *trap_section;

	file = (struct file*)data;

	trap_section = get_section_data(file,
					read_from_file,
					".trap",/* search for the attest(clear text) section */
					NULL,
					&trap_offset,
					&sz);
	if (!trap_section)
		return NULL;

	if (sz > PAGE_SIZE){
		printk("C-Flat Linux does not support size(%d) > 4096\n",(int)sz);
		return NULL;
	}

	image_file = (PIMAGE_FILE)tp_alloc(sizeof(IMAGE_FILE));
	if (image_file == NULL)
		return NULL;

	memset(image_file,0x00,sizeof(IMAGE_FILE));

	image_file->trap.uaddr  = trap_section;
	image_file->trap.offset = trap_offset;
	image_file->trap.size	= sz;
	image_file->flags  =  CFLAT_FLG_NOP32;

	return image_file;
}
