#include "loopback_pair.h"

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>

#ifdef __3DS__
#include <3ds.h>

/* Diagnostic only: binds and immediately closes without listening.  Never
 * substitute a LAN-facing address for loopback in the WinPR event transport. */
static int probe_bind_errno(uint32_t address, uint16_t port) {
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    int result = 0;
    struct sockaddr_in testaddr;
    if (fd < 0) return errno;
    memset(&testaddr, 0, sizeof(testaddr));
    testaddr.sin_family = AF_INET;
    testaddr.sin_addr.s_addr = htonl(address);
    testaddr.sin_port = htons(port);
    if (bind(fd, (struct sockaddr *)&testaddr, sizeof(testaddr)) < 0)
        result = errno;
    close(fd);
    return result;
}

/* Show whether EINVAL comes from loopback itself or port-zero binding.
 * A bind to ANY or Wi-Fi IP is used ONLY for this short diagnostic. */
static void report_loopback_bind_failure(char *message, size_t length, int loop_err) {
    struct in_addr ip = {0}, mask = {0}, broadcast = {0};
    int fixed = probe_bind_errno(INADDR_LOOPBACK, 49152);
    int any = probe_bind_errno(INADDR_ANY, 0);
    int wifi = -1;
    if (SOCU_GetIPInfo(&ip, &mask, &broadcast) == 0 && ip.s_addr != 0)
        wifi = probe_bind_errno(ntohl(ip.s_addr), 0);
    if (message && length)
        snprintf(message, length, "lo0:%d loP:%d any:%d wifi:%d",
                 loop_err, fixed, any, wifi);
}
#endif

static void diagnostic(char *error, size_t size, const char *stage) {
    if (error && size) snprintf(error, size, "%s (errno=%d)", stage, errno);
}

void loopback_pair_init(LoopbackPair *pair) {
    if (!pair) return;
    pair->read_fd = -1;
    pair->write_fd = -1;
}

void loopback_pair_close(LoopbackPair *pair) {
    if (!pair) return;
    if (pair->read_fd >= 0) close(pair->read_fd);
    if (pair->write_fd >= 0) close(pair->write_fd);
    loopback_pair_init(pair);
}

static int ready(int fd, bool read_ready) {
    fd_set fds;
    struct timeval tv;
    int result;
    tv.tv_sec = 1;
    tv.tv_usec = 0;
    do {
        FD_ZERO(&fds);
        FD_SET(fd, &fds);
        result = select(fd + 1, read_ready ? &fds : NULL,
                        read_ready ? NULL : &fds, NULL, &tv);
    } while (result < 0 && errno == EINTR);
    return result;
}

static bool nonblocking(int fd) {
    int flags = fcntl(fd, F_GETFL, 0);
    return flags >= 0 && fcntl(fd, F_SETFL, flags | O_NONBLOCK) == 0;
}

bool loopback_pair_open(LoopbackPair *pair, char *error, size_t error_len) {
    int listener = -1, client = -1, accepted = -1, rc, so_error = 0;
    bool used_fixed_port = false;
    socklen_t length;
    struct sockaddr_in addr;

    if (!pair) {
        diagnostic(error, error_len, "Missing pair");
        return false;
    }
    loopback_pair_init(pair);
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(0);
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);

    listener = socket(AF_INET, SOCK_STREAM, 0);
    if (listener < 0) { diagnostic(error, error_len, "listen socket"); goto fail; }
    if (bind(listener, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        const int loop_err = errno;
#ifdef __3DS__
        /* Some SOC implementations reject port zero. Try loopback ONLY
         * with short-lived fixed high ports; never expose the listener
         * by falling back to INADDR_ANY or the Wi-Fi interface. */
        if (loop_err == EINVAL) {
            uint16_t port;
            for (port = 49152; port < 49184; ++port) {
                addr.sin_port = htons(port);
                if (bind(listener, (struct sockaddr *)&addr, sizeof(addr)) == 0) {
                    used_fixed_port = true;
                    break;
                }
            }
        }
        if (!used_fixed_port)
            report_loopback_bind_failure(error, error_len, loop_err);
#else
        diagnostic(error, error_len, "loopback bind");
#endif
        if (!used_fixed_port) goto fail;
    }
    if (listen(listener, 1) < 0) { diagnostic(error, error_len, "listen"); goto fail; }
    length = (socklen_t)sizeof(addr);
    if (getsockname(listener, (struct sockaddr *)&addr, &length) < 0 ||
        length < (socklen_t)sizeof(addr) || addr.sin_port == 0) {
        diagnostic(error, error_len, "getsockname"); goto fail;
    }

    client = socket(AF_INET, SOCK_STREAM, 0);
    if (client < 0) { diagnostic(error, error_len, "client socket"); goto fail; }
    if (!nonblocking(client)) { diagnostic(error, error_len, "client nonblocking"); goto fail; }
    rc = connect(client, (struct sockaddr *)&addr, sizeof(addr));
    if (rc < 0 && errno != EINPROGRESS && errno != EWOULDBLOCK) {
        diagnostic(error, error_len, "loopback connect"); goto fail;
    }
    if (rc < 0) {
        if (ready(client, false) <= 0) {
            diagnostic(error, error_len, "connect timeout"); goto fail;
        }
        length = (socklen_t)sizeof(so_error);
        if (getsockopt(client, SOL_SOCKET, SO_ERROR, &so_error, &length) < 0 || so_error != 0) {
            if (so_error) errno = so_error;
            diagnostic(error, error_len, "connect SO_ERROR"); goto fail;
        }
    }

    if (ready(listener, true) <= 0) { diagnostic(error, error_len, "accept timeout"); goto fail; }
    accepted = accept(listener, NULL, NULL);
    if (accepted < 0) { diagnostic(error, error_len, "accept"); goto fail; }
    if (!nonblocking(accepted)) { diagnostic(error, error_len, "reader nonblocking"); goto fail; }

    close(listener);
    pair->read_fd = accepted;
    pair->write_fd = client;
    if (error && error_len) {
        if (used_fixed_port)
            snprintf(error, error_len, "Fixed loopback port %u",
                     (unsigned)ntohs(addr.sin_port));
        else
            error[0] = 0;
    }
    return true;

fail:
    if (accepted >= 0) close(accepted);
    if (client >= 0) close(client);
    if (listener >= 0) close(listener);
    return false;
}

bool loopback_pair_selftest(char *error, size_t error_len) {
    LoopbackPair pair;
    struct timeval tv = { 0, 0 };
    fd_set rset;
    unsigned char byte = 0xa5, received = 0;
    bool ok = false;
    int rc;
    char selected_port[64] = {0};
    loopback_pair_init(&pair);
    if (!loopback_pair_open(&pair, error, error_len)) return false;
    if (error && error_len)
        snprintf(selected_port, sizeof(selected_port), "%s", error);
    FD_ZERO(&rset);
    FD_SET(pair.read_fd, &rset);
    rc = select(pair.read_fd + 1, &rset, NULL, NULL, &tv);
    if (rc != 0) { diagnostic(error, error_len, "not initially idle"); goto done; }
    if (send(pair.write_fd, &byte, 1, 0) != 1) {
        diagnostic(error, error_len, "send wake byte"); goto done;
    }
    if (ready(pair.read_fd, true) != 1) {
        diagnostic(error, error_len, "select did not wake"); goto done;
    }
    if (recv(pair.read_fd, &received, 1, 0) != 1 || received != byte) {
        diagnostic(error, error_len, "receive wake byte"); goto done;
    }
    FD_ZERO(&rset);
    FD_SET(pair.read_fd, &rset);
    tv.tv_sec = 0; tv.tv_usec = 0;
    rc = select(pair.read_fd + 1, &rset, NULL, NULL, &tv);
    if (rc != 0) { diagnostic(error, error_len, "not drained"); goto done; }
    ok = true;
    if (error && error_len)
        snprintf(error, error_len, "%s", selected_port[0] ? "SOC fixed-port/select PASS" : "Loopback SOC/select PASS");

done:
    loopback_pair_close(&pair);
    return ok;
}
