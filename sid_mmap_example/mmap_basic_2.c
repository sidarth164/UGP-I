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

int main()
{
   char buf[40];
   char *ptr;
 
   int fd = open("/dev/pmap",O_RDWR);
   if(fd < 0){
       perror("open");
       exit(-1);
   }
  
  ptr = mmap((void*) 55555555, 53248, PROT_READ|PROT_WRITE, MAP_SHARED, fd, 0);
  if(ptr == MAP_FAILED){ 
        perror("mmap");
        exit(-1);
  }
  
  printf("Process mmap_basic_2\n");

  memcpy(ptr+5000, "hellokern",10);
  memcpy(buf, ptr+5000, 10);
  printf("Read 1 %s\n", buf);

  memcpy(ptr+35000, "kernhello",10);
  memcpy(buf, ptr+35000, 10);
  printf("Read II %s\n", buf);

/* Wait for a dummy input - mainly for pausing the process */
  scanf("%s",buf);
  munmap(ptr, 53428);
  close(fd);
  return 0;  
}
