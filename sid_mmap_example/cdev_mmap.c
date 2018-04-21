#include<linux/module.h>
#include<linux/kernel.h>
#include<linux/ioctl.h>
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
#define DEVNAME "pmap"

static int major;
atomic_t  device_opened;
struct rb_root vm_tree = RB_ROOT;

struct vm_tree {
	struct rb_node node;
	struct page **pg;
	unsigned long num_vmpages;
	char id[50];
};

struct vm_tree *vm_search(struct rb_root *root, char id[50])
{
   	struct rb_node *node = root->rb_node;

	while (node) {
 		struct vm_tree *vmnode = rb_entry(node, struct vm_tree, node);
		int result = strcmp(id, vmnode->id);

		if (result < 0)
			node = node->rb_left;
		else if (result > 0)
 			node = node->rb_right;
		else
			return vmnode;
	}
	return NULL;
}

bool vm_insert(struct rb_root *root, struct vm_tree *data)
{
  	struct rb_node **new = &(root->rb_node), *parent = NULL;
//	printk(KERN_INFO "DEBUG: Inserting a new node (with id %s) in the vm_tree\n", data->id);

  	/* Figure out where to put new node */
  	while (*new) {
 		struct vm_tree *this = rb_entry(*new, struct vm_tree, node);
		int result = strcmp(data->id,this->id);
		parent = *new;

		if (result < 0)
 			new = &((*new)->rb_left);
  		else if (result > 0)
			new = &((*new)->rb_right);
 		else
 			return false;
	}

	/* Add new node and rebalance tree. */
 	rb_link_node(&data->node, parent, new);
 	rb_insert_color(&data->node, root);
	return true;
}

static int pmap_open(struct inode *inode, struct file *file)
{
        atomic_inc(&device_opened);
        try_module_get(THIS_MODULE);
//		printk(KERN_INFO "DEBUG: Device opened successfully by %s\n", current->comm);
        return 0;
}

static int pmap_release(struct inode *inode, struct file *file)
{
        atomic_dec(&device_opened);
        module_put(THIS_MODULE);
//		printk(KERN_INFO "DEBUG: Device closed successfully\n");

        return 0;
}

static void vm_close(struct vm_area_struct *vma)
{
	unsigned long i;
	struct vm_tree *vmnode;
	char search[50];
	sprintf(search, "%s*%lu", current->comm, vma->vm_start);
	vmnode = vm_search(&vm_tree, search);
	printk(KERN_INFO "DEBUG: vm_close called\n");
	if(!vmnode){
		printk(KERN_INFO "error: vmnode is null\n");	
		return ;
	}
	if((current->flags & PF_EXITING) != PF_EXITING)
		for(i=0; i<vmnode->num_vmpages; i++){
			if(vmnode->pg[i]){
//				printk(KERN_INFO "DEBUG: freeing persistent page\n");
				__free_pages(vmnode->pg[i], 0);
			}
//			else
//				printk(KERN_INFO "DEBUG: page with offset %lu is null\n", i);
		}
//	else{
//		printk(KERN_INFO "DEBUG: Exiting the process\n");
//	}
	if(vmnode->pg){
//		printk(KERN_INFO "DEBUG: Freed the vmnode->pg array\n");
		kfree(vmnode->pg);
	}
	else 
		printk(KERN_INFO "DEBUG: struct page **pg was null... something is wrong...\n");
	rb_erase(&vmnode->node, &vm_tree);
	kfree(vmnode);
//	printk(KERN_INFO "DEBUG: vmnode (with id %s) removed successfully\n",search);
	printk(KERN_INFO "DEBUG: vma closed pid = %d\n", current->pid);
}

static int vm_fault(struct vm_fault *vmf)
{
	char id[55], search[50];
	struct vm_tree *vmnode;
	unsigned long offset = (vmf->address - vmf->vma->vm_start)/PAGE_SIZE;
	sprintf(search, "%s*%lu", current->comm, vmf->vma->vm_start);
	vmnode = vm_search(&vm_tree, search);
	if(!vmnode){
		printk(KERN_INFO "error: vmnode is null\n");	
		return 1;
	}
	sprintf(id, "%s*%lu", search, offset);
//	printk(KERN_INFO "DEBUG: vm_fault called\n");
	vmnode->pg[offset] = alloc_page_pm(GFP_PM, 0, id, vmf->vma->vm_mm);
//	if(vmnode->pg[offset])
//		printk(KERN_INFO "DEBUG: Got persistent page to allocate to vm at address %lu\n", vmf->address);
	get_page(vmnode->pg[offset]);

	vmnode->pg[offset]->index = offset;
    vmf->page = vmnode->pg[offset];
//	printk(KERN_INFO "DEBUG: Page with offset no. %lu is allocated\n", offset);
//	printk(KERN_INFO "DEBUG: vma fault pid = %d\n", current->pid);
    return 0;
}

static void vm_open(struct vm_area_struct *vma)
{
//	printk(KERN_INFO "DEBUG: vm_open called\n");
//	printk(KERN_INFO "DEBUG: vma opened pid = %d\n", current->pid);
}

static struct vm_operations_struct vm_ops =
{
    .close = vm_close,
    .fault = vm_fault,
    .open = vm_open,
};

unsigned long get_num_vmpages(unsigned long start, unsigned long end)
{
	int rem;
	unsigned long num_pages;
	num_pages = (end-start)/PAGE_SIZE;
	rem = (end-start) - num_pages * PAGE_SIZE;
	if(rem>0)
		num_pages++;
	return num_pages;
}

static int pmap_mmap(struct file *filp, struct vm_area_struct *vma)
{
	unsigned long i;
	struct vm_tree *vmnode = kmalloc(sizeof(struct vm_tree), GFP_KERNEL);
	printk(KERN_INFO "mmap called\n");
	sprintf(vmnode->id,"%s*%lu", current->comm, vma->vm_start);
	vmnode->num_vmpages = get_num_vmpages(vma->vm_start, vma->vm_end);
	vmnode->pg = kmalloc(vmnode->num_vmpages * sizeof(struct page*), GFP_KERNEL);
	for(i=0; i<vmnode->num_vmpages; i++){
		vmnode->pg[i] = NULL;
	}
	vm_insert(&vm_tree, vmnode);
	printk(KERN_INFO "DEBUG: Mapping %lu persistent pages\n", vmnode->num_vmpages);
    vma->vm_ops = &vm_ops;
    vma->vm_flags |= VM_DONTEXPAND;
	// vma->vm_file = filp;
	vma->vm_pgoff = 0;
    vm_open(vma);
    return 0;
}

static struct file_operations fops = {
        .open = pmap_open,
        .release = pmap_release,
        .mmap = pmap_mmap,
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
    printk(KERN_INFO "'sudo mknod /dev/%s c %d 0'.\n", DEVNAME, DEV_MAJOR);  
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
