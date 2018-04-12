#include <linux/init.h>
#include <linux/module.h> 

MODULE_LICENSE("Dual BSD/GPL");
extern int hello_export(void);
static int hello_init(void)
{ 

	 printk(KERN_INFO "Hello,world\n");
	  hello_export();
	   return 0; 
}
static void hello_exit(void) 
{ 

	 printk(KERN_INFO "Goodbye cruel world\n"); 
} 
module_init(hello_init);
module_exit(hello_exit);
