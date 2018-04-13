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
 
   int fd = open("/dev/demo",O_RDWR);
   if(fd < 0){
       perror("open");
       exit(-1);
   }
  
  ptr = mmap((void*) 55555555, 8192, PROT_READ|PROT_WRITE, MAP_SHARED, fd, 0);
  if(ptr == MAP_FAILED){ 
        perror("mmap");
        exit(-1);
  }
  
  printf("Checking persistence\n");

  memcpy(ptr, "hellokern",10);
  memcpy(buf, ptr, 10);
  printf("Read 1 %s\n", buf);

  memcpy(ptr, "kernhello",10);
  memcpy(buf, ptr, 10);
  printf("Read II %s\n", buf);
  munmap(ptr, 4096);
  close(fd);
  return 0;  
}
