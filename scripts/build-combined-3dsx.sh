#!/usr/bin/env bash
# Build the experimental O3DS application linked against the FreeRDP core,
# WinPR and PS-backed OpenSSL. This script never executes the .3dsx on hardware.
set -euo pipefail
root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
: "${DEVKITPRO:?Set DEVKITPRO to your devkitPro installation}"
: "${DEVKITARM:=$DEVKITPRO/devkitARM}"
export DEVKITPRO DEVKITARM
for archive in \
  "$root/build/freerdp-3ds/libfreerdp/libfreerdp3.a" \
  "$root/build/freerdp-3ds/winpr/libwinpr/libwinpr3.a" \
  "$root/build/openssl-3ds/libcrypto.a" \
  "$root/build/openssl-3ds/libssl.a"; do
    [[ -s "$archive" ]] || { echo "Missing $archive" >&2; exit 2; }
done
make -C "$root" -j"${JOBS:-2}" \
  BUILD=build-full TARGET=3ds-rdp-full RDP_FULL_CLIENT=1
test -s "$root/3ds-rdp-full.3dsx"
echo "Built 3ds-rdp-full.3dsx (not hardware tested; TLS trust not configured)"
