# SPDX-License-Identifier: GPL-2.0-or-later
CC ?= gcc
CFLAGS = -std=gnu11 -Wall -Wextra -g -I../components/smartslot/shim/include -I../components/smartslot/nut -I../components/smartslot/upsd -Iunity -D_GNU_SOURCE
SHIM = ../components/smartslot/shim
UPSD = ../components/smartslot/upsd
SRCS = unity/unity.c os_posix.c test_main.c $(SHIM)/dstate.c $(SHIM)/nutlog.c $(SHIM)/vars.c $(SHIM)/ser_common.c serial_host.c test_dstate.c test_serial.c $(UPSD)/nutproto.c test_nutproto.c $(UPSD)/upsd.c test_upsd.c $(SHIM)/nutdrv.c fake_upsdrv.c test_nutdrv.c test_publish.c test_timegm.c

.PHONY: run
run: build/tests
	./build/tests

build/tests: $(SRCS) $(wildcard ../components/smartslot/*/*.c ../components/smartslot/*/*.h ../components/smartslot/shim/include/*.h *.h)
	@mkdir -p build
	$(CC) $(CFLAGS) $(SRCS) -o $@ -lpthread

# Host tests for the SNMP agent core (plain C++, asserts): `make snmp`
.PHONY: snmp
snmp: build/test_snmp
	./build/test_snmp

build/test_snmp: test_snmp.cpp ../components/snmp_agent/snmp_core.cpp ../components/snmp_agent/snmp_core.h
	@mkdir -p build
	$(CXX) -std=c++17 -Wall -Wextra -g -O1 -fsanitize=address,undefined -I../components/snmp_agent test_snmp.cpp ../components/snmp_agent/snmp_core.cpp -o $@ -lpthread

.PHONY: snmp-serve
snmp-serve: build/snmp_host
	./build/snmp_host $(or $(PORT),1161)

build/snmp_host: snmp_host.cpp ../components/snmp_agent/snmp_core.cpp ../components/snmp_agent/snmp_core.h
	@mkdir -p build
	$(CXX) -std=c++17 -Wall -Wextra -g -I../components/snmp_agent snmp_host.cpp ../components/snmp_agent/snmp_core.cpp -o $@ -lpthread
