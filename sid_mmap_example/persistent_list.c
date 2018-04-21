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

#define MAX_LIST_SIZE 32768	// Maximum number of nodes the list can contain (1024*32=32768)
#define PAGE_SIZE 4096
#define HEAD NULL
#define P_NODE_SIZE 24

// STRUCTURES
// struct free_mem_list {
// 	void *mem;
// 	struct free_mem_list *next, *prev;
// };

struct p_node {
	char *data;
	unsigned long key;
	struct p_node *next;
};

// GLOBAL VARIABLES
struct p_node *head = HEAD;
void *pmem;
void *free_head = HEAD;

// FUNCTION PROTOTYPES
//void create_free_memlist(void *memory, int size);
void* get_free_memory(void);
void add_free_memlist(void *memory);
void add_pnode(char* data, unsigned long key);
void delete_pnode(unsigned long key);
struct p_node* search_pnode(unsigned long key);
void modify_pnode(unsigned long key, char *data);
void flush_list(void);
// void free_freememlist(void);
void restore_list(void);

// MAIN
int main()
{
	unsigned long i,num;
	char* meta;
	char isPers[7];
	struct p_node *search = NULL;
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
	
	scanf("%lu", &num);

	meta = (char*)pmem;
	char * buf=(char*)pmem;
	memcpy(isPers, meta, 7);
	printf("isPers = %s\n", isPers);

	if(!strcmp(isPers, "pmlist")){
		printf("\nList is already present in the memory\n\n");
		head = (struct p_node *)(pmem +7);
		// memcpy(meta, "pmfree", 7);
		
		restore_list();

		for(i=1; i<num; i+=1){
			unsigned long index = rand() % 10000;
			if (index == 0) index++;
			char data[10];
			printf("\nSearching Node with key %lu\n", index);
			search = search_pnode(index);
			if(search){
				memcpy(data, search->data, 100);
				printf("Node %lu found in the list!!\nIt has the following data\n%s\n", search->key, data);
			}
			else
				printf("Node %lu not found in the list\n", i);
		}
		for(i=1; i<num/2; i++){
			unsigned long index = rand() % 10000;
			if (index == 0) index ++;
			char data[100];
			sprintf(data, "We have modified the data of this node with key %lu\n", index);
			printf("\nModifying Node with key %lu\n", index);
			modify_pnode(index, data);
		}


		// flush_list();
		// free_freememlist();

		// memcpy(buf, "pmfree", 7);
		// munmap(pmem, MAX_LIST_SIZE * PAGE_SIZE);
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
		head->data = (char *)(pmem + 7 + P_NODE_SIZE + sizeof(struct p_node *));
		memcpy(head->data, "This is the head", strlen("This is the head")+1);
		printf("Linked list with a single node - head, made successfully\n");
	
		free_head = pmem + PAGE_SIZE;
		// create_free_memlist(pmem + PAGE_SIZE, MAX_LIST_SIZE-1);
		printf("Free list for storing free memory created successfully\n");

		for(i=1; i<10000; i++){
			char data[100];
			sprintf(data, "This is a node of a persistent linked list\nIt's key value is %lu\n", i);
			// printf("Adding Node %lu\n", i);
			add_pnode(data,i);
		}

		for(i=100; i<40000; i+=100){
			printf("\nSearching Node with key %lu\n", i);
			search = search_pnode(i);
			if(search == NULL)
				printf("Node %lu not found in the list\n", i);
			else
				printf("Node %lu found in the list!!\nIt has the following data\n%s\n", search->key, search->data);
		}

		// flush_list();
		// free_freememlist();

		// memcpy(buf, "pmfree", 7);
		// munmap(pmem, MAX_LIST_SIZE * PAGE_SIZE);
		return 0;
	}
}

//FUNCTION DEFINITIONS

void* get_free_memory(void)
{
	void *memory = free_head;
	int flag = 0;
	void *next = free_head + 4096;
	if(next > pmem + MAX_LIST_SIZE * PAGE_SIZE){
		next = pmem + 4096;
		flag = 1;
	}

	while(1){
		char* buf, ssfree[7];
		buf = (char*) next;
		memcpy(ssfree, buf, strlen("ssused")+1);
		if(strcmp(ssfree,"ssused") && strcmp (ssfree, "pmlist"))
			break;

		next += 4096;
		if(next > pmem + MAX_LIST_SIZE * PAGE_SIZE){
			if(flag){
				free_head = NULL;
				perror("No more memory present\n");
				exit(-1);
			}
			next = pmem + 4096;
			flag = 1;
		}
	}

	free_head = next;
	return memory;
}

void add_free_memlist(void *memory)
{
	char* buf;
	buf = (char*) memory;
	memcpy(buf, "ssfree", strlen("ssfree")+1);
}

void add_pnode(char* data, unsigned long key)
{
	struct p_node *node, *it;
	void *memory;
	// printf("Got memory for key %lu\n", key);
	memory= get_free_memory();
	char *meta = (char*) memory;
	memcpy(meta, "ssused", 7);	

	node = (struct p_node*)(memory+7);
	node->key = key;
	node->next = NULL;
	node->data = (char*)(memory + 7 + P_NODE_SIZE + sizeof(struct p_node *));

	memcpy(node->data, data, strlen(data)+1);

	it = head;
	while(it->next){
		it = it->next;
	}

	it->next = node;
}

void delete_pnode(unsigned long key)
{
	struct p_node *node = head;
	struct p_node *prev_node = NULL;
	void *mem;
	while(node){
		if(node->key == key){
			/* deleting the node by sending the memory  back to free list */
			mem = (void*)node;
			printf("Deleting node %lu\nIt has the following data\n%s\n", node->key, node->data);
			
			if(prev_node)
				prev_node->next = node->next;
			else
				head = node->next;
	
			add_free_memlist(mem-7);
			return;
		}
		prev_node = node;
		node = node->next;
	}
	printf("Node %lu not found in the list\n", key);
}

struct p_node* search_pnode(unsigned long key)
{
	struct p_node *node = head;
	while(node){
		if(node->key == key){
			/* Search for the node was successfull */
			printf("Search successfull\n");
			return node;
		}
		node = node->next;
	}
	printf("Search unsuccessfull\n");
	return NULL;
}

void modify_pnode(unsigned long key, char *data)
{
	struct p_node *node = head;
	while(node){
		if(node->key == key){
			/* Search for the node was successfull */
			memcpy(node->data, data, strlen(data)+1);
			printf("Modification successfull\n");
			return;
		}
		node = node->next;
	}
	printf("Modification unsuccessfull\n");
}

void flush_list()
{
	struct p_node* node = head->next;
	void *mem;
	printf("\nFlushing all the nodes in the linked list (except the head)\n");
	while(node){
		mem = (void*)node;
		printf("Deleting node %lu\nIt has the following data\n%s\n", node->key, node->data);
		
		node = node->next;

		add_free_memlist(mem-7);
	}
}

void restore_list()
{
	void *memory = pmem + PAGE_SIZE;
	int i;
	char*buf;
	char meta[7];
	for(i=0;i<MAX_LIST_SIZE-1;i++){
		buf = (char*) memory;
		memcpy(meta, buf, 7);
//		printf("%s\n",meta);
		if(strcmp(meta, "ssused") && strcmp(meta, "pmlist")){
			free_head = memory;
			return;
		}
		
		memory += PAGE_SIZE;
	}
}
