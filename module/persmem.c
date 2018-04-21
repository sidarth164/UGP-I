#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/moduleparam.h>

#include <linux/timer.h>
#include <linux/delay.h>
#include <linux/sched.h>
#include <linux/fs.h>
#include <linux/string.h>

#include <asm/uaccess.h>
#include <asm/segment.h>
#include <asm/pgtable.h>
#include <asm/tlbflush.h>

#include <linux/gfp.h>
#include <linux/buffer_head.h>
#include <linux/rbtree.h>
#include <linux/page-flags.h>
#include <linux/page_ref.h>
#include <linux/kthread.h>
#include <linux/rmap.h>
#include <linux/huge_mm.h>

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
	struct mm_struct *mm;
	bool isDirty;
	unsigned long num_reads;
	unsigned long num_writes;
	char id[55];
};

static char* bf_name = "backup";
static long backup_interval = 900;	// In seconds
static long account_interval = 50;	// In milliseconds
static unsigned long counter=0;

struct task_struct *pthread;

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

//  printk(KERN_INFO "DEBUG: file open successfull\n");
    return filp;
}

void file_close(struct file *file) 
{
//	printk(KERN_INFO "DEBUG: Closing backup file...\n");
	filp_close(file, NULL);
//	printk(KERN_INFO "DEBUG: file close successfull\n");
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
	struct rb_node *node = rb_first(&p_area->mytree);
//	printk(KERN_INFO "DEBUG: Freeing pages\n");

	while(p_area->head)	{
		struct free_list* l;
		unsigned long offs_no = p_area->head->offs_no;

		if(p_area->head->pg) {
			ClearPagePersistent(p_area->head->pg);
			p_area->head->pg->mapping = NULL;
			__free_pages(p_area->head->pg,0);
			i++;
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

	while(node){
		struct used_tree *unode = rb_entry(node, struct used_tree, node);
		if(unode->pg){
			ClearPagePersistent(unode->pg);
			__free_pages(unode->pg,0);
			i++;
		}
		else{
			printk(KERN_INFO "[%lu] No page to free!!, id=%s\n", unode->offs_no, unode->id);
			return;
		}
		rb_erase(&unode->node,&p_area->mytree);
		kfree(unode);
		node=rb_first(&p_area->mytree);
	}

//	printk(KERN_INFO "DEBUG: Total freed pages - %lu\n", i);
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

	page->mapping = NULL;	
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
	int ret, count = 0;
	int read;
	struct free_list *l = p_area->head;
	printk(KERN_INFO "DEBUG: Restoring the state of the persistent memory, Copying data from backup to pages\n");
	if(p_area->head==NULL){
		printk(KERN_INFO "p_area->head is null\n");
		return;
	}
	while(l) {
		read = file_read(p_area->bf->fp, l->offs_no * (PAGE_SIZE+METADATA_SIZE), isfree, 4);
		isfree[4]='\0';
//		printk(KERN_INFO "DEBUG: File Read result %d\n",read);
		if(isfree==NULL){
			/* page is free */
			strcpy(meta,"free");
			ret = file_write(p_area->bf->fp, l->offs_no * (PAGE_SIZE+METADATA_SIZE), meta, METADATA_SIZE);
		}
		else if(strcmp(isfree,"used")){
			/* page is free */	
			strcpy(meta,"free");
			ret = file_write(p_area->bf->fp, l->offs_no * (PAGE_SIZE+METADATA_SIZE), meta, METADATA_SIZE);
		}
		else{
			/* page is used 
			 * transferring this used page from free list to used tree
			 */

			struct used_tree *unode = kmalloc(sizeof(struct used_tree), GFP_KERNEL);
			struct free_list *used=l;
			bool res;

			file_read(p_area->bf->fp, l->offs_no * (PAGE_SIZE+METADATA_SIZE), meta, METADATA_SIZE);
			file_read(p_area->bf->fp, l->offs_no * (PAGE_SIZE+METADATA_SIZE) + METADATA_SIZE, (char*) page_address(l->pg), PAGE_SIZE);

			unode->pg=l->pg;
			unode->offs_no = l->offs_no;
			unode->pfn = page_to_pfn(l->pg);
			unode->mm = NULL;
			unode->isDirty = false;
			unode->num_reads = 0;
			unode->num_writes = 0;
			strcpy(unode->id,meta+4);
			res = used_insert(&p_area->mytree, unode);

//			if(res==TRUE)
//				printk(KERN_INFO "DEBUG: Successfully restored used page %lu with id %s\n", unode->pfn, unode->id);
//			else
//				printk(KERN_INFO "Error in restoration of page %lu with id %s\n", unode->pfn, unode->id);
			
			if(l->prev)
				l->prev->next = l->next;
			if(l->next)
				l->next->prev = l->prev;
			if(l==p_area->head)
				p_area->head = p_area->head->next;
			l = l->next;
			kfree(used);
			count++;
			continue;
		}
		l = l->next;
	}
	printk(KERN_INFO "DEBUG: Restoration successfull: %d used pages restored\n", count);
//	printk(KERN_INFO "DEBUG: File write result %d\n", ret);
}

struct page *palloc_pages(unsigned int order, char id[55], struct mm_struct *mm)
{
	struct used_tree *unode; 
	struct rb_node *node;
	struct page *pg;
	struct free_list *l;
	bool res;
	int ret;
	char meta[64]="used";
	
	/* Checking if there exists a used page with given id */

	for (node = rb_first(&p_area->mytree); node; node = rb_next(node)){
		struct used_tree *unode = rb_entry(node, struct used_tree, node);
		if(!strcmp(unode->id, id)){
			pg=unode->pg;
			unode->mm = mm;
			unode->isDirty = false;
			unode->num_reads = 0;
			unode->num_writes = 0;
//			printk(KERN_INFO "DEBUG: Page with id %s already present in used tree\n", id);
			return pg;
		}
	}

	/* Since there is no such page in used-tree structure */	
	strcat(meta,id);
	unode = kmalloc(sizeof(struct used_tree), GFP_KERNEL);

	/* allocate free pages (currently only 1) from the persistent region */
//	printk(KERN_INFO "DEBUG: palloc_pages() called, id=%s\n", id);

	l = get_page_from_freelist();
	pg=l->pg;

	unode->pg = pg;
	unode->offs_no = l->offs_no;
	unode->pfn = page_to_pfn(pg);
	unode->mm = mm;
	unode->isDirty = false;
	unode->num_reads = 0;
	unode->num_writes = 0;
	strcpy(unode->id,id);
	res = used_insert(&p_area->mytree, unode);

	ret = file_write(p_area->bf->fp, unode->offs_no * (PAGE_SIZE+METADATA_SIZE), meta, METADATA_SIZE);
	// printk(KERN_INFO "DEBUG: File write result %d\n", ret);

	if(res != TRUE)
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
	int ret;
	char meta[64]="free";

	/* free pages from the persistent region */
//	printk(KERN_INFO "DEBUG: pfree_pages() called\n");

	unode = used_search(&p_area->mytree, page_to_pfn(page));

	// PM_ASSERT(unode!=NULL);
	if(!unode) {
		printk(KERN_INFO "Page %lu not found in used list\n", page_to_pfn(page));
		return;
	}

	ret = file_write(p_area->bf->fp, unode->offs_no * (PAGE_SIZE+METADATA_SIZE), meta, METADATA_SIZE);
//	printk(KERN_INFO "DEBUG: File write result %d\n", ret);

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

//	printk(KERN_INFO "DEBUG: [%lu] Successfully transferred page %lu from used tree to free list\n", offs_no, page_to_pfn(page));
	return;
}
EXPORT_SYMBOL(pfree_pages);

void flush_used_pages(void)
{
	struct rb_node *node = rb_first(&p_area->mytree);
	struct page *pg;
	unsigned long offs_no;
	bool res;
	int ret;
	char meta[64]="free";

	while(node){
		struct used_tree *unode = rb_entry(node, struct used_tree, node);

		if(!unode) {
			printk(KERN_INFO "Node not found\n");
			return;
		}
		ret = file_write(p_area->bf->fp, unode->offs_no * (PAGE_SIZE+METADATA_SIZE), meta, METADATA_SIZE);
//	printk(KERN_INFO "DEBUG: File write result %d\n", ret);
	
		pg = unode->pg;
		offs_no = unode->offs_no;
		rb_erase(&unode->node,&p_area->mytree);
		kfree(unode);
	
		res = add_page_to_freelist(pg,offs_no);
		PM_ASSERT(res==true);
		if(res == FALSE) {
			printk(KERN_INFO "Error in adding page %lu to free list\n", page_to_pfn(pg));
			return;
		}
	
//		printk(KERN_INFO "DEBUG: [%lu] Successfully transferred page %lu from used tree to free list\n", offs_no, page_to_pfn(pg));
		node = rb_first(&p_area->mytree);
	}
}	
EXPORT_SYMBOL(flush_used_pages);

void do_accounting(struct used_tree *unode){
	/* For the given unode's page; check the access and dirty bits in the pte */
	struct page* page= unode->pg;
	// struct mm_struct *mm;
	char *id = unode->id;
	char c;
	long unsigned st_addr=0, offs=0, virt_addr;
	pgd_t *pgd;
	p4d_t *p4d;
	pud_t *pud;
	pmd_t *pmd;
	pte_t *ptep, pte;
	struct page* pg;
//	struct vm_area_struct *vma;

	if(unode->mm == NULL){
		return;
	}

	for(c=*id; c!='*' && c!='\0'; c=*(++id)); // Reading the process name part of the id
	
	for(c=*(++id); c!='*' && c!='\0'; c=*(++id)){
		/* Reading the vm_start address for this page */
		st_addr*=10;
		st_addr+=c-'0';
	}
	
	for(c=*(++id); c!='\0'; c=*(++id)){
		/* Reading the offset in vma for this page */
		offs*=10;
		offs+=c-'0';
	}
	
	if(offs != page->index){
//		printk(KERN_INFO "Wrong Info passed to accounting function\n");
		return;
	}
	
	virt_addr = st_addr + (offs << PAGE_SHIFT);

	pgd = pgd_offset(unode->mm, virt_addr);
	if (pgd_none(*pgd) || pgd_bad(*pgd))
		return;

	p4d = p4d_offset(pgd, virt_addr);
	if (p4d_none(*p4d) || p4d_bad(*p4d))
		return;

	pud = pud_offset(p4d, virt_addr);
	if (pud_none(*pud) || pud_bad(*pud))
		return;

	pmd = pmd_offset(pud, virt_addr);
	if (pmd_none(*pmd) || pmd_bad(*pmd))
		return;

	ptep = pte_offset_kernel(pmd, virt_addr);
	if(!ptep)
		return;
	
	pte = *ptep;
	
	if(!pte_present(pte)){
//		printk(KERN_INFO "DEBUG: No Page present for this pte, something went wrong\n");
		return;
	}

	pg = pte_page(pte);
	if(pg!= page){
//		printk(KERN_INFO "DEBUG: Wrong pte\n");
		return;
	}
	
//	for(vma= unode->mm->mmap; vma; vma= vma->vm_next){
//		if(vma->vm_start == st_addr){
//			break;
//		}
//	}

	if(pte_young(pte) && pte_dirty(pte)){
		unode->isDirty = true;
		unode->num_writes++;
//		pte_mkold(pte);
//		pte_mkclean(pte);
        ptep->pte = ptep->pte & ~_PAGE_ACCESSED;
        ptep->pte = ptep->pte & ~_PAGE_DIRTY;
		// flush_tlb_page(vma, virt_addr);
//		printk(KERN_INFO "DEBUG: Page was written into\n");
	}
	else if(pte_young(pte)){
		unode->num_reads++;
        ptep->pte = ptep->pte & ~_PAGE_ACCESSED;
		//pte_mkold(pte);
		// flush_tlb_page(vma, virt_addr);
//		printk(KERN_INFO "DEBUG: Page was read\n");
	}
}

void take_backup(void){
	/* copy contents of *used* memory to backup file*/
	struct rb_node *node;
	unsigned long reads =0, writes =0;
	printk(KERN_INFO "DEBUG: Taking backup of used memory pages into backup file\n");

	for (node = rb_first(&p_area->mytree); node; node = rb_next(node)){
		struct used_tree *unode = rb_entry(node, struct used_tree, node);
		if(unode==NULL){
//			printk(KERN_INFO "Error: a node in used tree was null\n");
			return;
		}
		if(unode->isDirty || TestClearPageDirty(unode->pg)){
			file_write(p_area->bf->fp, unode->offs_no * (PAGE_SIZE+METADATA_SIZE) + METADATA_SIZE, (char*) page_address(unode->pg), PAGE_SIZE);
			reads +=unode->num_reads;
			writes += unode->num_writes;
			unode->isDirty = false;
			unode->num_reads = 0;
			unode->num_writes = 0;
		}
	}
	printk(KERN_INFO "DEBUG: Num_reads=%lu, Num_writes=%lu\n", reads, writes);

	printk(KERN_INFO "DEBUG: Taking backup successfull\n");
}
EXPORT_SYMBOL(take_backup);

static int pthread_run(void * nothing){
	struct rb_node *node;
	while(!kthread_should_stop()){
		counter++;
		
		for (node = rb_first(&p_area->mytree); node; node = rb_next(node)){
			struct used_tree *unode = rb_entry(node, struct used_tree, node);
            if(unode==NULL){
            	printk(KERN_INFO "Error: a node in used tree was null\n");
            	break;
            }
			do_accounting(unode);
		}
		
		if(counter*account_interval>backup_interval*1000){
			take_backup();
			counter=0;
		}
		msleep(account_interval);
	}

	printk(KERN_INFO "Stopping pthread\n");
	return 0;
}		

static int __init pmem_init(void)
{
	printk(KERN_INFO "Setting up Persistent Aware Kernel Module\n");
    printk(KERN_INFO "module parameters are - %s, %ld\n",bf_name,backup_interval);
  	
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
	restore_state();
	
	pmem_alloc = palloc_pages;
	pmem_free = pfree_pages;
	
	pthread = kthread_run(pthread_run, NULL, "pthread");
	if (IS_ERR(pthread)) {
                printk(KERN_INFO "Error: Creating kthread failed\n");
        }

	return 0;
}

static void __exit pmem_exit(void)
{
	int ret;
	printk(KERN_INFO "Exiting Persistent Aware Kernel Module\n");

	/* taking backup */
	take_backup();

	/* close the backup file when removing the module */
	if(p_area->bf->fp == NULL)
		return;
	file_close(p_area->bf->fp);

	/* Stop the pthread */
	ret = kthread_stop(pthread);
	if (ret != -EINTR){
		printk(KERN_INFO "DEBUG: Counter thread has stopped\n");
	}

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
module_param(account_interval, long, S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH);
MODULE_PARM_DESC(backup_interval, "Accounting interval time");

module_init(pmem_init);
module_exit(pmem_exit);

MODULE_LICENSE("GPL");

/* Who wrote this module? */
MODULE_AUTHOR("Siddharth Agrawal");
/* What does this module do */
MODULE_DESCRIPTION("Module to make OS persistent aware");
