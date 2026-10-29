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

## Follow-up on physical O3DS: ERR code zero

The next test returned `OPENSSL RAND FAILED CODE 0X00000000`, after the
direct PS test passed. **This proves the PS service is accessible for the
direct test, not that OpenSSL's provider obtained entropy successfully.**
OpenSSL 3.5's `RAND_bytes_ex()` can return zero without populating its
error queue when the global RNG data or public DRBG is unavailable. During
primary initialization, the seed-source failure's error may also be consumed
by `ERR_pop_to_mark()`, leaving no error to display.

The updated FreeRDP startup now explicitly:
1. loads the built-in `default` provider (no-module cross-build),
2. fetches `CTR-DRBG` from it,
3. checks that `RAND_get0_primary(NULL)` returns a ready generator,
4. checks that `RAND_get0_public(NULL)` returns a ready generator,
5. calls `RAND_bytes()` only after the previous steps pass.

When a stage fails, its name is shown **even when the error code is zero**.
The provider is kept loaded until the session and FreeRDP cleanup finish.
No bypass, alternative DRBG, static seed, or replacement RNG is used.

The separate `openssl-rng-smoke.3dsx` probe is updated to show the same
provider, primary, and public stages. If the standalone probe fails at
the same stage, the problem belongs to the embedded OpenSSL port, not
FreeRDP's network or Windows authentication.
