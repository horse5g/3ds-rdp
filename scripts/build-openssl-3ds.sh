#!/usr/bin/env bash
# Experimental only: build real OpenSSL crypto/TLS for devkitARM.
# Do not deploy it until RNG, time, certificate verification, threads and
# multithreaded TLS behavior have passed physical-device tests.
set -euo pipefail

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
: "${DEVKITPRO:?Set DEVKITPRO to your devkitPro installation}"
: "${DEVKITARM:=$DEVKITPRO/devkitARM}"
export DEVKITPRO DEVKITARM

version="openssl-3.5.9"
src="${OPENSSL_SOURCE:-$root/external/openssl}"
build="${OPENSSL_BUILD_DIR:-$root/build/openssl-3ds}"
mode="${1:-build}"

if [[ "$mode" != build && "$mode" != configure ]]; then
  echo "Usage: build-openssl-3ds.sh [configure|build]" >&2
  exit 2
fi

for cmd in git perl make cmp patch; do
  command -v "$cmd" >/dev/null || { echo "Missing $cmd" >&2; exit 2; }
done
for exe in gcc ar ranlib; do
  [[ -x "$DEVKITARM/bin/arm-none-eabi-$exe" ]] || {
    echo "Missing devkitARM arm-none-eabi-$exe" >&2
    exit 2
  }
done

# The source directory is intentionally separate from the FreeRDP checkout,
# and only a pinned upstream checkout is ever patched by this script.
if [[ ! -f "$src/Configure" ]]; then
  [[ ! -e "$src" ]] || {
    echo "Existing non-OpenSSL source directory: $src" >&2
    exit 2
  }
  mkdir -p "$(dirname "$src")"
  git clone --depth 1 --branch "$version" \
    https://github.com/openssl/openssl.git "$src"
fi
if [[ "$src" == "$root/external/openssl" ]]; then
  dest="$src/Configurations/15-3ds.conf"
  if [[ -e "$dest" ]] && ! cmp -s "$root/cmake/openssl-3ds.conf" "$dest"; then
    echo "Existing 3DS OpenSSL target differs: $dest" >&2
    exit 2
  fi
  cp "$root/cmake/openssl-3ds.conf" "$dest"
  if ! grep -q 'Horizon SOC socket descriptors require recv/send' "$src/include/internal/sockets.h"; then
    patch -d "$src" -p1 < "$root/patches/openssl-3.5.9-3ds-sockets.patch"
  fi
  if ! grep -q '#ifndef __3DS__' "$src/crypto/bio/bio_addr.c"; then
    patch -d "$src" -p1 < "$root/patches/openssl-3.5.9-3ds-numeric-services.patch"
  fi
  if ! grep -q 'Not a supported Horizon SOC socket option' "$src/crypto/bio/bio_sock2.c"; then
    patch -d "$src" -p1 < "$root/patches/openssl-3.5.9-3ds-keepalive.patch"
  fi
elif [[ ! -f "$src/Configurations/15-3ds.conf" ]]; then
  echo "Custom OPENSSL_SOURCE requires its own 3ds-armv6k target config" >&2
  exit 2
fi

mkdir -p "$build"
build="$(cd "$build" && pwd)"

# OpenSSL's Unix/pthreads back end needs actual libctru pthread support.
# Do not use no-threads to make the crypto libraries appear to work.
# No ASM / DSO / dynamic modules on the ARM11/Horizon target.
(
  cd "$build"
  PATH="$DEVKITARM/bin:$PATH" \
  "$src/Configure" 3ds-armv6k \
    --cross-compile-prefix=arm-none-eabi- \
    --prefix="$build/install" \
    --openssldir="$build/install/ssl" \
    no-asm no-shared no-dso no-module no-engine \
    no-tests no-apps no-afalgeng no-ui-console no-quic
  if [[ "$mode" == build ]]; then
    PATH="$DEVKITARM/bin:$PATH" make -j"${JOBS:-2}" build_libs
    test -s libcrypto.a
    test -s libssl.a
    "$DEVKITARM/bin/arm-none-eabi-ar" t libcrypto.a | head -n 3
    "$DEVKITARM/bin/arm-none-eabi-ar" t libssl.a | head -n 3
  fi
)
