obj-m = cdev_mmap.o

KVERSION = $(shell uname -r)

all:
	make -C /lib/modules/$(KVERSION)/build M=$(PWD) modules
	gcc mmap_basic.c -o mmap_basic

clean:
	make -C /lib/modules/$(KVERSION)/build M=$(PWD) clean
	rm mmap_basic

