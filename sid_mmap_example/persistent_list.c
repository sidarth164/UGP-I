#define _GNU_SOURCE
#include<stdio.h>
#include<stdlib.h>
#include<string.h>
#include<sys/fcntl.h>
#include<signal.h>
#include<sys/ioctl.h>
#include<sys/mman.h>
#include <sys/syscall.h>

#include "syschar.h"

#define MAX_LIST_SIZE 1024	// Maximum number of nodes the list can contain
#define PAGE_SIZE 4096
#define HEAD NULL
#define P_NODE_SIZE 20

// STRUCTURES
struct free_mem_list {
	void *mem;
	struct free_mem_list *next, *prev;
};

struct p_node {
	char *data;
	int key;
	struct p_node *next;
};

// GLOBAL VARIABLES
struct p_node *head = HEAD;
void *pmem;
struct free_mem_list *free_head = HEAD;

// FUNCTION PROTOTYPES
struct free_mem_list* create_free_memlist(void *memory, int size);
void* get_free_memory(void);
void add_free_memlist(void *memory);
void add_pnode(char* data, int key);
void delete_pnode(int key);
struct p_node* search_pnode(int key);
void flush_list(void);
void free_freememlist(void);
void restore_list(void);

// MAIN
int main()
{
	char* meta;
	char isPers[7];
	struct p_node *search;
	int fd = open("/dev/pmap", O_RDWR);
	if(fd<0){
		perror("Error in opening character device /dev/pmap\n");
		exit(-1);
	}
	
	printf("Persistent Linked List Demo\n");

	pmem = mmap((void*) 55555555, MAX_LIST_SIZE * PAGE_SIZE, PROT_READ|PROT_WRITE, MAP_SHARED, fd, 0);
	if(pmem == MAP_FAILED){
		perror("mmap failed\n");
		exit(-1);
	}
	else
		printf("mmap successfull\n");

	meta = (char*)pmem;
	memcpy(isPers, meta, 7);
	printf("isPers = %s\n", isPers);

	if(!strcmp(isPers, "pmlist")){
		printf("\nList is already present in the memory\n\n");
		head = (struct p_node *)(pmem +7);
		
		restore_list();

		printf("\nSearching Node with key 6\n");
		search = search_pnode(6);
		if(search)
			printf("Node 6 found in the list!!\nIt has the following data\n%s\n", search->data);
		else
			printf("Node 6 not found in the list\n");
	
		printf("\nSearching Node with key 2\n");
		search = search_pnode(2);
		if(search)
			printf("Node 2 found in the list!!\nIt has the following data\n%s\n", search->data);
		else
			printf("Node 2 not found in the list\n");
	
		printf("\nSearching Node with key 19\n");
		search = search_pnode(19);
		if(search)
			printf("Node 19 found in the list!!\nIt has the following data\n%s\n", search->data);
		else
			printf("Node 19 not found in the list\n");

		flush_list();
		free_freememlist();
		return 0;
	}
	else {
		printf("\nList is not present in the memory\n\n");
		memcpy(meta, "pmlist", 7);
		printf("Metadata added successfully\n");
		/* Making new linked list */
		head = (struct p_node *)(pmem + 7);
		// Data available for head = 4096-7-20-8 = 4061 bytes
		// Data available for normal nodes = 4096-20-8 = 4068 bytes
		head->key = 0;
		head->next = NULL;
		head->data = (char *)(pmem + 6 + P_NODE_SIZE + sizeof(struct p_node *));
		memcpy(head->data, "This is the head", strlen("This is the head")+1);
		printf("Linked list with a single node - head, made successfully\n");
	
		free_head = create_free_memlist(pmem + PAGE_SIZE, MAX_LIST_SIZE-1);
		printf("Free list for storing free memory created successfully\n");

		// printf("Adding a node with key = 1\n");
		add_pnode("This is the second node, its key is 1",1);
		// printf("%s\n", head->next->data);
	
		// printf("Deleting the node with key = 1\n");
		delete_pnode(1);
	
		// printf("Adding a node with key = 8\n");
		add_pnode("Dummy node #1",8);
	
		// printf("Adding a node with key = 10\n");
		add_pnode("Dummy node #2",10);
	
		// printf("Adding a node with key = 6\n");
		add_pnode("This node is used to check search", 6);
	
		// printf("Adding a node with key = 2\n");
		add_pnode("Dummy node #3",2);
	
		// printf("Adding a node with key = 4\n");
		add_pnode("Dummy node #4",4);
	
		// printf("Adding a node with key = 19\n");
		add_pnode("Dummy node #5",19);
	
		printf("\nSearching Node with key 6\n");
		search = search_pnode(6);
		if(search)
			printf("Node 6 found in the list!!\nIt has the following data\n%s\n", search->data);
		else
			printf("Node 6 not found in the list\n");
	
		printf("\nSearching Node with key 2\n");
		search = search_pnode(2);
		if(search)
			printf("Node 2 found in the list!!\nIt has the following data\n%s\n", search->data);
		else
			printf("Node 2 not found in the list\n");
	
		printf("\nSearching Node with key 19\n");
		search = search_pnode(19);
		if(search)
			printf("Node 19 found in the list!!\nIt has the following data\n%s\n", search->data);
		else
			printf("Node 19 not found in the list\n");
	
		flush_list();
		free_freememlist();
		return 0;
	}
}

//FUNCTION DEFINITIONS
struct free_mem_list* create_free_memlist(void *memory, int size)
{
	struct free_mem_list *head, *it;
	int i;
	char* buf;
	head = (struct free_mem_list*) malloc(sizeof(struct free_mem_list));
	head->mem = memory;
	head->next = NULL;
	head->prev = NULL;
	it = head;
	memory += 4096;
	for(i=1;i<size;i++){
		struct free_mem_list *temp = (struct free_mem_list *) malloc(sizeof(struct free_mem_list));
		temp->mem = memory;
		temp->next = NULL;
		temp->prev = it;
		buf = (char*) memory;
		memcpy(buf, "This is free", strlen("This is free")+1);
		it->next = temp;
		it = temp;
		memory += 4096;
	}

	return head;
}

void* get_free_memory(void)
{
	struct free_mem_list *tmp = free_head;
	if(tmp == NULL){
		perror("No more free memory\n");
		exit(-1);
	}

	void* mem = tmp->mem;
	
	free_head = free_head->next;
	free_head->prev = NULL;
	free(tmp);
	
	return mem;
}

void add_free_memlist(void *memory)
{
	struct free_mem_list* tmp = (struct free_mem_list*) malloc(sizeof(struct free_mem_list));
	char* buf;
	tmp->mem = memory;
	tmp->next = free_head;
	buf = (char*) memory;
	memcpy(buf, "This is free", strlen("This is free")+1);
	tmp->prev = NULL;
	if(free_head)
		free_head->prev = tmp;
	free_head = tmp;
}

void add_pnode(char* data, int key)
{
	struct p_node *node, *it;
	void *memory = get_free_memory();

	node = (struct p_node*)memory;
	node->key = key;
	node->next = NULL;
	node->data = (char*)(memory + P_NODE_SIZE + sizeof(struct p_node *));

	memcpy(node->data, data, strlen(data)+1);
//	printf("Added node %d\nIt has the following data\n%s\n", node->key, node->data);

	it = head;
	while(it->next){
		it = it->next;
	}

	it->next = node;
}

void delete_pnode(int key)
{
	struct p_node *node = head;
	struct p_node *prev_node = NULL;
	void *mem;
	while(node){
		if(node->key == key){
			/* deleting the node by sending the memory  back to free list */
			mem = (void*)node;
			printf("Deleting node %d\nIt has the following data\n%s\n", node->key, node->data);
			
			if(prev_node)
				prev_node->next = node->next;
			else
				head = node->next;
	
			add_free_memlist(mem);
			return;
		}
		prev_node = node;
		node = node->next;
	}
	printf("Node %d not found in the list\n", key);
}

struct p_node* search_pnode(int key)
{
	struct p_node *node = head;
	while(node){
		if(node->key == key){
			/* Search for the node was successfull */
			printf("node %d successfully found\nIt has the following data\n%s\n", node->key, node->data);
	
			return node;
		}
		node = node->next;
	}
	printf("Did not find node %d in the list\n", key);
	return NULL;
}

void flush_list()
{
	struct p_node* node = head->next;
	void *mem;
	printf("\nFlushing all the nodes in the linked list (except the head)\n");
	while(node){
		mem = (void*)node;
		printf("Deleting node %d\nIt has the following data\n%s\n", node->key, node->data);
		
		node = node->next;

		add_free_memlist(mem);
	}
}

void free_freememlist(){
	struct free_mem_list* tmp;
	char* buf = (char*) pmem;
	printf("freeing the free_mem_list\n");
	while(free_head){
		tmp = free_head;
		free_head = free_head->next;
		free(tmp);
	}

	memcpy(buf, "pmfree", 7);
	munmap(pmem, MAX_LIST_SIZE * PAGE_SIZE);
}

void restore_list()
{
	void *memory = pmem + PAGE_SIZE;
	int i;
	char*buf;
	char isFree[13];
	for(i=0;i<MAX_LIST_SIZE-1;i++){
		buf = (char*) memory;
		memcpy(isFree, buf, 13);
		if(!strcmp(isFree, "This is free"))
			add_free_memlist(memory);
		
		memory += PAGE_SIZE;
	}
}
