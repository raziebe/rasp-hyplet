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
	size_t hooked_offset, attest_offset, sz;
	void *hooked_section;
	void* attest_section;

	file = (struct file*)data;

	hooked_section = get_section_data(file,
									  read_from_file,
									  ".hooked", /* search for the hooked section */
									  NULL,
									  &hooked_offset,
									  NULL);
	if (!hooked_section)
		return NULL;

	attest_section = get_section_data(file,
									read_from_file,
									".attest",/* search for the attest(clear text) section */
									NULL,
									&attest_offset,
									&sz);
	if (!attest_section)
		return NULL;

	if (sz > PAGE_SIZE){
		printk("C-Flat Linux does not support size(%d) > 4096\n",(int)sz);
		return NULL;
	}

	image_file = (PIMAGE_FILE)tp_alloc(sizeof(IMAGE_FILE));
	if (image_file == NULL)
		return NULL;

	image_file->attest.uaddr  = attest_section;
	image_file->attest.offset = attest_offset;
	image_file->attest.size   = sz;

	image_file->hooked.uaddr  = hooked_section;
	image_file->hooked.offset = hooked_offset;
	image_file->hooked.size	  = sz;

	printk("C-FLAT: hooked section is at %p offset %d, "
			"attest section is at %p offset %d size=%d\n",
				image_file->hooked.uaddr,
				(int)image_file->hooked.offset,
				image_file->attest.uaddr,
				(int)image_file->attest.offset,
				(int)image_file->attest.size);

	return image_file;
}
