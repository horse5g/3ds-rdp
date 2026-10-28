#!/usr/bin/env bash
# Compile-only check of the full client source, independent of a completed
# FreeRDP static archive. CI runs it after FreeRDP CMake configure.
set -euo pipefail
root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
: "${DEVKITPRO:?Set DEVKITPRO to devkitPro}"
: "${DEVKITARM:=$DEVKITPRO/devkitARM}"
build="${FREERDP_BUILD_DIR:-$root/build/freerdp-3ds}"
ssl="${OPENSSL_BUILD_DIR:-$root/build/openssl-3ds}"
frsrc="${FREERDP_SOURCE:-$root/external/FreeRDP}"
sslsrc="${OPENSSL_SOURCE:-$root/external/openssl}"
[[ -r "$build/include/freerdp/config.h" ]] || {
  echo "FreeRDP CMake has not generated include/freerdp/config.h" >&2
  exit 2
}
[[ -r "$ssl/include/openssl/configuration.h" ]] || exit 2
cc="$DEVKITARM/bin/arm-none-eabi-gcc"
"$cc" -march=armv6k -mtune=mpcore -mfloat-abi=hard -mtp=soft \
  -std=gnu11 -D__3DS__ -DRDP_FULL_CLIENT \
  -Wall -Wextra -Werror=implicit-function-declaration \
  -Werror=incompatible-pointer-types -Wno-deprecated-declarations \
  -I"$DEVKITPRO/libctru/include" -I"$root/include" \
  -I"$frsrc/include" -I"$frsrc/winpr/include" \
  -I"$build" -I"$build/include" -I"$build/winpr" -I"$build/winpr/include" \
  -I"$ssl/include" -I"$sslsrc/include" \
  -fsyntax-only "$root/source/freerdp_session.c" "$root/source/main.c"
echo "Experimental native FreeRDP session source passed ARM11 syntax check"
