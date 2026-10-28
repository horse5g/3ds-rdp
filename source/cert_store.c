#include "cert_store.h"
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#define STORE_BYTES (RDP_CERT_POOL_MAX * RDP_CERT_POOL_LINE_MAX + 1024)
#define STORE_PATH_CAP 512

static int path_with_suffix(const char *path, const char *suffix,
                            char out[STORE_PATH_CAP])
{
    if (!path || !*path || !suffix)
        return -1;
    const int n = snprintf(out, STORE_PATH_CAP, "%s%s", path, suffix);
    return n > 0 && n < STORE_PATH_CAP ? 0 : -1;
}

/* 1=exists, 0=absent, -1=unexpected filesystem error. */
static int path_exists(const char *path)
{
    struct stat st;
    if (stat(path, &st) == 0)
        return 1;
    return errno == ENOENT ? 0 : -1;
}

static int read_pool(const char *path, RdpCertPool *out)
{
    char *text = NULL;
    FILE *f = NULL;
    size_t n;
    int ok = -1;
    if (!out)
        return -1;
    f = fopen(path, "rb");
    if (!f) {
        if (errno != ENOENT)
            return -1;
        memset(out, 0, sizeof(*out));
        return 0;
    }
    text = (char *)malloc(STORE_BYTES + 1);
    if (!text)
        goto finish;
    n = fread(text, 1, STORE_BYTES + 1, f);
    if (ferror(f) || n > STORE_BYTES || memchr(text, 0, n))
        goto finish;
    text[n] = 0;
    if (rdp_cert_pool_parse(text, out) == 0)
        ok = 0;
finish:
    if (fclose(f) != 0)
        ok = -1;
    free(text);
    return ok;
}

int rdp_cert_store_load(const char *path, RdpCertPool *out)
{
    char pending[STORE_PATH_CAP], backup[STORE_PATH_CAP];
    if (!out || path_with_suffix(path, ".tmp", pending) ||
        path_with_suffix(path, ".bak", backup))
        return -1;
    /* A partial transaction must not be mistaken for an empty trust store. */
    if (path_exists(pending) != 0 || path_exists(backup) != 0)
        return -1;
    return read_pool(path, out);
}

static int pool_equal(const RdpCertPool *a, const RdpCertPool *b)
{
    if (a->count != b->count)
        return 0;
    for (size_t i = 0; i < a->count; ++i) {
        const RdpCertPin *p = &a->entries[i];
        if (rdp_cert_pool_lookup(b, p->host, p->port, p->sha256) != 1)
            return 0;
    }
    return 1;
}

int rdp_cert_store_remove(const char *path, size_t index,
                          const RdpCertPin *expected)
{
    char pending[STORE_PATH_CAP], backup[STORE_PATH_CAP];
    RdpCertPool *source = NULL, *desired = NULL, *checked = NULL;
    FILE *out = NULL;
    int result = -1, promoted = 0, backup_moved = 0;
    if (!expected || path_with_suffix(path, ".tmp", pending) ||
        path_with_suffix(path, ".bak", backup))
        return -1;

    source = (RdpCertPool *)malloc(sizeof(*source));
    desired = (RdpCertPool *)malloc(sizeof(*desired));
    checked = (RdpCertPool *)malloc(sizeof(*checked));
    if (!source || !desired || !checked ||
        rdp_cert_store_load(path, source) != 0 ||
        index >= source->count ||
        !rdp_cert_pin_matches(&source->entries[index],
                              expected->host, expected->port) ||
        memcmp(source->entries[index].sha256, expected->sha256, 32) != 0)
        goto finish;

    *desired = *source;
    for (size_t i = index; i + 1 < desired->count; ++i)
        desired->entries[i] = desired->entries[i + 1];
    desired->count--;
    memset(&desired->entries[desired->count], 0,
           sizeof(desired->entries[desired->count]));

    out = fopen(pending, "wb");
    if (!out)
        goto finish;
    for (size_t i = 0; i < desired->count; ++i) {
        char line[RDP_CERT_POOL_LINE_MAX];
        const int size = rdp_cert_pool_format(&desired->entries[i], line, sizeof(line));
        if (size <= 0 || fwrite(line, 1, (size_t)size, out) != (size_t)size)
            goto finish;
    }
    if (fflush(out) != 0)
        goto finish;
    if (fclose(out) != 0) {
        out = NULL;
        goto finish;
    }
    out = NULL;

    if (read_pool(pending, checked) != 0 || !pool_equal(desired, checked))
        goto finish;
    if (rename(path, backup) != 0)
        goto finish;
    backup_moved = 1;

    if (rename(pending, path) != 0)
        goto finish;
    promoted = 1;
    if (read_pool(path, checked) != 0 || !pool_equal(desired, checked))
        goto finish;
    if (remove(backup) != 0)
        goto finish;
    backup_moved = 0;
    result = 0;

finish:
    if (out)
        fclose(out);
    if (result != 0) {
        /* Best effort rollback; leave .bak present on rollback failure so
         * subsequent loads refuse any potentially incomplete trust store. */
        if (backup_moved) {
            if (promoted)
                remove(path);
            if (rename(backup, path) == 0)
                backup_moved = 0;
        }
        if (!backup_moved)
            remove(pending);
    }
    free(source);
    free(desired);
    free(checked);
    return result;
}
