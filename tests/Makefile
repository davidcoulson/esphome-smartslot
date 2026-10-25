# SPDX-License-Identifier: GPL-2.0-or-later
CC ?= gcc
CFLAGS = -std=gnu11 -Wall -Wextra -g -I../components/smartslot/shim/include -I../components/smartslot/nut -I../components/smartslot/upsd -Iunity
SRCS = unity/unity.c os_posix.c test_main.c

.PHONY: run
run: build/tests
	./build/tests

build/tests: $(SRCS) $(wildcard ../components/smartslot/*/*.c ../components/smartslot/*/*.h ../components/smartslot/shim/include/*.h *.h)
	@mkdir -p build
	$(CC) $(CFLAGS) $(SRCS) -o $@ -lpthread
