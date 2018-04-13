#include <linux/module.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/moduleparam.h>

#include <linux/gfp.h>
#include <linux/buffer_head.h>
#include <linux/page_ref.h>
#include <linux/string.h>

static int num_pages = 10;
struct page *pg[100];

extern struct page* palloc_pages(unsigned int order, char id[55]);
extern void pfree_pages(struct page* pg, unsigned int order);
extern void take_backup(void);

static int __init pcall_init(void)
{
	int i=0;
	char id[55]="pcall";
	char page_num[9];
	printk(KERN_INFO "We shall be allocating and freeing %d persistent pages in this module\n", num_pages);

	for(i=0;i<num_pages;i++){
		sprintf(page_num,"%d",i);
		strcat(id,page_num);
		pg[i] = alloc_page_pm(GFP_PM,0,id);
		printk(KERN_INFO "Got page %lu from persistent region\nrefcount = %d\n", page_to_pfn(pg[i]), page_ref_count(pg[i]));
		TestSetPageDirty(pg[i]);
		snprintf((char*) page_address(pg[i]), PAGE_SIZE, "[module pcall] This is the %dth page called by pcall module\n",i+1);
		printk(KERN_INFO "%s\n", (char*) page_address(pg[i]));
		strcpy(id,"pcall");
	}
	take_backup();
	TestSetPageDirty(pg[i-1]);
	return 0;
}

static void __exit pcall_exit(void)
{
//	int i=0;
	printk(KERN_INFO "Exiting from module pcall ...\n");

//	for(i=0;i<num_pages;i++) {
//		printk(KERN_INFO "Freeing page %lu from persistent region\n%s\n", page_to_pfn(pg[i]),(char*)
//		page_address(pg[i]));
//		__free_pages(pg[i],0);
//		printk(KERN_INFO "Freeing successfull\n");
//	}
}

module_init(pcall_init);
module_exit(pcall_exit);

module_param(num_pages, int, S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH);
MODULE_PARM_DESC(num_pages, "How many pages shall be called");

MODULE_LICENSE("GPL");

/* Who wrote this module? */
MODULE_AUTHOR("Siddharth Agrawal");
/* What does this module do */
MODULE_DESCRIPTION("Module to check if palloc_pages() and pfree_pages() calls are working");
