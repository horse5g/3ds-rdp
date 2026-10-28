#include "config.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CONFIG_LINE_CAP 512

static int fail(char *error, size_t cap, const char *message) {
    if (error && cap) snprintf(error, cap, "%s", message);
    return -1;
}

static int eq_ci(const char *a, const char *b) {
    while (*a && *b) {
        if (tolower((unsigned char)*a) != tolower((unsigned char)*b)) return 0;
        ++a;
        ++b;
    }
    return *a == 0 && *b == 0;
}

static char *trim(char *p) {
    char *end;
    while (*p && isspace((unsigned char)*p)) ++p;
    end = p + strlen(p);
    while (end > p && isspace((unsigned char)end[-1])) *--end = 0;
    return p;
}

static int assign(char *dst, size_t cap, const char *src) {
    size_t len = strlen(src);
    if (len == 0 || len >= cap) return -1;
    memcpy(dst, src, len + 1);
    return 0;
}

static int domain_host_chars(const char *s) {
    const unsigned char *p = (const unsigned char *)s;
    if (!*p) return 0;
    for (; *p; ++p)
        if (!(isalnum(*p) || *p == '.' || *p == '-' || *p == '_')) return 0;
    return 1;
}

static int valid_username(const char *s) {
    const unsigned char *p = (const unsigned char *)s;
    if (!*p) return 0;
    for (; *p; ++p)
        if (*p < 32 || *p == 127 || *p == '/' || *p == ':' || *p == '\\') return 0;
    return 1;
}

static int parse_port(const char *s, uint16_t *port) {
    unsigned long value = 0;
    const unsigned char *p = (const unsigned char *)s;
    if (!*p) return -1;
    for (; *p; ++p) {
        if (*p < '0' || *p > '9') return -1;
        value = value * 10 + (*p - '0');
        if (value > 65535) return -1;
    }
    if (!value) return -1;
    *port = (uint16_t)value;
    return 0;
}

static int set_legacy(const char *value, RdpConfig *cfg, char *error, size_t n) {
    char buffer[CONFIG_LINE_CAP];
    char *colon;
    if (assign(buffer, sizeof(buffer), value)) return fail(error, n, "Legacy host line too long");
    colon = strrchr(buffer, ':');
    if (colon) {
        *colon++ = 0;
        if (parse_port(colon, &cfg->port)) return fail(error, n, "Invalid TCP port");
    }
    if (!domain_host_chars(buffer) || assign(cfg->host, sizeof(cfg->host), buffer))
        return fail(error, n, "Invalid hostname or IPv4 address");
    return 0;
}

int rdp_config_parse(const char *text, RdpConfig *out, char *error, size_t error_size) {
    RdpConfig cfg = {0};
    const char *cursor;
    unsigned flags = 0;
    int mode = 0;
    enum { HOST = 1, PORT = 2, USER = 4, DOMAIN = 8 };
    if (!text || !out) return fail(error, error_size, "Invalid config arguments");
    cfg.port = 3389;
    cursor = text;
    while (*cursor) {
        char line[CONFIG_LINE_CAP];
        const char *eol = strchr(cursor, '\n');
        size_t n = eol ? (size_t)(eol - cursor) : strlen(cursor);
        char *p, *sep, *key, *value;
        if (n >= sizeof(line)) return fail(error, error_size, "Config line too long");
        memcpy(line, cursor, n);
        line[n] = 0;
        cursor = eol ? eol + 1 : cursor + n;
        p = trim(line);
        if (!*p || *p == '#' || *p == ';') continue;
        sep = strchr(p, '=');
        if (!sep) {
            if (mode != 0 || flags) return fail(error, error_size, "Cannot mix legacy and key=value lines");
            mode = 1;
            if (set_legacy(p, &cfg, error, error_size)) return -1;
            flags |= HOST;
            continue;
        }
        if (mode == 1) return fail(error, error_size, "Cannot mix legacy and key=value lines");
        mode = 2;
        *sep++ = 0;
        key = trim(p);
        value = trim(sep);
        if (eq_ci(key, "host")) {
            if (flags & HOST) return fail(error, error_size, "Duplicate host");
            if (!domain_host_chars(value) || assign(cfg.host, sizeof(cfg.host), value))
                return fail(error, error_size, "Invalid hostname or IPv4 address");
            flags |= HOST;
        } else if (eq_ci(key, "port")) {
            if (flags & PORT) return fail(error, error_size, "Duplicate port");
            if (parse_port(value, &cfg.port)) return fail(error, error_size, "Invalid TCP port");
            flags |= PORT;
        } else if (eq_ci(key, "username")) {
            if (flags & USER) return fail(error, error_size, "Duplicate username");
            if (assign(cfg.username, sizeof(cfg.username), value)) return fail(error, error_size, "Invalid username length");
            flags |= USER;
        } else if (eq_ci(key, "domain")) {
            if (flags & DOMAIN) return fail(error, error_size, "Duplicate domain");
            if (!domain_host_chars(value) || assign(cfg.domain, sizeof(cfg.domain), value))
                return fail(error, error_size, "Invalid Windows domain");
            flags |= DOMAIN;
        } else {
            return fail(error, error_size, "Unknown config option (passwords forbidden)");
        }
    }
    if (!(flags & HOST)) return fail(error, error_size, "Missing host");
    if (flags & USER) {
        char *slash = strchr(cfg.username, '\\');
        if (slash) {
            char split_domain[RDP_DOMAIN_CAP], split_user[RDP_USER_CAP];
            *slash++ = 0;
            if (!domain_host_chars(cfg.username) || !valid_username(slash))
                return fail(error, error_size, "Invalid DOMAIN\\username");
            if (assign(split_domain, sizeof(split_domain), cfg.username) ||
                assign(split_user, sizeof(split_user), slash))
                return fail(error, error_size, "Invalid DOMAIN\\username length");
            if ((flags & DOMAIN) && !eq_ci(cfg.domain, split_domain))
                return fail(error, error_size, "Domain conflicts with username");
            strcpy(cfg.domain, split_domain);
            strcpy(cfg.username, split_user);
        } else if (strchr(cfg.username, '@')) {
            char *at = strchr(cfg.username, '@');
            if (at == cfg.username || !domain_host_chars(at + 1) ||
                strchr(at + 1, '@') || (flags & DOMAIN) ||
                !valid_username(cfg.username))
                return fail(error, error_size, "Invalid UPN or conflicting domain");
        } else if (!valid_username(cfg.username)) {
            return fail(error, error_size, "Invalid username");
        }
    }
    if (error && error_size) error[0] = 0;
    *out = cfg;
    return 0;
}
