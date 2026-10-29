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


## Further source-level investigation: DRBG nonce pool and PS instrumentation

After the `91d308ef` diagnostic build (which has **not yet been tested on
hardware**), inspecting OpenSSL 3.5.9's `crypto/rand/prov_seed.c` revealed
an additional capacity bug. `ossl_rand_get_nonce` ordinarily requests
nonce bytes and **then** appends a DRBG-specific `salt`. The Horizon
implementation could consume all the nonce pool's `max_len` with PS-generated
bytes, leaving no room for the salt. That makes `ossl_rand_pool_add` fail.
It is a legitimate failure path, but **we have not shown that it caused the
observed zero-code failure**.

`patches/openssl-3.5.9-3ds-nonce-salt.patch` changes only the `__3DS__`
path to append the salt first. The PS backend then queries the actual remaining
capacity and generates only that many nonce bytes. If there is no room for
PS nonce material, the operation still fails; it does not invent entropy or
fall back to a predictable source. The updated patch is applied automatically
by `scripts/build-openssl-3ds.sh` and included in the OpenSSL CI cache key.

The Horizon PS backend also exports a **non-secret diagnostic snapshot**
(`rdp_ps_rng_diag.h`). It contains:
- entropy acquisition call/success counts (`E success/calls`);
- nonce acquisition call/success counts (`N success/calls`);
- the most recent PS failure stage (0 none, 1 entropy `psInit`,
  2 entropy PS draw, 3 entropy pool, 4 nonce `psInit`, 5 nonce PS
  draw, 6 nonce pool);
- the most recent PS `Result` and requested entropy/nonce capacities.

The snapshot contains **no random bytes or seed material**. The full client
prints compact `PS E... N... F... R...` metadata on RNG failure, including
when OpenSSL's error queue is empty. If the client says `no PS seed calls`,
OpenSSL failed before reaching these entropy hooks.

The standalone `openssl-rng-smoke.3dsx` now separately fetches,
instantiates and draws from `SEED-SRC` *before* checking the primary DRBG,
public DRBG and `RAND_bytes`. This helps localize whether the failure occurs
in Horizon entropy collection, seed-source provider registration, or downstream
DRBG initialization. It is diagnostic only and does not replace the normal
randomness path for TLS.

### How to interpret the next O3DS diagnostic

- `SEED-SRC fetch/context` fails: check the embedded default provider and
  its seed-source registration, not the PS syscall.
- `SEED-SRC draw` fails and `PS E... F1/F2`: the Horizon service init/draw
  failed **inside OpenSSL**, even if a separate PS selftest passed earlier.
- Entropy successes are recorded but primary DRBG initialization fails:
  inspect nonce collection, DRBG configuration and OpenSSL thread/locking.
- `PS N... F4/F5/F6` indicates the nonce service or nonce pool path.
- `RAND_bytes` fails despite both DRBGs READY: investigate provider
  generate callbacks and per-thread state rather than merely reseeding.
- All direct PS and OpenSSL smoke checks pass but FreeRDP still fails:
  continue with the FreeRDP worker's own TLS/NLA/RNG sequence.

CI verifies compilation, static symbol linkage and `.3dsx` packaging, **not**
cryptographic correctness, provider initialization or TLS on physical O3DS.
Keep PR #11 draft until an actual hardware test provides the new stage and
counter values. No insecure RNG fallback is included.
