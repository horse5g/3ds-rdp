# OpenSSL for the original 3DS (experimental)

This is a *cryptography dependency build probe*, not a working TLS client and
not yet integrated into the main `3ds-rdp.3dsx` target.

## Build

The standalone workflow [OpenSSL ARM11 feasibility](../.github/workflows/openssl-arm11.yml)
uses the official pinned upstream `openssl-3.5.9` tag. From the project root
with devkitARM/devkitPro installed:

```sh
bash scripts/build-openssl-3ds.sh build
```

The script clones to `external/openssl`, installs our target description from
`cmake/openssl-3ds.conf` inside that ignored tagged checkout, and builds
`libcrypto.a` and `libssl.a` under `build/openssl-3ds`.

The `3ds-armv6k` target uses the native ARM11 hard-float ABI and **pthreads**.
The experimental build disables assembly (pending ARM11 performance and
correctness analysis), shared libraries, modules/DSOs, engines, apps and tests.
It **does not** disable threading, TLS, certificate validation or
cryptographic algorithms simply to make the build pass.

The first independent CI builds reached real OpenSSL BIO compilation and uncovered
missing libctru network include paths, unavailable IPv6 structures, and Unix-only
service-name, keepalive and syslog APIs. Small scoped adjustments address these
compile-time dependencies. These are not successful TLS runtime tests; inspect
the latest OpenSSL ARM11 workflow for remaining failures.

Platform compatibility measures:
- libctru's `netdb.h` and SOC networking headers are added to the compile
  include path.
- OpenSSL IPv6 and Unix-domain socket code paths are excluded for the
  IPv4-only Horizon SOC implementation. Neither is emulated.
- In `include/internal/sockets.h` the socket BIO read/write operations
  use `recv/send` rather than POSIX file `read/write`. `ioctl` and
  nonblocking socket semantics still need checking on hardware.
- In `crypto/bio/bio_addr.c` the unsupported service-name fallback is
  excluded; the available libctru `getaddrinfo` path is retained. Numeric
  TCP ports such as 3389 are the supported use case. DNS and socket behavior
  must still be validated on physical hardware.
- Socket keepalive requests fail explicitly if libctru lacks `SO_KEEPALIVE`;
  OpenSSL's Unix syslog BIO is excluded as Horizon provides no `syslog.h`.

CI output must be inspected for actual library build results; the existing
FreeRDP feasibility workflow deliberately treats the FreeRDP build as
non-blocking, and must not be used as a cryptography success signal.

## Archive and link feasibility milestones

In CI, the OpenSSL 3.5.9 `build_libs` step has completed and both `libcrypto.a` and `libssl.a` passed nonempty archive checks. An earlier job nevertheless failed with exit code 141 because `ar t | head` triggered `pipefail`; the command now uses `sed` to avoid that false failure.

The build omits QUIC with OpenSSL's supported `no-quic` option (Microsoft RDP uses TLS over TCP, not QUIC in this client) and omits console-only password UI with `no-ui-console`. TLS and X.509 remain enabled, and threading is retained.

After compiling both archives, CI also attempts a **separate, informational static link** of `tests/openssl-arm11-link.c`, pulling `SSL_CTX_new`, `TLS_client_method`, X.509 store symbols, and libctru into an ARM11 ELF. This test does not run on hardware. Any unresolved symbols or linker failures must be fixed before attempting a real secure TLS session.

The first static-link probe failed only on POSIX libc symbols absent from
Horizon: `posix_memalign`, `getuid`, `geteuid`, `getgid`, and `getegid`.
The native patch now routes aligned allocations through OpenSSL's existing
padded-allocation fallback with a size-overflow check. Horizon has no Unix
user/group identities, so `OPENSSL_issetugid()` conservatively returns true:
OpenSSL must **not** trust environment-based configuration overrides on the
3DS. Do not invent or fake Unix UIDs. These link changes are being rechecked
by the independent ARM11 workflow.

The native homebrew app now includes an independent **SELECT**-button check for `psInit` and `PS_GenerateRandomBytes`, without retaining or displaying any output bytes. A passing self-test will establish that the PS service is accessible but **not** that OpenSSL's DRBG is securely seeded or that a TLS session is safe. The OpenSSL entropy-provider integration is still required.

## Security and native integration requirements

**Do not use the static libraries for authentication yet.** Before enabling
FreeRDP TLS or NLA on a 3DS, all of the following need implemented and tested:

1. **Cryptographically secure entropy.** The 3DS exposes
   `PS_GenerateRandomBytes` via libctru (requires suitable PS service
   initialization). Integrate it into OpenSSL's supported entropy/DRBG
   provider path, propagate all failures, verify OpenSSL's DRBG instantiates
   successfully and never fall back to timestamps or predictable seeds.
2. **Certificate validation and trust anchors.** Load/maintain trusted CAs
   through an explicit device trust-store policy, validate certificate chains
   and hostname, reject unknown/untrusted identities by default, and ensure
   system wall clock is trustworthy for certificate validity checks.
3. **Socket and thread behavior.** Link `libssl`, `libcrypto`, libctru
   and any other required native libraries into a minimal 3DS executable.
   Verify SOC socket readiness/nonblocking modes, TLS 1.2+/1.3 handshake,
   multiple concurrent threads and event wakeups, closure, sleep/resume,
   error handling, memory limits and performance on original hardware.
4. **FreeRDP compatibility.** Only after a secure TLS smoke test, configure
   FreeRDP 3.32.2 to use the actual OpenSSL headers and archives and resolve
   any remaining library interfaces. Preserve certificate validation and
   CredSSP/NLA; do not disable NLA merely to obtain an RDP session.
5. **Sensitive input.** No passwords on SD; implement appropriate runtime
   input and handling before attempting Windows/domain authentication.

FreeRDP currently retains its independent Mbed TLS cross-build experiment.
The OpenSSL build is kept separate so it cannot change the existing RDP
negotiation probe or silently enable authentication before security validation.
