#include <linux/module.h>
#include <linux/highmem.h>
#include <linux/hyplet.h>
#include <linux/delay.h>
#include <linux/proc_fs.h>

#include "common.h"
#include "ImageManager.h"

int tp_put_trap(void);
int tp_put_nop(void);

/* user interface  */
static struct proc_dir_entry *tpprocfs = NULL;
static IMAGE_MANAGER *imgmgr = NULL;

static ssize_t tp_proc_write(struct file *file, const char __user * buffer,
			  size_t count, loff_t * dummy);
static ssize_t tp_proc_read(struct file *filp, char __user * page,
			 size_t size, loff_t * off);
static int tp_proc_open(struct inode *inode, struct file *filp);


static struct file_operations tp_proc_ops = {
	.open = tp_proc_open,
	.read = tp_proc_read,
	.write = tp_proc_write,
};


static ssize_t tp_proc_write(struct file *file, const char __user * buffer,
			  size_t count, loff_t * dummy)
{
	PIMAGE_FILE img = imgmgr->first_active_image;

	if (imgmgr->first_active_image == NULL)
		return -1;

	if (!strncmp(buffer,"1",1)){
		img->flags = (img->flags & ~CFLAT_FLG_UNSET_TRAP);
		img->flags = img->flags | CFLAT_FLG_SET_TRAP;
		if (!tp_put_trap())
			return count;
	}

	if (!strncmp(buffer,"0",1)){
		img->flags = (img->flags & ~CFLAT_FLG_SET_TRAP);
		img->flags = img->flags | CFLAT_FLG_UNSET_TRAP;
		if ( !tp_put_nop() )
			return count;
	}
	return -1;
}

static int tp_proc_open(struct inode *inode, struct file *filp)
{
	filp->private_data = (void *)0x01;
	return 0;
}

static ssize_t tp_proc_read(struct file *filp, char __user * page,
			 size_t size, loff_t * off)
{
	ssize_t len = 0;
	struct hyplet_vm *vm = NULL;
	int cpu;

	if (filp->private_data == 0)
		return 0;

	if (imgmgr->first_active_image == NULL)
		len += sprintf(page + len,"not active\n");
	else{
		char* in =  "in cflat";
		char* out = "not in cflat";
		char *cflat_state = out;
		PIMAGE_FILE img = imgmgr->first_active_image;
	
		if (img->flags & CFLAT_FLG_SET_TRAP)
			cflat_state = in;
		len += sprintf(page + len, "CFLAT: active PID=%d %s\n",
				 img->pid, cflat_state);
	}

	for_each_possible_cpu(cpu){
		vm = hyplet_get(cpu);
		len += sprintf(page + len, "#%d cnt %ld\n",
				cpu, vm->cnt);
	}
	filp->private_data = 0x00;
	return len;
}


void tp_init_procfs(IMAGE_MANAGER *_imgmgr)
{
	tpprocfs = proc_create_data("cflat_stats", O_RDWR, NULL, &tp_proc_ops, NULL);
	if (!tpprocfs){
		printk("hyplet: failed to initialize truly procfs\n");
		return;
	}
	imgmgr = _imgmgr;
}
