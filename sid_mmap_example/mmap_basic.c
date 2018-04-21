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

#define PAGE_SIZE 4096

int main()
{
   char buf[40];
   char *ptr;
 
   int fd = open("/dev/pmap",O_RDWR);
   if(fd < 0){
       perror("open");
       exit(-1);
   }
  
  ptr = mmap((void*) 55555555, 10 * PAGE_SIZE, PROT_READ|PROT_WRITE, MAP_SHARED, fd, 0);
  if(ptr == MAP_FAILED){ 
        perror("mmap");
        exit(-1);
  }
  
  printf("Process mmap_basic\n");

  memcpy(buf, ptr, 10);
  printf("Read 4 %s\n", buf);

  memcpy(ptr, "write1",10);
  memcpy(buf, ptr, 10);
  printf("Read 1 %s\n", buf);

  memcpy(ptr+25000, "write3",10);
  memcpy(buf, ptr+25000, 10);
  printf("Read II %s\n", buf);

  memcpy(ptr+20000, "write2",10);
  memcpy(buf, ptr+20000, 10);
  printf("Read II %s\n", buf);

  /* Waiting for a dummy input - mainly to pause the process */
  scanf("%s", buf);
  // munmap(ptr, 10*PAGE_SIZE);
  close(fd);
  return 0;  
}
