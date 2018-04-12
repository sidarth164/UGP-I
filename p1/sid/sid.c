#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>

MODULE_LICENSE("GPL");
MODULE_AUTHOR("SIDDHARTH AGRAWAL");

static int sid_init(void){
	printk(KERN_ALERT "A very basic module by sid!\n");
	return 0;
}

static void sid_exit(void){
	printk(KERN_ALERT "Exiting the module!\n");
}

module_init(sid_init);
module_exit(sid_exit);
