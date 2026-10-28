CC ?= cc
CFLAGS ?= -O2 -Wall -Wextra -Werror -std=c11
.PHONY: all test clean
all: test
test: test_rdp_probe test_config
	./test_rdp_probe
	./test_config
test_rdp_probe: test_rdp_probe.c ../source/rdp_probe.c ../include/rdp_probe.h
	$(CC) $(CFLAGS) -I../include test_rdp_probe.c ../source/rdp_probe.c -o $@
test_config: test_config.c ../source/config.c ../include/config.h
	$(CC) $(CFLAGS) -I../include test_config.c ../source/config.c -o $@
clean:
	rm -f test_rdp_probe test_config
