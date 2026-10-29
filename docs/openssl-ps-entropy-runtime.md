# OpenSSL 3DS entropy runtime investigation

## Hardware symptom

On O3DS, full-client sign-on stops before the RDP password prompt with
`OPENSSL ENTROPY UNAVAILABLE (3DS PS SERVICE)`. This means the
`RAND_bytes()` preflight did not return success, **not** necessarily that
`psInit()` itself failed.

## Two separate failure layers

1. **Horizon PS:** the app already supports **L+SELECT** on the connection
   menu to call `psInit()`, draw two 32-byte buffers from
   `PS_GenerateRandomBytes()`, and ensure they are distinct/nonzero.
   When this fails it reports the actual 32-bit Horizon result code.
2. **OpenSSL DRBG:** on the 3DS, OpenSSL 3.5.9 is patched to use PS
   entropy. It also requests nonce material. The original 3DS patch
   unconditionally appended 32 nonce bytes, ignoring the maximum nonce
   pool allocation from OpenSSL. The patch now caps the PS request to
   `ossl_rand_pool_bytes_remaining(pool)`, and fails closed if the
   pool cannot accept any data.

Before `RAND_bytes()`, the client now runs the direct PS check. If PS
fails the dialog shows `PS RNG: psInit: 0x...` or
`PS RNG: PS draw 1/2: 0x...`. If PS passes but the OpenSSL DRBG
still fails, the dialog shows the last OpenSSL error reason and code,
for example `OpenSSL RAND: ... (0x...)`. This is a diagnostic
classification: **successful PS draws do not guarantee that OpenSSL's
provider initialized successfully**.

## Security and verification

- No fallback to `rand()`, system clocks, timestamps, predictable
  startup values, or untrusted files; TLS/NLA remains blocked until
  `RAND_bytes()` succeeds.
- Random output is not printed or saved. The PS test clears its buffers,
  and the preflight calls `OPENSSL_cleanse()`.
- Updating `patches/openssl-3.5.9-3ds-ps-entropy.patch` changes the
  OpenSSL prerequisite's GitHub Actions cache key. The next full-client
  build must rebuild libcrypto and libssl rather than silently reusing
  the previous entropy implementation.
- CI compilation and linking cannot prove correct Horizon PS IPC or
  OpenSSL DRBG initialization on hardware. Retest on an actual O3DS.
- Do not merge until the full OpenSSL + FreeRDP pipeline passes; keep this
  PR stacked on the current runtime audit PR.

## Next O3DS test

Try **L+SELECT** first in the launcher and record the status.
Then try full RDP again and send the exact new error text.
If the PS test passes and OpenSSL still fails, the error queue reason
helps pinpoint whether the RNG provider, seed pool, or a different
OpenSSL initialization path is failing.
