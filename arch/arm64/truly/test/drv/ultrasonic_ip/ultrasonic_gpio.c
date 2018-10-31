#include <linux/module.h>
#include <linux/fs.h>		/* for file_operations */
#include <linux/version.h>	/* versioning */
#include <linux/module.h>
#include <linux/init.h>
#include <linux/compiler.h>
#include <linux/linkage.h>
#include <asm/sections.h>
#include <asm/page.h>
#include <linux/delay.h>
#include <linux/gpio.h>
#include <linux/hyplet.h>

#include "utils.h"

/*
 * must export before use.
* and give a direction
* echo 485 > /sys/class/gpio/export
* echo in > /sys/class/gpio/gpio475/direction
* echo 475 > /sys/class/gpio/export
* echo out > /sys/class/gpio/gpio475/direction
*/
struct hyp_wait hypeve;

static int gpio_r = 485;
module_param(gpio_r, int, 0 );

static int gpio_w = 475;
module_param(gpio_w, int, 0);

static int cpu = 1;
module_param(cpu, int, 0);

void trig(int val)
{
	gpio_set_value(gpio_w, val);
}

/*
 * Wait unti we get this echo value
*/
long wait_echo(int val)
{
	int rc;

read_again:
	rc = gpio_get_value(gpio_r);
	if (rc == val)
		goto read_again;
	return cycles_ns();
}

/*
    Return value is broken to:
	long  cmd:8;  USONIC_ECHO/USONIC_TRIG
	long  cmd_val:8; // trig 1 or 0
	long  pad:48;
*/
static void offlet_trigger(struct hyplet_vm *hyp, struct hyp_wait *hypevent)
{
	int cmd =      (int) ( hyp->user_arg1  & 0xFF );
	int cmd_val =  (int) ( (hyp->user_arg1 >> 8) & 0xFF ) ;

	printk("ARG1 %ld\n", hyp->user_arg1);
	if (cmd == USONIC_TRIG) {
		printk("USONIC TRIG %d\n",cmd_val);
		trig(cmd_val);
		hyp->user_arg1 = cycles_ns();
		return;
	}

	if (cmd == USONIC_ECHO) {
		printk("USONIC ECHO cmd_val=%d\n",cmd_val);
		hyp->user_arg1 = wait_echo(cmd_val);
		return;
	}

	printk("should not be here.reset to TRIG %ld\n",hyp->user_arg1);
}

static int offlet_init(void)
{
	hypeve.offlet_action =  offlet_trigger;
	
	printk("offlet:  trigger on cpu %d\n"
			"gpios: %d %d\n",
			cpu, gpio_w, gpio_r);

	offlet_register(&hypeve, cpu);

	return 0;
}

static void offlet_cleanup(void) 
{
//	gpio_free(gpio_w);
//	gpio_free(gpio_r);

	offlet_unregister(&hypeve,cpu);
	printk( "offlet exit\n");
}

module_init(offlet_init);
module_exit(offlet_cleanup);

MODULE_DESCRIPTION("offlet gpio");
MODULE_AUTHOR("Raz Ben Jehuda");
MODULE_LICENSE("GPL");
