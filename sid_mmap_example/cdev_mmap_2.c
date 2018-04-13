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

#define MAX_BUF_SIZE 8192
#define DEVNAME "demo"

static int major;
atomic_t  device_opened;
static unsigned long gptr;


static int demo_open(struct inode *inode, struct file *file)
{
        atomic_inc(&device_opened);
        try_module_get(THIS_MODULE);
        printk(KERN_INFO "Device opened successfully\n");
        return 0;
}

static int demo_release(struct inode *inode, struct file *file)
{
        atomic_dec(&device_opened);
        module_put(THIS_MODULE);
        printk(KERN_INFO "Device closed successfully\n");

        return 0;
}
static ssize_t demo_read(struct file *filp,
                           char *buffer,
                           size_t length,
                           loff_t * offset)
{
        memcpy(buffer, (void *)gptr, 10);
        return 10;
}

static ssize_t
demo_write(struct file *filp, const char *buff, size_t len, loff_t * off)
{
        printk(KERN_INFO "Sorry, this operation isn't supported.\n");
        return -EINVAL;
}
static void vm_close(struct vm_area_struct *vma)
{
    struct page *page;
    page = pfn_to_page(__pa(gptr) >> PAGE_SHIFT);
    printk(KERN_INFO "vma closed pid = %d\n", current->pid);
}

static int vm_fault(struct vm_fault *vmf)
{
    struct page *page;
    page = pfn_to_page(__pa(gptr) >> PAGE_SHIFT);
    get_page(page);

    vmf->page = page;
    printk(KERN_INFO "vma fault pid = %d\n", current->pid);
    return 0;
}

static void vm_open(struct vm_area_struct *vma)
{
    printk(KERN_INFO "vma opened pid = %d\n", current->pid);
}

static struct vm_operations_struct vm_ops =
{
    .close = vm_close,
    .fault = vm_fault,
    .open = vm_open,
};

static int demo_mmap(struct file *filp, struct vm_area_struct *vma)
{
    printk(KERN_INFO "mmap called\n");
    if(vma->vm_end - vma->vm_start > 4096){
         printk(KERN_INFO "MMAP error. max len = 4096\n");
         return -EINVAL;
    }
    vma->vm_ops = &vm_ops;
    vma->vm_flags |= VM_DONTEXPAND;
    vm_open(vma);
    return 0;
}

static struct file_operations fops = {
        .read = demo_read,
        .write = demo_write,
        .open = demo_open,
        .release = demo_release,
        .mmap = demo_mmap,
};


int init_module(void)
{
	printk(KERN_INFO "Hello kernel\n");
        gptr = (unsigned long) kmalloc(4096, GFP_KERNEL);
        if(!gptr || ((gptr >> PAGE_SHIFT) << PAGE_SHIFT) != gptr){
               if(gptr)
                   kfree((void *)gptr);
               return -ENOMEM;      
        }
            
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
        kfree((void *)gptr);
        unregister_chrdev(DEV_MAJOR, DEVNAME);
	printk(KERN_INFO "Goodbye kernel\n");
}
MODULE_AUTHOR("deba@cse.iitk.ac.in");
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Demo modules");
