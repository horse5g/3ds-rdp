CC ?= cc
CFLAGS ?= -O2 -Wall -Wextra -Werror -std=c11
.PHONY: all test clean
all: test
test: test_rdp_probe test_config test_loopback_pair test_cert_pin test_cert_pool test_cert_store test_profiles test_ui_hit test_session_pad test_session_zoom test_rdp_pixels test_session_wait test_session_keys test_session_cursor
	./test_rdp_probe
	./test_config
	./test_loopback_pair
	./test_cert_pin
	./test_cert_pool
	./test_cert_store
	./test_profiles
	./test_ui_hit
	./test_session_pad
	./test_session_zoom
	./test_session_keys
	./test_session_cursor
	./test_rdp_pixels
	./test_session_wait
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
test_profiles: test_profiles.c ../source/profile_store.c ../source/config.c ../include/profile_store.h ../include/config.h
	$(CC) $(CFLAGS) -I../include test_profiles.c ../source/profile_store.c ../source/config.c -o $@
test_ui_hit: test_ui_hit.c ../source/connection_ui_hit.c ../include/connection_ui.h
	$(CC) $(CFLAGS) -I../include test_ui_hit.c ../source/connection_ui_hit.c -o $@
test_session_pad: test_session_pad.c ../source/session_pad.c ../include/session_pad.h
	$(CC) $(CFLAGS) -I../include test_session_pad.c ../source/session_pad.c -o $@
test_session_zoom: test_session_zoom.c ../source/session_zoom.c ../include/session_zoom.h
	$(CC) $(CFLAGS) -I../include test_session_zoom.c ../source/session_zoom.c -lm -o $@
test_session_wait: test_session_wait.c ../include/session_wait.h
	$(CC) $(CFLAGS) -I../include test_session_wait.c -o $@
test_rdp_pixels: test_rdp_pixels.c ../include/rdp_pixels.h
	$(CC) $(CFLAGS) -I../include test_rdp_pixels.c -o $@
test_session_keys: test_session_keys.c ../source/session_keys.c ../include/session_keys.h
	$(CC) $(CFLAGS) -I../include test_session_keys.c ../source/session_keys.c -o $@
test_session_cursor: test_session_cursor.c ../source/session_cursor.c ../include/session_cursor.h
	$(CC) $(CFLAGS) -I../include test_session_cursor.c ../source/session_cursor.c -o $@
clean:
	rm -f test_rdp_probe test_config test_loopback_pair test_cert_pin test_cert_pool test_cert_store test-cert-store-fixture.txt test-cert-store-fixture.txt.tmp test-cert-store-fixture.txt.bak test_profiles test_ui_hit test_session_pad test_session_zoom test_rdp_pixels test_session_wait test_session_keys test_session_cursor test-profiles.tmp test-profiles.tmp.bak
