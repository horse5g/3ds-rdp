CC ?= cc
CFLAGS ?= -O2 -Wall -Wextra -Werror -std=c11
.PHONY: all test clean
all: test
test: test_rdp_probe test_config test_loopback_pair test_profiles test_ui_hit
	./test_rdp_probe
	./test_config
	./test_loopback_pair
	./test_profiles
	./test_ui_hit
test_rdp_probe: test_rdp_probe.c ../source/rdp_probe.c ../include/rdp_probe.h
	$(CC) $(CFLAGS) -I../include test_rdp_probe.c ../source/rdp_probe.c -o $@
test_config: test_config.c ../source/config.c ../include/config.h
	$(CC) $(CFLAGS) -I../include test_config.c ../source/config.c -o $@
test_loopback_pair: test_loopback_pair.c ../source/loopback_pair.c ../include/loopback_pair.h
	$(CC) $(CFLAGS) -I../include test_loopback_pair.c ../source/loopback_pair.c -o $@

test_profiles: test_profiles.c ../source/profile_store.c ../source/config.c ../include/profile_store.h ../include/config.h
	$(CC) $(CFLAGS) -I../include test_profiles.c ../source/profile_store.c ../source/config.c -o $@
test_ui_hit: test_ui_hit.c ../source/connection_ui_hit.c ../include/connection_ui.h
	$(CC) $(CFLAGS) -I../include test_ui_hit.c ../source/connection_ui_hit.c -o $@
clean:
	rm -f test_rdp_probe test_config test_loopback_pair test_profiles test_ui_hit test-profiles.tmp test-profiles.tmp.bak
