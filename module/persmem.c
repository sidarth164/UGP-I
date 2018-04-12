#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/moduleparam.h>

#include <linux/timer.h>

#include <linux/gfp.h>
#include <linux/buffer_head.h>
#include <linux/rbtree.h>
#include <linux/page-flags.h>
#include <linux/page_ref.h>

#include <linux/fs.h>
#include <linux/string.h>
#include <asm/uaccess.h>
#include <asm/segment.h>

#define MAX_FILE_NAME 100
#define TRUE true
#define FALSE false
#define METADATA_SIZE 64

#define PM_ASSERT(x) do{\
                         if(!(x)) { \
                           printk(KERN_INFO "Assert failed! %s:%s at %d\n", __FILE__, __func__, __LINE__);\
						   WARN_ON(!x); \
                         }\
				     }while(0);		  

unsigned int log_2(long long int x)
{
	unsigned int i=0;
	while(x){
		x=x/2;
		i++;
	}
	return i-1;
}

unsigned long exp_2(unsigned int i)
{
	unsigned long exp = 1;
	while(i){
		exp*=2;
		i--;
	}
	return exp;
}

struct backupfile{
	char name[MAX_FILE_NAME];
	unsigned long page_count;
	struct file *fp;
};

struct free_list {
	struct page *pg;
	unsigned long offs_no;
	struct free_list *next, *prev;
};

struct pmem{
    struct backupfile *bf;
	struct free_list *head;
	struct rb_root mytree;
};

struct used_tree {
	struct rb_node node;
	struct page *pg;
	unsigned long offs_no;
	unsigned long pfn;
	char id[17];
};

static char* bf_name = "backup";
static long backup_interval = 300000;

static struct timer_list backup_timer;

struct pmem* p_area;

struct pmem* _pmem(void) 
{
	struct pmem* p = kmalloc(sizeof(struct pmem), GFP_KERNEL);
	printk(KERN_INFO "DEBUG: Initializing p_area\n");
	p->bf = kmalloc(sizeof(struct backupfile), GFP_KERNEL);
	p->head = kmalloc(sizeof(struct free_list), GFP_KERNEL);
	p->head->next=NULL;
	p->head->prev=NULL;
	p->head->pg=NULL;
	p->mytree = RB_ROOT;
	return p;
}

struct used_tree *used_search(struct rb_root *root, unsigned long pfn)
{
   	struct rb_node *node = root->rb_node;

	while (node) {
 		struct used_tree *unode = rb_entry(node, struct used_tree, node);

		if (pfn < unode->pfn)
			node = node->rb_left;
		else if (pfn > unode->pfn)
 			node = node->rb_right;
		else
			return unode;
	}
	return NULL;
}

bool used_insert(struct rb_root *root, struct used_tree *data)
{
  	struct rb_node **new = &(root->rb_node), *parent = NULL;
	unsigned long pfn = data->pfn;

  	/* Figure out where to put new node */
  	while (*new) {
 		struct used_tree *this = rb_entry(*new, struct used_tree, node);
		parent = *new;

		if (pfn < this->pfn)
 			new = &((*new)->rb_left);
  		else if (pfn > this->pfn)
			new = &((*new)->rb_right);
 		else
 			return FALSE;
	}

	/* Add new node and rebalance tree. */
 	rb_link_node(&data->node, parent, new);
 	rb_insert_color(&data->node, root);
	return TRUE;
}

struct file *file_open(const char *path, int flags, int rights) 
{
    struct file *filp = NULL;
    mm_segment_t oldfs;
    int err = 0;

    oldfs = get_fs();
    set_fs(get_ds());
    filp = filp_open(path, flags, rights);
    set_fs(oldfs);
    if (IS_ERR(filp)) {
        err = PTR_ERR(filp);
		printk(KERN_INFO "Error in opening the file\n");
        return NULL;
    }

    printk(KERN_INFO "DEBUG: file open successfull\n");
    return filp;
}

void file_close(struct file *file) 
{
    printk(KERN_INFO "DEBUG: Closing backup file...\n");
    filp_close(file, NULL);
    printk(KERN_INFO "DEBUG: file close successfull\n");
}

int file_read(struct file *file, unsigned long long offset, unsigned char *data, unsigned int size) 
{
    mm_segment_t oldfs;
    int ret;

    oldfs = get_fs();
    set_fs(get_ds());

    ret = kernel_read(file, data, size, &offset);

    set_fs(oldfs);
    return ret;
}   

int file_write(struct file *file, unsigned long long offset, unsigned char *data, unsigned int size) 
{
    mm_segment_t oldfs;
    int ret;
	
    oldfs = get_fs();
    set_fs(get_ds());
	
    ret = kernel_write(file, data, size, &offset);

    set_fs(oldfs);
 
    return ret;
}

void get_freepages(void) 
{
	struct page* pg;
	long i=p_area->bf->page_count-1;
	pg = alloc_pages(GFP_KERNEL, 0);
	p_area->head->pg=pg;
	p_area->head->offs_no=i;
	i--;
	SetPagePersistent(pg);
	if(!pg)
		printk(KERN_INFO "Error in allocating page\n");
	
	for(;i>=0;i--) {
		struct free_list* l = kmalloc(sizeof(struct free_list), GFP_KERNEL);
	    pg = alloc_pages(GFP_KERNEL, 0);
		SetPagePersistent(pg);
		if(!pg)	
			printk(KERN_INFO "Error in allocating page\n");
		l->pg = pg;
		l->offs_no = i;
		l->next = p_area->head;
		l->prev = NULL;
		p_area->head->prev = l;
		p_area->head = l;
	}
}

void freepages(void)
{
	long i=0;
	printk(KERN_INFO "DEBUG: Freeing pages\n");
	while(p_area->head)	{
		struct free_list* l;
		unsigned long offs_no = p_area->head->offs_no;
		i++;
	//	printk(KERN_INFO "DEBUG: freeing page %lu\n", offs_no);

		if(p_area->head->pg) {
			ClearPagePersistent(p_area->head->pg);
			__free_pages(p_area->head->pg,0);
		}
		else{
			printk(KERN_INFO "[%lu] No page to free!!\n", offs_no);
			return;
		}
		
		l = p_area->head;
		p_area->head=p_area->head->next;
		if(p_area->head) {
			p_area->head->prev=NULL;
		}
		if(l) {
			kfree(l);
		}
		else
			printk(KERN_INFO "[%lu] Problem in removing this node\n",offs_no);
	}

	printk(KERN_INFO "DEBUG: Total freed pages - %lu\n", i);
}

struct free_list *get_page_from_freelist (void)
{
	struct free_list *l = p_area->head;
	if(p_area->head)
		p_area->head = p_area->head->next;
	if(p_area->head)
		p_area->head->prev = NULL;
	if(l){
		return l;
	}
	return NULL;
}

bool add_page_to_freelist (struct page *page, unsigned long offs_no)
{
	struct free_list *l = kmalloc(sizeof(struct free_list), GFP_KERNEL);
	if(!page) {
		printk(KERN_INFO "No page to add to free list\n");
		return FALSE;
	}
	
	l->pg = page;
	l->offs_no = offs_no;
	l->next = p_area->head;
	l->prev = NULL;
	if(p_area->head)
		p_area->head->prev = l;
	p_area->head = l;
	return TRUE;
}

void restore_state(void)
{
	char meta[64], isfree[5]="free";
	struct free_list *l = p_area->head;
	printk(KERN_INFO "DEBUG: Restoring the state of the persistent memory, Copying data from backup to pages\n");
	if(p_area->head==NULL){
		printk(KERN_INFO "p_area->head is null\n");
		return;
	}
	while(l) {
		file_read(p_area->bf->fp, l->offs_no * (PAGE_SIZE+METADATA_SIZE), isfree, 4);
		isfree[4]='\0';
		if(isfree==NULL){
			/* page is free */
			strcpy(meta,"free");
			file_write(p_area->bf->fp, l->offs_no * (PAGE_SIZE+METADATA_SIZE), meta, METADATA_SIZE);
		}
		else if(strcmp(isfree,"used")){
			/* page is free */	
			strcpy(meta,"free");
			file_write(p_area->bf->fp, l->offs_no * (PAGE_SIZE+METADATA_SIZE), meta, METADATA_SIZE);
		}
		else{
			/* page is used 
			 * transferring this used page from free list to used tree
			 */

			struct used_tree *unode = kmalloc(sizeof(struct used_tree), GFP_KERNEL);
			struct free_list *used=l;
			bool res;

			printk(KERN_INFO "DEBUG: page type: %s\n", isfree);
			file_read(p_area->bf->fp, l->offs_no * (PAGE_SIZE+METADATA_SIZE), meta, METADATA_SIZE);
			file_read(p_area->bf->fp, l->offs_no * (PAGE_SIZE+METADATA_SIZE) + METADATA_SIZE, (char*) page_address(l->pg), PAGE_SIZE);

			unode->pg=l->pg;
			unode->offs_no = l->offs_no;
			unode->pfn = page_to_pfn(l->pg);
			strcpy(unode->id,meta+4);
			res = used_insert(&p_area->mytree, unode);

			if(res==TRUE)
				printk(KERN_INFO "DEBUG: Successfully restored used page %lu with id %s\n", unode->pfn, unode->id);
			else
				printk(KERN_INFO "Error in restoration of page %lu with id %s\n", unode->pfn, unode->id);
			
			if(l->prev)
				l->prev->next = l->next;
			if(l->next)
				l->next->prev = l->prev;
			if(l==p_area->head)
				p_area->head = p_area->head->next;
			l = l->next;
			kfree(used);
			continue;
		}
		l = l->next;
	}
	printk(KERN_INFO "DEBUG: Restoration successfull\n");
}

struct page *palloc_pages(unsigned int order, char id[17])
{
	struct used_tree *unode = kmalloc(sizeof(struct used_tree), GFP_KERNEL);
	struct rb_node *node;
	struct page *pg;
	struct free_list *l;
	bool res;
	char meta[64]="used";
	
	/* Checking if there exists a used page with given id */

	for (node = rb_first(&p_area->mytree); node; node = rb_next(node)){
		struct used_tree *unode = rb_entry(node, struct used_tree, node);
		if(!strcmp(unode->id, id)){
			pg=unode->pg;
			printk(KERN_INFO "DEBUG: Page with id %s already present in used tree\n", id);
			return pg;
		}
	}

	/* Since there is no such page in used-tree structure */	
	strcat(meta,id);

	/* allocate free pages (currently only 1) from the persistent region */
	printk(KERN_INFO "DEBUG: palloc_pages() called\n");

	l = get_page_from_freelist();
	pg=l->pg;

	unode->pg = pg;
	unode->offs_no = l->offs_no;
	unode->pfn = page_to_pfn(pg);
	strcpy(unode->id,id);
	res = used_insert(&p_area->mytree, unode);

	file_write(p_area->bf->fp, unode->offs_no * (PAGE_SIZE+METADATA_SIZE), meta, METADATA_SIZE);

	if(res == TRUE)
		printk(KERN_INFO "DEBUG: [%lu] Successfully transferred page %lu from free list to used tree\nrefcount = %d\n", unode->offs_no, unode->pfn, page_ref_count(pg));
	else
		printk(KERN_INFO "[%lu] Transfer of page %lu from free list to used tree was UNSUCCESSFULL\n", unode->offs_no, unode->pfn);
	
	kfree(l);
	return pg;
}
EXPORT_SYMBOL(palloc_pages);

void pfree_pages(struct page *page, unsigned int order)
{
	struct used_tree *unode;
	struct page *pg;
	unsigned long offs_no;
	bool res;
	char meta[64]="free";

	/* free pages from the persistent region */
	printk(KERN_INFO "DEBUG: pfree_pages() called\n");

	unode = used_search(&p_area->mytree, page_to_pfn(page));

	// PM_ASSERT(unode!=NULL);
	if(!unode) {
		printk(KERN_INFO "Page %lu not found in used list\n", page_to_pfn(page));
		return;
	}

	file_write(p_area->bf->fp, unode->offs_no * (PAGE_SIZE+METADATA_SIZE), meta, METADATA_SIZE);

	pg = unode->pg;
	offs_no = unode->offs_no;
	rb_erase(&unode->node,&p_area->mytree);
	kfree(unode);

	res = add_page_to_freelist(pg,offs_no);
	PM_ASSERT(res==true);
	if(res == FALSE) {
		printk(KERN_INFO "Error in adding page %lu to free list\n", page_to_pfn(page));
		return;
	}

	printk(KERN_INFO "DEBUG: [%lu] Successfully transferred page %lu from used tree to free list\n", offs_no, page_to_pfn(page));
	return;
}
EXPORT_SYMBOL(pfree_pages);

void take_backup(void){
	/* copy contents of *used* memory to backup file*/
	struct rb_node *node;
	printk(KERN_INFO "DEBUG: Taking backup of used memory pages into backup file\n");

	for (node = rb_first(&p_area->mytree); node; node = rb_next(node)){
		struct used_tree *unode = rb_entry(node, struct used_tree, node);
		if(TestClearPageDirty(unode->pg)){
			printk(KERN_INFO "DEBUG: [%lu]page: %lu is dirty\n", unode->offs_no, unode->pfn);
			file_write(p_area->bf->fp, unode->offs_no * (PAGE_SIZE+METADATA_SIZE) + METADATA_SIZE, (char*) page_address(unode->pg), PAGE_SIZE);
		}
		else
			printk(KERN_INFO "DEBUG: [%lu]page: %lu is NOT dirty\n", unode->offs_no, unode->pfn);
	}
	
	printk(KERN_INFO "DEBUG: Taking backup successfull\n");
}
EXPORT_SYMBOL(take_backup);

void backup_timer_callback( struct timer_list *t )
{
	/* take_backup() is called every backup_interval milliseconds */
	printk(KERN_INFO "DEBUG: backup_timer_callback() function called\n");
    mod_timer(t, jiffies + msecs_to_jiffies(backup_interval));

	take_backup();
}

static int __init pmem_init(void)
{
	printk(KERN_INFO "Setting up Persistent Aware Kernel Module\n");
    printk(KERN_INFO "module parameters are - %s, %ld\n",bf_name,backup_interval);
  	
	/* setup your timer to call backup_timer_callback */
    timer_setup(&backup_timer, &backup_timer_callback, 0);
  	/* setup timer interval to 1 hour*/
    mod_timer(&backup_timer, jiffies + msecs_to_jiffies(backup_interval));
	
	/* Initializing persistent area */
	p_area = _pmem();

	p_area->bf->fp = file_open(bf_name, O_RDWR | O_CREAT, 0666);
	p_area->bf->page_count = exp_2(log_2(p_area->bf->fp->f_inode->i_size))/(PAGE_SIZE+METADATA_SIZE);
	strcpy(p_area->bf->name,bf_name);
	printk("DEBUG: Size of file %s : %lld bytes, %lu pages", p_area->bf->name, p_area->bf->fp->f_inode->i_size,
	p_area->bf->page_count);

	if(p_area->bf->page_count<1){
		printk(KERN_INFO "Insufficient file size\n");
		return 1;
	}

	get_freepages();

	/* Restoring persistent area state */
	printk(KERN_INFO "DEBUG: Calling restore_state()\n");
	restore_state();
	printk(KERN_INFO "DEBUG: Returned from restore_state()\n");
	
	pmem_alloc = palloc_pages;
	pmem_free = pfree_pages;

	return 0;
}

static void __exit pmem_exit(void)
{
	printk(KERN_INFO "Exiting Persistent Aware Kernel Module\n");
	/* remove kernel timer when unloading module */
	del_timer(&backup_timer);

	/* taking backup */
	take_backup();

	/* close the backup file when removing the module */
	file_close(p_area->bf->fp);

	/* freeing the pages which we fetched for our module */
	freepages();

	pmem_alloc = NULL;
	pmem_free = NULL;

	/* freeing p_area */
	kfree(p_area->bf);
	kfree(p_area);
}

module_param(bf_name, charp, 0000);
MODULE_PARM_DESC(bf_name, "backup file name");
module_param(backup_interval, long, S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH);
MODULE_PARM_DESC(backup_interval, "Backup interval time");

module_init(pmem_init);
module_exit(pmem_exit);

MODULE_LICENSE("GPL");

/* Who wrote this module? */
MODULE_AUTHOR("Siddharth Agrawal");
/* What does this module do */
MODULE_DESCRIPTION("Module to make OS persistent aware");
