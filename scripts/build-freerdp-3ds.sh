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
backend="${FREERDP_CRYPTO_BACKEND:-mbedtls}"
case "$backend" in
  mbedtls|openssl) ;;
  *) echo "FREERDP_CRYPTO_BACKEND must be mbedtls or openssl" >&2; exit 2 ;;
esac
case "${1:-configure}" in configure|build) ;; *) echo 'Usage: build-freerdp-3ds.sh [configure|build]' >&2; exit 2;; esac
command -v cmake >/dev/null || { echo 'cmake required' >&2; exit 2; }
command -v git >/dev/null || { echo 'git required' >&2; exit 2; }
[[ -x "$DEVKITARM/bin/arm-none-eabi-gcc" ]] || { echo 'devkitARM gcc not found' >&2; exit 2; }
crypto_cmake_args=()
if [[ "$backend" == openssl ]]; then
  openssl_src="${OPENSSL_SOURCE:-$root/external/openssl}"
  openssl_build="${OPENSSL_BUILD_DIR:-$root/build/openssl-3ds}"
  for lib in libssl.a libcrypto.a; do
    [[ -s "$openssl_build/$lib" ]] || {
      echo "Build OpenSSL first: missing $openssl_build/$lib" >&2
      exit 2
    }
  done
  [[ -r "$openssl_build/include/openssl/ssl.h" && -r "$openssl_build/include/openssl/configuration.h" && -d "$openssl_src/include/openssl" ]] || {
    echo "Missing source or generated ARM11 OpenSSL headers" >&2
    exit 2
  }
  crypto_cmake_args=(
    -DWITH_OPENSSL=ON
    -DWITH_MBEDTLS=OFF
    -DOPENSSL_USE_STATIC_LIBS=TRUE
    -DOPENSSL_ROOT_DIR="$openssl_build"
    -DOPENSSL_INCLUDE_DIR="$openssl_build/include"
    -DOPENSSL_SSL_LIBRARY="$openssl_build/libssl.a"
    -DOPENSSL_CRYPTO_LIBRARY="$openssl_build/libcrypto.a"
    -DOPENSSL_3DS_GENERATED_INCLUDE="$openssl_build/include"
    -DOPENSSL_3DS_SOURCE_INCLUDE="$openssl_src/include"
  )
else
  [[ -r "$portlibs/include/mbedtls/ssl.h" ]] || { echo 'Install 3ds-mbedtls first' >&2; exit 2; }
  for lib in mbedtls mbedx509 mbedcrypto; do
    [[ -f "$portlibs/lib/lib${lib}.a" ]] || { echo "Missing $portlibs/lib/lib${lib}.a" >&2; exit 2; }
  done
  crypto_cmake_args=(
    -DWITH_MBEDTLS=ON
    -DWITH_OPENSSL=OFF
    -DMBEDTLS_ROOT="$portlibs"
    -DMBEDTLS_INCLUDE_DIR="$portlibs/include"
    -DMBEDTLS_LIBRARY="$portlibs/lib/libmbedtls.a"
    -DMBEDCRYPTO_LIBRARY="$portlibs/lib/libmbedcrypto.a"
    -DMBEDX509_LIBRARY="$portlibs/lib/libmbedx509.a"
  )
fi
echo "FreeRDP ARM11 crypto backend: $backend"
if [[ ! -f "$src/CMakeLists.txt" ]]; then
  [[ ! -e "$src" ]] || { echo "Source dir exists but no CMakeLists.txt: $src" >&2; exit 2; }
  mkdir -p "$(dirname "$src")"
  git clone --depth 1 --branch "$freerdp_version" https://github.com/FreeRDP/FreeRDP.git "$src"
fi
# Apply a narrow build-system-only patch to our ignored, tagged checkout.
# Never silently alter a custom FREERDP_SOURCE checkout.
if [[ "$src" == "$root/external/FreeRDP" ]]; then
  if [[ "$backend" == mbedtls ]] && ! grep -q '3DS RDP port: allow a toolchain' "$src/CMakeLists.txt"; then
    patch -d "$src" -p1 < "$root/patches/freerdp-3.32.2-mbedtls-only.patch"
  fi
  if ! grep -q '3DS has no AF_UNIX' "$src/winpr/include/winpr/winsock.h"; then
    patch -d "$src" -p1 < "$root/patches/freerdp-3.32.2-3ds-network-headers.patch"
  fi
  if ! grep -q '3DS homebrew has no guaranteed POSIX device hostname' "$src/winpr/libwinpr/sysinfo/sysinfo.c"; then
    patch -d "$src" -p1 < "$root/patches/freerdp-3.32.2-3ds-client-hostname.patch"
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
  if ! grep -q 'FAT/SD file timestamps do not have' "$src/winpr/libwinpr/file/file.c"; then
    patch -d "$src" -p1 < "$root/patches/freerdp-3.32.2-3ds-filetime.patch"
  fi
  if ! grep -q 'The O3DS client does not support serial/COM-port redirection' "$src/winpr/libwinpr/CMakeLists.txt"; then
    patch -d "$src" -p1 < "$root/patches/freerdp-3.32.2-3ds-no-serial.patch"
  fi
  if ! grep -q 'loopback_pair_open' "$src/winpr/libwinpr/synch/event.c"; then
    patch -d "$src" -p1 < "$root/patches/freerdp-3.32.2-3ds-loopback-waits.patch"
  fi
  if ! grep -q '#ifndef __3DS__' "$src/winpr/libwinpr/io/io.c"; then
    patch -d "$src" -p1 < "$root/patches/freerdp-3.32.2-3ds-no-unix-io-header.patch"
  fi
  if ! grep -q 'Compute GMT offset without the nonstandard' "$src/winpr/libwinpr/timezone/timezone.c"; then
    patch -d "$src" -p1 < "$root/patches/freerdp-3.32.2-3ds-portable-timezone.patch"
  fi
  if ! grep -q '!defined(__3DS__) && defined(_POSIX_C_SOURCE)' "$src/winpr/libwinpr/thread/thread.c"; then
    patch -d "$src" -p1 < "$root/patches/freerdp-3.32.2-3ds-thread-priority.patch"
  fi
  if ! grep -q 'Horizon exposes its IPv4 settings through SOC' "$src/winpr/libwinpr/winsock/winsock.c"; then
    patch -d "$src" -p1 < "$root/patches/freerdp-3.32.2-3ds-wsaioctl-ipv4.patch"
  fi
  if ! grep -q 'The target has no matching errno' "$src/winpr/libwinpr/winsock/winsock.c"; then
    patch -d "$src" -p1 < "$root/patches/freerdp-3.32.2-3ds-winsock-compat.patch"
  fi
  if ! grep -q 'devkitARM newlib does not provide POSIX getline' "$src/winpr/libwinpr/crt/string.c"; then
    patch -d "$src" -p1 < "$root/patches/freerdp-3.32.2-3ds-getline.patch"
  fi
  if ! grep -q '!defined(ANDROID) && !defined(__3DS__)' "$src/libfreerdp/utils/passphrase.c"; then
    patch -d "$src" -p1 < "$root/patches/freerdp-3.32.2-3ds-no-tty-passphrase.patch"
  fi
  if ! grep -q 'signal_3ds.c' "$src/libfreerdp/utils/CMakeLists.txt"; then
    patch -d "$src" -p1 < "$root/patches/freerdp-3.32.2-3ds-signals.patch"
  fi
  if [[ "$backend" == openssl ]] && ! grep -q 'unsigned int digest_len = 0;' "$src/libfreerdp/crypto/x509_utils.c"; then
    patch -d "$src" -p1 < "$root/patches/freerdp-3.32.2-3ds-openssl-x509-length.patch"
  fi
  if ! grep -q 'Horizon SOC supports IPv4 only' "$src/libfreerdp/core/utils.c"; then
    patch -d "$src" -p1 < "$root/patches/freerdp-3.32.2-3ds-ipv4-only-utils.patch"
  fi
  if ! grep -q '#ifndef __3DS__' "$src/libfreerdp/core/tcp.c"; then
    patch -d "$src" -p1 < "$root/patches/freerdp-3.32.2-3ds-tcp-network-headers.patch"
  fi
  if ! grep -q 'Horizon SOC does not implement Unix-domain sockets' "$src/libfreerdp/core/tcp.c"; then
    patch -d "$src" -p1 < "$root/patches/freerdp-3.32.2-3ds-tcp-ipv4-soc.patch"
  fi
  # Preserve numeric errno / WSA status at FreeRDP's true TCP failure
  # stage (the public 0x00020006 code is too broad to diagnose alone).
  if ! grep -q 'Optional O3DS TCP diagnostic hook' "$src/libfreerdp/core/tcp.c"; then
    patch -d "$src" -p1 < "$root/patches/freerdp-3.32.2-3ds-tcp-stage-diag.patch"
  fi
  if ! grep -q 'IPv6 subnet matching is unavailable on Horizon' "$src/libfreerdp/core/proxy.c"; then
    patch -d "$src" -p1 < "$root/patches/freerdp-3.32.2-3ds-proxy-ipv4.patch"
  fi
  if ! grep -q 'server-only peer/listener implementations on Horizon SOC' "$src/libfreerdp/core/CMakeLists.txt"; then
    patch -d "$src" -p1 < "$root/patches/freerdp-3.32.2-3ds-no-server-sources.patch"
  fi
  if ! grep -q '^#if !defined(_WIN32) && !defined(__3DS__)' "$src/libfreerdp/core/utils.c"; then
    patch -d "$src" -p1 < "$root/patches/freerdp-3.32.2-3ds-link-libc.patch"
  fi
  if ! grep -q 'The allocation already reserves space for WinPR' "$src/winpr/libwinpr/crt/alignment.c"; then
    patch -d "$src" -p1 < "$root/patches/freerdp-3.32.2-3ds-winpr-alignment.patch"
  fi
  if ! grep -q 'Horizon has no POSIX child processes or waitpid' "$src/winpr/libwinpr/synch/wait.c"; then
    patch -d "$src" -p1 < "$root/patches/freerdp-3.32.2-3ds-process-wait.patch"
  fi
  if ! grep -q 'Do not falsely claim Unix sharing/locking semantics on FAT' "$src/winpr/libwinpr/file/file.c"; then
    patch -d "$src" -p1 < "$root/patches/freerdp-3.32.2-3ds-no-flock.patch"
  fi
  # Context and RDP subtree failures are otherwise flattened to a generic
  # boolean. Keep the last non-sensitive initialization stage after cleanup.
  if ! grep -q 'O3DS diagnostic: preserve the last context construction stage' "$src/libfreerdp/core/freerdp.c"; then
    patch -d "$src" -p1 < "$root/patches/freerdp-3.32.2-3ds-context-stages.patch"
  fi
  if ! grep -q 'O3DS diagnostic: last channel-construction step survives cleanup' "$src/libfreerdp/core/client.c"; then
    patch -d "$src" -p1 < "$root/patches/freerdp-3.32.2-3ds-channels-stages.patch"
  fi
  # A context may fail at the channel-error event and then hang in
  # partial cleanup. Persist low-level numeric breadcrumbs *before* free.
  if ! grep -q 'O3DS_CTX_CHECKPOINT(80, errno)' "$src/libfreerdp/core/freerdp.c"; then
    patch -d "$src" -p1 < "$root/patches/freerdp-3.32.2-3ds-context-cleanup-checkpoints.patch"
  fi
  # O3DS recovery: no optional FreeRDP timer worker, preventing a
  # native thread blocked in WinPR event wait from hanging context cleanup.
  # Preserve this client's timer handle API without extra wake sockets.
  # Replace the older experimental patch on managed tagged source trees.
  if grep -q 'O3DS: keep native timer diagnostic state' "$src/libfreerdp/core/timer.c"; then
    git -C "$src" checkout -- libfreerdp/core/timer.c
  fi
  if ! grep -q 'O3DS diagnostic: keep timer constructor stage after' "$src/libfreerdp/core/timer.c"; then
    patch -d "$src" -p1 < "$root/patches/freerdp-3.32.2-3ds-native-core-timer.patch"
  fi
  # WinPR events on the 3DS use nonblocking SOC loopback sockets.
  if ! grep -q 'SOC loopback buffer full means' "$src/winpr/libwinpr/synch/event.c"; then
    patch -d "$src" -p1 < "$root/patches/freerdp-3.32.2-3ds-event-hardening.patch"
  fi
  # Ordinary events should not consume SOC sockets until a wait requests
  # an event FD. Without this, C9 exhausts socket/FD resources on O3DS.
  if ! grep -q 'Prevent idle events from consuming two SOC sockets each' "$src/winpr/libwinpr/synch/event.c"; then
    patch -d "$src" -p1 < "$root/patches/freerdp-3.32.2-3ds-lazy-events.patch"
  fi
else
  echo "Custom FreeRDP source: experimental 3DS patches are not automatically applied."
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
  "${crypto_cmake_args[@]}" \
  -DWITH_UNICODE_BUILTIN=ON \
  -DWINPR_HAVE_PIPE2=OFF \
  -DWINPR_TARGET_3DS=ON \
  -DWINPR_3DS_ROOT="$root" \
  -DWINPR_HAVE_PTHREAD_MUTEX_TIMEDLOCK=OFF \
  -DWINPR_HAVE_PTHREAD_MUTEX_TIMEDLOCK_SYMBOL=OFF \
  -DWINPR_HAVE_PTHREAD_MUTEX_TIMEDLOCK_LIB=OFF \
  -DWINPR_HAVE_PTHREAD_MUTEX_TIMEDLOCK_LIBS=OFF \
  -DTIMER_CREATE=OFF \
  -DTIMER_DELETE=OFF \
  -DTIMER_SETTIME=OFF \
  -DTIMER_GETTIME=OFF \
  -DUSE_UNWIND=OFF \
  -DWITH_WINPR_TOOLS=OFF \
  -DWITH_CLIENT=OFF \
  -DWITH_CLIENT_COMMON=OFF \
  -DWITH_SERVER=OFF \
  -DWITH_CHANNELS=OFF \
  -DWITH_SAMPLE=OFF \
  -DWITH_MANPAGES=OFF \
  -DWITH_PKCS11=OFF \
  -DWITH_SMARTCARD_EMULATE=OFF \
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
