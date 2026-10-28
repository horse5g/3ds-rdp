#!/usr/bin/env bash
# Static linker feasibility check for the ARM11 FreeRDP/OpenSSL core.
# No remote endpoint is contacted by CI and no on-device tests are run.
set -euo pipefail
root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
: "${DEVKITPRO:?Set DEVKITPRO to the devkitPro directory}"
: "${DEVKITARM:=$DEVKITPRO/devkitARM}"
frbuild="${FREERDP_BUILD_DIR:-$root/build/freerdp-3ds}"
sslbuild="${OPENSSL_BUILD_DIR:-$root/build/openssl-3ds}"
frsrc="${FREERDP_SOURCE:-$root/external/FreeRDP}"
sslsrc="${OPENSSL_SOURCE:-$root/external/openssl}"
out="$frbuild/combined-arm11-link"
cc="$DEVKITARM/bin/arm-none-eabi-gcc"
[[ -x "$cc" ]] || { echo "Missing devkitARM compiler" >&2; exit 2; }

# Search both the usual FreeRDP CMake output and a flattened static build.
find_archive() {
  local result
  result="$(find "$frbuild" -type f -name "$1" -print -quit)"
  [[ -n "$result" && -s "$result" ]] || {
    echo "Missing completed $1 in $frbuild" >&2
    return 1
  }
  printf '%s\n' "$result"
}
freerdp_archive="$(find_archive libfreerdp3.a)"
winpr_archive="$(find_archive libwinpr3.a)"
for lib in libssl.a libcrypto.a; do
  [[ -s "$sslbuild/$lib" ]] || { echo "Missing ARM11 $lib" >&2; exit 2; }
done
[[ -r "$sslbuild/include/openssl/configuration.h" ]] || {
  echo "Missing generated OpenSSL headers" >&2
  exit 2
}
mkdir -p "$frbuild"

"$cc" -march=armv6k -mtune=mpcore -mfloat-abi=hard -mtp=soft \
  -D__3DS__ -Os -ffunction-sections -fdata-sections \
  -I"$DEVKITPRO/libctru/include" -I"$root/include" \
  -I"$frsrc/include" -I"$frsrc/winpr/include" \
  -I"$frbuild" -I"$frbuild/include" \
  -I"$frbuild/winpr" -I"$frbuild/winpr/include" \
  -I"$sslbuild/include" -I"$sslsrc/include" \
  -specs=3dsx.specs -Wl,--gc-sections \
  -Wl,-Map,"$out.map" \
  "$root/tests/freerdp-arm11-link.c" \
  -L"$DEVKITPRO/libctru/lib" -L"$DEVKITPRO/portlibs/3ds/lib" \
  -Wl,--start-group \
    "$freerdp_archive" "$winpr_archive" \
    "$sslbuild/libssl.a" "$sslbuild/libcrypto.a" \
    -lz -lctru -lm \
  -Wl,--end-group -o "$out.elf"

test -s "$out.elf"
if command -v 3dsxtool >/dev/null; then
  3dsxtool "$out.elf" "$out.3dsx"
  test -s "$out.3dsx"
fi
echo "ARM11 core/crypto combined ELF link PASSED (not hardware tested)"
