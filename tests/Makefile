CC ?= cc
CFLAGS ?= -O2 -Wall -Wextra -Werror -std=c11
.PHONY: all test clean
all: test
test: test_rdp_probe test_config test_loopback_pair test_cert_pin test_cert_pool test_cert_store
	./test_rdp_probe
	./test_config
	./test_loopback_pair
	./test_cert_pin
	./test_cert_pool
	./test_cert_store
test_rdp_probe: test_rdp_probe.c ../source/rdp_probe.c ../include/rdp_probe.h
	$(CC) $(CFLAGS) -I../include test_rdp_probe.c ../source/rdp_probe.c -o $@
test_config: test_config.c ../source/config.c ../include/config.h
	$(CC) $(CFLAGS) -I../include test_config.c ../source/config.c -o $@
test_loopback_pair: test_loopback_pair.c ../source/loopback_pair.c ../include/loopback_pair.h
	$(CC) $(CFLAGS) -I../include test_loopback_pair.c ../source/loopback_pair.c -o $@
test_cert_pin: test_cert_pin.c ../source/cert_pin.c ../include/cert_pin.h
	$(CC) $(CFLAGS) -I../include test_cert_pin.c ../source/cert_pin.c -o $@
test_cert_pool: test_cert_pool.c ../source/cert_pool.c ../include/cert_pool.h ../source/cert_pin.c ../include/cert_pin.h
	$(CC) $(CFLAGS) -I../include test_cert_pool.c ../source/cert_pool.c ../source/cert_pin.c -o $@
test_cert_store: test_cert_store.c ../source/cert_store.c ../source/cert_pool.c ../source/cert_pin.c ../include/cert_store.h
	$(CC) $(CFLAGS) -I../include test_cert_store.c ../source/cert_store.c ../source/cert_pool.c ../source/cert_pin.c -o $@
clean:
	rm -f test_rdp_probe test_config test_loopback_pair test_cert_pin test_cert_pool test_cert_store test-cert-store-fixture.txt test-cert-store-fixture.txt.tmp test-cert-store-fixture.txt.bak
