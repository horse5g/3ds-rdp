#!/usr/bin/env bash
# Independent link probe; do not run until a secure entropy provider and
# certificate trust policy have been integrated and tested on hardware.
set -euo pipefail
root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
: "${DEVKITPRO:?Set DEVKITPRO to your devkitPro install}"
: "${DEVKITARM:=$DEVKITPRO/devkitARM}"
build="${OPENSSL_BUILD_DIR:-$root/build/openssl-3ds}"
mode="${1:-link}"
case "$mode" in
  link)
    source_file="$root/tests/openssl-arm11-link.c"
    output="$build/openssl-link-probe"
    ;;
  runtime)
    source_file="$root/tests/openssl-arm11-runtime.c"
    output="$build/openssl-rng-smoke"
    command -v 3dsxtool >/dev/null || { echo "Missing 3dsxtool" >&2; exit 2; }
    ;;
  *)
    echo "Usage: link-openssl-3ds.sh [link|runtime]" >&2
    exit 2
    ;;
esac
for lib in libcrypto.a libssl.a; do
  test -s "$build/$lib" || { echo "Missing $build/$lib" >&2; exit 2; }
done
test -d "$build/include/openssl" || {
  echo "Missing generated OpenSSL headers: $build/include/openssl" >&2; exit 2;
}
mkdir -p "$build"
"$DEVKITARM/bin/arm-none-eabi-gcc" \
  -march=armv6k -mtune=mpcore -mfloat-abi=hard -mtp=soft \
  -Os -ffunction-sections -fdata-sections -D__3DS__ \
  -I"$DEVKITPRO/libctru/include" \
  -I"$root/include" \
  -I"$build/include" -I"$root/external/openssl/include" \
  -specs=3dsx.specs -Wl,--gc-sections \
  -Wl,-Map,"$output.map" \
  "$source_file" "$root/source/rdp_pthread_tsd.c" \
  -L"$build" -L"$DEVKITPRO/libctru/lib" \
  -Wl,--start-group -lssl -lcrypto -lctru -lm -Wl,--end-group \
  -o "$output.elf"
if [[ "$mode" == runtime ]]; then
  3dsxtool "$output.elf" "$output.3dsx"
  test -s "$output.3dsx"
  echo "Built OpenSSL RNG hardware smoke test: $output.3dsx (not executed)"
else
  echo "Native OpenSSL SSL_CTX/X.509 link succeeded (not yet executed on 3DS)."
fi
