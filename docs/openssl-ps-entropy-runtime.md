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


## New O3DS result: OpenSSL core initialization fails before provider load

The latest reported **standalone** diagnostic showed:
```text
default provider failed (OpenSSL 0x00000000)
SSL_CTX_new failed (OpenSSL 0x00000000)
PS entropy OK/calls: 0/0
PS nonce OK/calls: 0/0
SEED-SRC: FAIL / UNAVAILABLE
```
The **full RDP client** separately reported `SSL CRYPTO INIT NO PS SEED CALLS`.
Together these demonstrate that the failure currently happens **before
OpenSSL reaches our Horizon PS entropy backend**; PS and the nonce capacity
patch are not the primary active blockers. The direct `PS_GenerateRandomBytes`
hardware self-test passed previously.

OpenSSL 3.5.9's `OPENSSL_init_crypto` can return 0 from its once-only base
initialization, startup lock allocation, pthread TLS key initialization,
atomic state handling, or exit-handler registration, without a populated
error queue. The 3DS build uses `thread_scheme=pthreads`. To isolate these
paths, `patches/openssl-3.5.9-3ds-init-diagnostics.patch` now adds a
read-only `ossl_3ds_crypto_init_stage()` marker (no RNG material). The
full RDP client displays `OpenSSL core init S<stage>`. Interpret:

| Stage | Failure |
|---|---|
| 0 | Not reached by current instrumentation / another uninstrumented path |
| 1 | Startup mutex allocation/init |
| 2 | OpenSSL thread destructor TLS key |
| 3 | OpenSSL configuration TLS key |
| 4 | Pthread once-only or base initialization |
| 5 | Post-base initialization atomic load |
| 6 | Process-exit handler registration |
| 7 | DSO/no-delete initialization |
| 8 | Suppressing OpenSSL configuration loading |
| 9 | Recording initialized option bits |

The updated standalone test also reports `pthread_once`, mutex
initialization/lock/unlock, TLS-key create/set/get, and pthread read/write
lock return codes; then runs `OPENSSL_init_crypto(BASE_ONLY)` followed by
the normal full initialization, showing both stage numbers. This diagnoses
the platform's pthread support **without disabling threading** or replacing
entropy with a predictable fallback.

**Do not interpret stage 0 as successful initialization.** If it still
returns zero, the next step is to add more precise instrumentation inside
the relevant remaining upstream OpenSSL initialization branch. Running the
same failing probe repeatedly without further evidence is not useful.

These changes are limited to draft PR #11 until the updated code is tested
on real original 3DS hardware.

## Hardware follow-up: Stage 2 is ENOSYS in newlib pthread TSD

Corrected O3DS hardware smoke reports:
```text
pthread once 0,0 calls 1
mutex init/lock/unlock 0/0/0
TLS key/create/set/get 88/-1/0
crypto BASE 0 stage 2
crypto FULL 0 stage 2
PS entropy OK/calls 0/0
PS nonce OK/calls 0/0
```
The full client independently reports `OPENSSL CORE INIT S2 0x00000000`.
On this devkitARM/newlib target, errno 88 is `ENOSYS`: thread-specific
key creation is not implemented by the linked newlib pthread stub.
OpenSSL's `ossl_init_thread()` needs a destructor-bearing pthread key;
its absence blocks base initialization before provider or PS entropy.

### MIT-compatible implementation under test

`rust3ds/pthread-3ds` is **MIT OR Apache-2.0**, but its Rust crate explicitly
targets Rust std rather than standalone C clients. We chose its permitted
MIT licensing and per-thread storage design as a reference, and wrote a
self-contained C bridge in `source/rdp_pthread_tsd.c`. It uses libctru
LightLock, GCC `__thread` storage, generation-stamped pthread keys, and
POSIX-style four-pass destructor cleanup. It supplies only
`pthread_key_create/delete/getspecific/setspecific` and deliberately leaves
mutexes, rwlocks and once-only state with the existing platform library.

**Important:** This C adapter is experimental and does not magically hook
all libctru thread-exit paths. Application-controlled RDP connect and
keyboard networking workers call `OPENSSL_thread_stop()` and
`rdp_3ds_tsd_cleanup_current_thread()` before returning. New app-managed
threads using OpenSSL must do the same; third-party threads not managed
by this application need an independent lifecycle audit. The C code does
not use Rust, require a Rust compiler, disable OpenSSL threads, or change
RNG/trust policy. The MIT-compatible implementation is not an assertion of
hardware correctness.

The dedicated smoke tool now tests **real worker thread isolation** and
destructor invocation (expected 2 calls), in addition to OpenSSL BASE/FULL,
SEED-SRC, DRBG and PS counters. This is a security-sensitive boundary:
a failed TSD test MUST fail the smoke summary even if a later OpenSSL call
appears to succeed. Strict CI checks that the compiled .elf contains the
strong pthread key implementation and separately publishes only the .3dsx
as a runnable artifact.

**Next physical O3DS test:** Run the newly uploaded standalone
`openssl-rng-smoke.3dsx`; report `TLS key/create/set/get`,
`TSD threads key/set/worker/main`, `TSD destructor calls`,
`crypto BASE/FULL`, and SEED-SRC / PS counters. Do not assume the
built-in `__thread` data is correctly thread-isolated on this device
until hardware confirms it. Keep draft PR #11 unmerged until TLS/NLA works.
