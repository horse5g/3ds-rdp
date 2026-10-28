CC ?= cc
CFLAGS ?= -O2 -Wall -Wextra -Werror -std=c11
.PHONY: all test clean
all: test
test: test_rdp_probe
	./test_rdp_probe
test_rdp_probe: test_rdp_probe.c ../source/rdp_probe.c ../include/rdp_probe.h
	$(CC) $(CFLAGS) -I../include test_rdp_probe.c ../source/rdp_probe.c -o $@
clean:
	rm -f test_rdp_probe
