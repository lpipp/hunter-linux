PYTHON ?= python3
CC ?= cc
PREFIX ?= /usr
DESTDIR ?=
.PHONY: test kernel prepare install
prepare:
	$(PYTHON) tools/prepare_kernel.py
kernel: prepare
	$(MAKE) -C kernel
test:
	mkdir -p build
	$(CC) -std=c11 -Wall -Wextra -Werror -pthread -Ikernel tests/test_wire.c -o build/test-wire
	./build/test-wire
	sh -n bin/hunter-mode
	sh -n bin/hunter-fans
install:
	install -d "$(DESTDIR)$(PREFIX)/bin"
	install -m 755 bin/hunter-mode bin/hunter-fans "$(DESTDIR)$(PREFIX)/bin/"
