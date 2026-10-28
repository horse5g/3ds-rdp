#include "profile_store.h"

#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define PROFILE_LINE_CAP 1024
#define PROFILE_SIGNATURE "3DSRDP1"

static int problem(char *error, size_t cap, const char *reason)
{
    if (error && cap) snprintf(error, cap, "%s", reason);
    return -1;
}

static int safe_field(const char *s, size_t cap, int allow_empty)
{
    size_t n = 0;
    if (!s) return 0;
    while (s[n]) {
        unsigned char c = (unsigned char)s[n];
        if (n + 1 >= cap || c < 32 || c > 126 || c == '|') return 0;
        ++n;
    }
    return allow_empty || n > 0;
}

static int safe_name(const char *name)
{
    size_t n;
    if (!safe_field(name, RDP_PROFILE_NAME_CAP, 0)) return 0;
    n = strlen(name);
    return name[0] != ' ' && name[n - 1] != ' ';
}

static int same_name(const char *a, const char *b)
{
    while (*a && *b) {
        if (tolower((unsigned char)*a) != tolower((unsigned char)*b)) return 0;
        ++a; ++b;
    }
    return *a == 0 && *b == 0;
}

int rdp_profile_validate(const RdpConfig *cfg, char *error, size_t cap)
{
    char source[768];
    RdpConfig parsed;
    int n;
    if (!cfg || !safe_field(cfg->host, sizeof(cfg->host), 0) ||
        !safe_field(cfg->username, sizeof(cfg->username), 1) ||
        !safe_field(cfg->domain, sizeof(cfg->domain), 1))
        return problem(error, cap, "Invalid connection text");
    n = snprintf(source, sizeof(source), "host=%s\nport=%u\n%s%s%s%s",
                 cfg->host, (unsigned)cfg->port,
                 cfg->username[0] ? "username=" : "",
                 cfg->username[0] ? cfg->username : "",
                 cfg->username[0] ? "\n" : "",
                 cfg->domain[0] ? "" : "");
    if (n < 0 || (size_t)n >= sizeof(source))
        return problem(error, cap, "Connection too long");
    if (cfg->domain[0]) {
        size_t used = (size_t)n;
        n = snprintf(source + used, sizeof(source) - used, "domain=%s\n", cfg->domain);
        if (n < 0 || (size_t)n >= sizeof(source) - used)
            return problem(error, cap, "Domain too long");
    }
    if (rdp_config_parse(source, &parsed, error, cap)) return -1;
    if (strcmp(parsed.host, cfg->host) || parsed.port != cfg->port ||
        strcmp(parsed.username, cfg->username) || strcmp(parsed.domain, cfg->domain))
        return problem(error, cap, "Conflicting domain or user");
    if (error && cap) error[0] = 0;
    return 0;
}

int rdp_profiles_upsert(RdpProfileStore *store, const char *name, const RdpConfig *config,
                        char *error, size_t cap)
{
    size_t i;
    if (!store || store->count > RDP_PROFILE_LIMIT || !safe_name(name)) return problem(error, cap, "Invalid profile name");
    if (rdp_profile_validate(config, error, cap)) return -1;
    for (i = 0; i < store->count; ++i)
        if (same_name(store->items[i].name, name)) break;
    if (i == store->count && store->count == RDP_PROFILE_LIMIT)
        return problem(error, cap, "Saved connection limit reached");
    if (i == store->count) ++store->count;
    snprintf(store->items[i].name, sizeof(store->items[i].name), "%s", name);
    store->items[i].config = *config;
    if (error && cap) error[0] = 0;
    return (int)i;
}

void rdp_profiles_delete(RdpProfileStore *store, size_t index)
{
    if (!store || index >= store->count) return;
    if (index + 1 < store->count)
        memmove(&store->items[index], &store->items[index + 1],
                (store->count - index - 1) * sizeof(store->items[0]));
    --store->count;
    memset(&store->items[store->count], 0, sizeof(store->items[0]));
}

static int parse_line(char *line, RdpProfileStore *store, char *error, size_t cap)
{
    char *fields[5], *cursor = line;
    RdpConfig config;
    char cfgtext[768];
    unsigned i;
    int n;
    for (i = 0; i < 5; ++i) {
        char *sep;
        fields[i] = cursor;
        sep = strchr(cursor, '|');
        if (i < 4) {
            if (!sep) return problem(error, cap, "Malformed saved connection");
            *sep = 0;
            cursor = sep + 1;
        } else if (sep) return problem(error, cap, "Too many profile fields");
    }
    n = snprintf(cfgtext, sizeof(cfgtext), "host=%s\nport=%s\n%s%s%s%s",
                 fields[1], fields[2], fields[3][0] ? "username=" : "",
                 fields[3][0] ? fields[3] : "", fields[3][0] ? "\n" : "",
                 fields[4][0] ? "" : "");
    if (n < 0 || (size_t)n >= sizeof(cfgtext))
        return problem(error, cap, "Saved connection too long");
    if (fields[4][0]) {
        size_t used = (size_t)n;
        n = snprintf(cfgtext + used, sizeof(cfgtext) - used, "domain=%s\n", fields[4]);
        if (n < 0 || (size_t)n >= sizeof(cfgtext) - used)
            return problem(error, cap, "Saved domain too long");
    }
    if (rdp_config_parse(cfgtext, &config, error, cap)) return -1;
    if (rdp_profiles_upsert(store, fields[0], &config, error, cap) < 0) return -1;
    return 0;
}

static int read_file(const char *path, RdpProfileStore *out, char *error, size_t cap)
{
    char line[PROFILE_LINE_CAP];
    RdpProfileStore parsed = {0};
    FILE *file = fopen(path, "rb");
    if (!file) return errno == ENOENT ? 1 : problem(error, cap, "Cannot open profiles");
    if (!fgets(line, sizeof(line), file) ||
        strcmp(line, PROFILE_SIGNATURE "\n") != 0) {
        fclose(file);
        return problem(error, cap, "Unknown profile file format");
    }
    while (fgets(line, sizeof(line), file)) {
        size_t n = strlen(line);
        if (n && line[n - 1] == '\n') line[--n] = 0;
        else if (!feof(file)) {
            fclose(file);
            return problem(error, cap, "Profile line too long");
        }
        if (n && line[n - 1] == '\r') line[--n] = 0;
        if (!n) continue;
        if (parsed.count >= RDP_PROFILE_LIMIT ||
            parse_line(line, &parsed, error, cap)) {
            fclose(file);
            return problem(error, cap, "Corrupt or excessive profiles");
        }
    }
    if (ferror(file)) {
        fclose(file);
        return problem(error, cap, "Profile read failed");
    }
    fclose(file);
    *out = parsed;
    return 0;
}

int rdp_profiles_load(const char *path, RdpProfileStore *out, char *error, size_t cap)
{
    char backup[512];
    int rc;
    if (!path || !out || strlen(path) + 5 >= sizeof(backup))
        return problem(error, cap, "Invalid profile path");
    memset(out, 0, sizeof(*out));
    rc = read_file(path, out, error, cap);
    if (rc != 0) {
        int backup_rc;
        snprintf(backup, sizeof(backup), "%s.bak", path);
        backup_rc = read_file(backup, out, error, cap);
        if (backup_rc == 0) rc = 0;
        else if (rc == 1 && backup_rc == 1) rc = 0; /* First run. */
        else if (rc == 1) rc = backup_rc;
        else problem(error, cap, "Saved profiles damaged (backup unavailable)");
    }
    if (!rc && error && cap) error[0] = 0;
    return rc;
}

int rdp_profiles_save(const char *path, const RdpProfileStore *store, char *error, size_t cap)
{
    char temp[512], backup[512];
    FILE *file;
    size_t i;
    int had_original;
    if (!path || !store || store->count > RDP_PROFILE_LIMIT ||
        strlen(path) + 5 >= sizeof(temp))
        return problem(error, cap, "Invalid profile store");
    for (i = 0; i < store->count; ++i) {
        size_t j;
        if (!safe_name(store->items[i].name) ||
            rdp_profile_validate(&store->items[i].config, error, cap)) return -1;
        for (j = 0; j < i; ++j)
            if (same_name(store->items[i].name, store->items[j].name))
                return problem(error, cap, "Duplicate saved connection");
    }
    snprintf(temp, sizeof(temp), "%s.tmp", path);
    snprintf(backup, sizeof(backup), "%s.bak", path);
    file = fopen(temp, "wb");
    if (!file) return problem(error, cap, "Cannot write SD profile file");
    if (fprintf(file, PROFILE_SIGNATURE "\n") < 0) goto write_error;
    for (i = 0; i < store->count; ++i) {
        const RdpProfile *p = &store->items[i];
        if (fprintf(file, "%s|%s|%u|%s|%s\n",
                    p->name, p->config.host, (unsigned)p->config.port,
                    p->config.username, p->config.domain) < 0) goto write_error;
    }
    {
        int flushed = fflush(file);
        int closed = fclose(file);
        if (flushed || closed) {
            remove(temp);
            return problem(error, cap, "SD write failed");
        }
    }
    had_original = 0;
    {
        FILE *existing = fopen(path, "rb");
        if (existing) {
            had_original = 1;
            fclose(existing);
        }
    }
    if (had_original) {
        remove(backup);
        if (rename(path, backup)) {
            remove(temp);
            return problem(error, cap, "Could not back up saved profiles");
        }
    }
    if (rename(temp, path)) {
        if (had_original) rename(backup, path);
        remove(temp);
        return problem(error, cap, "Could not replace saved profiles");
    }
    if (had_original) remove(backup);
    if (error && cap) error[0] = 0;
    return 0;
write_error:
    fclose(file);
    remove(temp);
    return problem(error, cap, "SD write failed");
}
