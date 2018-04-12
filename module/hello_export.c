#include <linux/init.h>
#include <linux/module.h> 
#include <linux/kernel.h>

MODULE_LICENSE("Dual BSD/GPL");

char buf[100];
static int hello_init(void)
{ 

	 printk(KERN_INFO "Hello,world\n");
	 snprintf(buf, sizeof(buf),"My name is Siddharth Agrawal");
	  return 0; 
}
static int hello_export(void) {
	printk(KERN_INFO "Hello from another module\n");
	return 0;
} 

static void hello_exit(void) 
{ 

	 printk(KERN_INFO "Goodbye cruel world\n%s\n",buf); 
} 
EXPORT_SYMBOL(hello_export);
module_init(hello_init);
module_exit(hello_exit);
