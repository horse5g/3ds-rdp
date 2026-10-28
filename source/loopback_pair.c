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
        diagnostic(error, error_len, "loopback bind"); goto fail;
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
    if (error && error_len) error[0] = 0;
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
    loopback_pair_init(&pair);
    if (!loopback_pair_open(&pair, error, error_len)) return false;
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
    if (error && error_len) snprintf(error, error_len, "Loopback SOC/select PASS");

done:
    loopback_pair_close(&pair);
    return ok;
}
