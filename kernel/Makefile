ifneq ($(KERNELRELEASE),)
obj-m := huawei-wmi.o
ccflags-y += -I$(src)
else
KDIR ?= /lib/modules/$(shell uname -r)/build
.PHONY: all clean
all:
	$(MAKE) -C "$(KDIR)" M="$(CURDIR)" W=1 modules
clean:
	$(MAKE) -C "$(KDIR)" M="$(CURDIR)" clean
endif
