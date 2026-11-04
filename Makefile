CC ?= cc
CFLAGS ?= -O2
WARN = -std=c11 -Wall -Wextra
KDIR ?= /lib/modules/$(shell uname -r)/build

.PHONY: all cli driver test clean

all: cli driver

cli: hunter

hunter: cli/hunter.c
	$(CC) $(WARN) $(CFLAGS) $(LDFLAGS) -o $@ $<

driver:
	$(MAKE) -C $(KDIR) M=$(CURDIR)/driver modules

test: tests/test_hunter.c driver/hunter.h
	$(CC) $(WARN) $(CFLAGS) -o tests/test_hunter $<
	./tests/test_hunter

clean:
	rm -f hunter tests/test_hunter
	$(MAKE) -C $(KDIR) M=$(CURDIR)/driver clean
