#ifndef RDP_PS_RNG_DIAG_H
#define RDP_PS_RNG_DIAG_H

/* Non-secret counters exported by the patched OpenSSL 3.5.9 Horizon PS seed
 * implementation. No RNG bytes, seed material or certificate keys are exposed.
 * This API exists only in this private 3DS cross-build, not upstream OpenSSL.
 * Read after RNG preflight, before multithreaded TLS begins. */
typedef struct {
    unsigned int entropy_calls;
    unsigned int entropy_successes;
    unsigned int nonce_calls;
    unsigned int nonce_successes;
    unsigned int last_failure_stage; /* 0=none, 1=entropy init, 2=draw,
                                     * 3=pool, 4=nonce init, 5=draw, 6=pool */
    unsigned int last_ps_result;
    unsigned int last_entropy_request;
    unsigned int last_nonce_capacity;
} RdpPsRngDiag;

void ossl_3ds_ps_rng_snapshot(RdpPsRngDiag *out);

#endif
