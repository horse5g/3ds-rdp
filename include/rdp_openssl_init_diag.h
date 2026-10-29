#ifndef RDP_OPENSSL_INIT_DIAG_H
#define RDP_OPENSSL_INIT_DIAG_H

/* 3DS-only probe added to patched OpenSSL 3.5.9 crypto/init.c.
 * The returned stage never contains entropy, keys, or application data.
 * 0: no traced failure
 * 1: startup lock allocation/init
 * 2: OpenSSL thread destructor TLS key
 * 3: configuration TLS key
 * 4: pthread_once or base initialization
 * 5: post-base atomic load
 * 6: exit-handler registration
 * 7: nodelete/DSO initialization
 * 8: configuration suppression
 * 9: atomic option-state update
 * This does not override or suppress any OpenSSL error. */
unsigned int ossl_3ds_crypto_init_stage(void);

#endif
