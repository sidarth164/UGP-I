obj-m = cdev_mmap.o

KVERSION = $(shell uname -r)

all:
	make -C /lib/modules/$(KVERSION)/build M=$(PWD) modules
	gcc mmap_basic.c -o mmap_basic
	gcc mmap_basic_2.c -o mmap_basic_2

clean:
	make -C /lib/modules/$(KVERSION)/build M=$(PWD) clean
	rm mmap_basic mmap_basic_2

