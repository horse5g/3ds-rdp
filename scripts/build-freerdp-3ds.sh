#!/usr/bin/env bash
# EXPERIMENTAL: configure and attempt compilation of WinPR/FreeRDP for O3DS.
# A failed build is expected until Horizon OS compatibility gaps are addressed.
set -euo pipefail
root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
: "${DEVKITPRO:?Set DEVKITPRO, e.g. /opt/devkitpro}"
: "${DEVKITARM:=${DEVKITPRO}/devkitARM}"
export DEVKITPRO DEVKITARM
freerdp_version=3.32.2
src="${FREERDP_SOURCE:-$root/external/FreeRDP}"
build="${FREERDP_BUILD_DIR:-$root/build/freerdp-3ds}"
portlibs="${PORTLIBS_PREFIX:-$DEVKITPRO/portlibs/3ds}"
case "${1:-configure}" in configure|build) ;; *) echo 'Usage: build-freerdp-3ds.sh [configure|build]' >&2; exit 2;; esac
command -v cmake >/dev/null || { echo 'cmake required' >&2; exit 2; }
command -v git >/dev/null || { echo 'git required' >&2; exit 2; }
[[ -x "$DEVKITARM/bin/arm-none-eabi-gcc" ]] || { echo 'devkitARM gcc not found' >&2; exit 2; }
[[ -r "$portlibs/include/mbedtls/ssl.h" ]] || { echo 'Install 3ds-mbedtls first' >&2; exit 2; }
for lib in mbedtls mbedx509 mbedcrypto; do
  [[ -f "$portlibs/lib/lib${lib}.a" ]] || { echo "Missing $portlibs/lib/lib${lib}.a" >&2; exit 2; }
done
if [[ ! -f "$src/CMakeLists.txt" ]]; then
  [[ ! -e "$src" ]] || { echo "Source dir exists but no CMakeLists.txt: $src" >&2; exit 2; }
  mkdir -p "$(dirname "$src")"
  git clone --depth 1 --branch "$freerdp_version" https://github.com/FreeRDP/FreeRDP.git "$src"
fi
# Apply a narrow build-system-only patch to our ignored, tagged checkout.
# Never silently alter a custom FREERDP_SOURCE checkout.
if [[ "$src" == "$root/external/FreeRDP" ]]; then
  if ! grep -q '3DS RDP port: allow a toolchain' "$src/CMakeLists.txt"; then
    patch -d "$src" -p1 < "$root/patches/freerdp-3.32.2-mbedtls-only.patch"
  fi
  if ! grep -q '3DS has no AF_UNIX' "$src/winpr/include/winpr/winsock.h"; then
    patch -d "$src" -p1 < "$root/patches/freerdp-3.32.2-3ds-network-headers.patch"
  fi
  if ! grep -q 'libctru socket handles are signed ints' "$src/winpr/include/winpr/winsock.h"; then
    patch -d "$src" -p1 < "$root/patches/freerdp-3.32.2-3ds-socket-types.patch"
  fi
  if ! grep -q "Horizon's pthreads do not expose POSIX" "$src/winpr/libwinpr/synch/timer.c"; then
    patch -d "$src" -p1 < "$root/patches/freerdp-3.32.2-3ds-timer-thread.patch"
  fi
  if ! grep -q 'Dynamic libraries are not supported in this 3DS static build' "$src/winpr/libwinpr/library/library.c"; then
    patch -d "$src" -p1 < "$root/patches/freerdp-3.32.2-3ds-static-loader.patch"
  fi
  if ! grep -q '#ifndef __3DS__' "$src/winpr/libwinpr/file/generic.c"; then
    patch -d "$src" -p1 < "$root/patches/freerdp-3.32.2-3ds-no-unix-file-sockets.patch"
  fi
  if ! grep -q 'Named-pipe client requires Unix-domain sockets' "$src/winpr/libwinpr/file/namedPipeClient.c"; then
    patch -d "$src" -p1 < "$root/patches/freerdp-3.32.2-3ds-no-named-pipe-client.patch"
  fi
else
  echo "Custom FreeRDP source: upstream CMake Mbed TLS-only patch was not applied."
fi
mkdir -p "$build"
cmake -S "$src" -B "$build" \
  -DCMAKE_TOOLCHAIN_FILE="$root/cmake/3ds-armv6k.cmake" \
  -DCMAKE_BUILD_TYPE=MinSizeRel \
  -DCMAKE_INSTALL_PREFIX="$build/install" \
  -DCMAKE_EXPORT_COMPILE_COMMANDS=ON \
  -DBUILD_SHARED_LIBS=OFF \
  -DBUILD_TESTING=OFF \
  -DBUILD_TESTING_INTERNAL=OFF \
  -DWITH_MBEDTLS=ON \
  -DWITH_OPENSSL=OFF \
  -DMBEDTLS_ROOT="$portlibs" \
  -DMBEDTLS_INCLUDE_DIR="$portlibs/include" \
  -DMBEDTLS_LIBRARY="$portlibs/lib/libmbedtls.a" \
  -DMBEDCRYPTO_LIBRARY="$portlibs/lib/libmbedcrypto.a" \
  -DMBEDX509_LIBRARY="$portlibs/lib/libmbedx509.a" \
  -DWITH_UNICODE_BUILTIN=ON \
  -DWINPR_HAVE_PIPE2=OFF \
  -DWINPR_HAVE_PTHREAD_MUTEX_TIMEDLOCK=OFF \
  -DWINPR_HAVE_PTHREAD_MUTEX_TIMEDLOCK_SYMBOL=OFF \
  -DWINPR_HAVE_PTHREAD_MUTEX_TIMEDLOCK_LIB=OFF \
  -DWINPR_HAVE_PTHREAD_MUTEX_TIMEDLOCK_LIBS=OFF \
  -DTIMER_CREATE=OFF \
  -DTIMER_DELETE=OFF \
  -DTIMER_SETTIME=OFF \
  -DTIMER_GETTIME=OFF \
  -DWITH_WINPR_TOOLS=OFF \
  -DWITH_CLIENT=OFF \
  -DWITH_CLIENT_COMMON=OFF \
  -DWITH_SERVER=OFF \
  -DWITH_CHANNELS=OFF \
  -DWITH_SAMPLE=OFF \
  -DWITH_MANPAGES=OFF \
  -DWITH_PKCS11=OFF \
  -DWITH_KRB5=OFF \
  -DWITH_GSSAPI=OFF \
  -DWITH_X11=OFF \
  -DWITH_WAYLAND=OFF \
  -DWITH_JPEG=OFF \
  -DWITH_CUPS=OFF \
  -DWITH_FFMPEG=OFF \
  -DWITH_SWSCALE=OFF \
  -DWITH_CAIRO=OFF \
  -DWITH_ALSA=OFF \
  -DWITH_PULSE=OFF \
  -DWITH_GSTREAMER_1_0=OFF \
  -DWITH_OPENH264=OFF \
  -DWITH_JSON_DISABLED=ON
if [[ "${1:-configure}" == build ]]; then
  cmake --build "$build" --parallel "${JOBS:-2}"
fi
