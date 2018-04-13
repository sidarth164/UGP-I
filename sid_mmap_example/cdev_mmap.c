#include<linux/module.h>
#include<linux/kernel.h>
#include<linux/mm.h>
#include<linux/mm_types.h>
#include<linux/file.h>
#include<linux/fs.h>
#include<linux/path.h>
#include<linux/slab.h>
#include<linux/dcache.h>
#include<linux/sched.h>
#include<linux/delayed_call.h>
#include<linux/fs_struct.h>

#include "syschar.h"
#include<linux/string.h>
#include<linux/gfp.h>

#define MAX_BUF_SIZE 8192
#define DEVNAME "demo"

static int major;
atomic_t  device_opened;
struct page **pg;
int num_vmpages;

static int demo_open(struct inode *inode, struct file *file)
{
        atomic_inc(&device_opened);
        try_module_get(THIS_MODULE);
        printk(KERN_INFO "DEBUG: Device opened successfully by %s\n", current->comm);
        return 0;
}

static int demo_release(struct inode *inode, struct file *file)
{
        atomic_dec(&device_opened);
        module_put(THIS_MODULE);
        printk(KERN_INFO "DEBUG: Device closed successfully\n");

        return 0;
}

static void vm_close(struct vm_area_struct *vma)
{
	int i;
	if((current->flags & PF_EXITING) != PF_EXITING)
		for(i=0; i<num_vmpages; i++){
			if(pg[i]){
				printk(KERN_INFO "DEBUG: freeing persistent page\n");
				__free_pages(pg[i], 0);
			}
			else
				printk(KERN_INFO "DEBUG: page with offset %d is null\n", i);
		}
	else
		printk(KERN_INFO "Exiting the process\n");
	kfree(pg);
	printk(KERN_INFO "DEBUG: Freed the *pg array\n");
    printk(KERN_INFO "DEBUG: vma closed pid = %d\n", current->pid);
}

static int vm_fault(struct vm_fault *vmf)
{
	char id[55];
	int offset = (vmf->address - vmf->vma->vm_start)/PAGE_SIZE;
	sprintf(id, "%s%lu%d", current->comm, vmf->vma->vm_start, offset);
	pg[offset] = alloc_page_pm(GFP_PM, 0, id);
	// get_page(pg[offset]);

    vmf->page = pg[offset];
	printk(KERN_INFO "DEBUG: Page with offset no. %d is allocated\n", offset);
    printk(KERN_INFO "DEBUG: vma fault pid = %d\n", current->pid);
    return 0;
}

static void vm_open(struct vm_area_struct *vma)
{
	int i;
    printk(KERN_INFO "DEBUG: vma opened pid = %d\n", current->pid);
	for(i=0; i<num_vmpages; i++){
		pg[i] = NULL;
	}

}

static struct vm_operations_struct vm_ops =
{
    .close = vm_close,
    .fault = vm_fault,
    .open = vm_open,
};

int get_num_vmpages(unsigned long start, unsigned long end)
{
	int rem;
	int num_pages;
	num_pages = (end-start)/PAGE_SIZE;
	rem = (end-start) - num_pages * PAGE_SIZE;
	if(rem>0)
		num_pages++;
	return num_pages;
}

static int demo_mmap(struct file *filp, struct vm_area_struct *vma)
{
    printk(KERN_INFO "mmap called\n");
	num_vmpages = get_num_vmpages(vma->vm_start, vma->vm_end);
	pg = kmalloc(num_vmpages * sizeof(struct page*), GFP_KERNEL);
//    if(vma->vm_end - vma->vm_start > 4096){
//         printk(KERN_INFO "MMAP error. max len = 4096\n");
//         return -EINVAL;
//    }
	printk(KERN_INFO "DEBUG: Mapping %d persistent pages\n", num_vmpages);
    vma->vm_ops = &vm_ops;
    vma->vm_flags |= VM_DONTEXPAND;
    vm_open(vma);
    return 0;
}
static ssize_t demo_read(struct file *filp,
                           char *buffer,
                           size_t length,
                           loff_t * offset)
{
        memcpy(buffer, (char *)pg, 10);
        return 10;
}

static struct file_operations fops = {
        .open = demo_open,
        .release = demo_release,
        .mmap = demo_mmap,
		.read = demo_read,
};


int init_module(void)
{
	printk(KERN_INFO "Module to map persistent memory using mmap syscall\n");
    major = register_chrdev(DEV_MAJOR, DEVNAME, &fops);
    if (major < 0) {      
      printk(KERN_ALERT "Registering char device failed with %d\n", major);   
      return major;
    }                 
  
    printk(KERN_INFO "I was assigned major number %d. To talk to\n", DEV_MAJOR);                                                              
    printk(KERN_INFO "'mknod /dev/%s c %d 0'.\n", DEVNAME, DEV_MAJOR);  
    atomic_set(&device_opened, 0);
	return 0;
}

void cleanup_module(void)
{
    unregister_chrdev(DEV_MAJOR, DEVNAME);
	printk(KERN_INFO "Exiting module cdev_mmap\n");
}
MODULE_AUTHOR("Siddharth Agrawal");
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Module to map persistent memory using mmap syscall");
