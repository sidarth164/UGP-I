#include <linux/module.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/moduleparam.h>

#include <linux/gfp.h>
#include <linux/buffer_head.h>
#include <linux/page_ref.h>
#include <linux/string.h>

extern void flush_used_pages(void);
extern void take_backup(void);

static int __init pcall_init(void)
{
	printk(KERN_INFO "We shall be flush all used pages in our persistent region to free list in this module\n");
	flush_used_pages();
	return 0;
}

static void __exit pcall_exit(void)
{
	printk(KERN_INFO "Exiting from module flush ...\n");
}

module_init(pcall_init);
module_exit(pcall_exit);

MODULE_LICENSE("GPL");

/* Who wrote this module? */
MODULE_AUTHOR("Siddharth Agrawal");
/* What does this module do */
MODULE_DESCRIPTION("Module to flush all the used pages to free list");
