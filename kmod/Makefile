module := skyloft

CPU_LIST ?= 0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19,20,21

KDIR ?= /lib/modules/$(shell uname -r)/build
DEBUG ?=
UINTR ?= 1

KBUILD_CFLAGS += -I$(PWD)/../include

ifneq ($(DEBUG),)
  KBUILD_CFLAGS += -DDEBUG
endif
ifneq ($(UINTR),)
  KBUILD_CFLAGS += -DSKYLOFT_UINTR
endif

obj-m := $(module).o

$(module)-y := main.o

all:
	make -C $(KDIR) M=$(PWD) modules

clean:
	make -C $(KDIR) M=$(PWD) clean

insmod: rmmod
	sudo insmod $(module).ko cpu_list=$(CPU_LIST)
	sudo chmod 666 /dev/$(module)

rmmod:
ifneq ($(shell lsmod | grep $(module)),)
	sudo rmmod $(module)
endif
