#include "profile_store.h"

#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#define PROFILE_LINE_CAP 1024
#define PROFILE_SIGNATURE "3DSRDP1"

static int problem(char *error, size_t cap, const char *reason)
{
    if (error && cap) snprintf(error, cap, "%s", reason);
    return -1;
}

static int safe_field(const char *s, size_t cap, int allow_empty)
{
    if (!s || !cap) return 0;
    /* Read only inside the supplied fixed-size field. A corrupted or
     * unterminated RdpConfig must not cause an out-of-bounds read. */
    for (size_t n = 0; n < cap; ++n) {
        const unsigned char c = (unsigned char)s[n];
        if (!c) return allow_empty || n > 0;
        if (c < 32 || c > 126 || c == '|') return 0;
    }
    return 0;
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


/* 1=present, 0=absent, -1=I/O or permission failure. Never confuse
 * inaccessible storage with an empty profile file. */
static int profile_file_exists(const char *path)
{
    struct stat st;
    if (stat(path, &st) == 0) return 1;
    return errno == ENOENT ? 0 : -1;
}

static int profiles_equal(const RdpProfileStore *left, const RdpProfileStore *right)
{
    if (left->count != right->count) return 0;
    for (size_t i = 0; i < left->count; ++i) {
        const RdpProfile *a = &left->items[i];
        const RdpProfile *b = &right->items[i];
        if (strcmp(a->name, b->name) ||
            strcmp(a->config.host, b->config.host) ||
            strcmp(a->config.username, b->config.username) ||
            strcmp(a->config.domain, b->config.domain) ||
            a->config.port != b->config.port)
            return 0;
    }
    return 1;
}

int rdp_profiles_save(const char *path, const RdpProfileStore *store, char *error, size_t cap)
{
    char temp[512], backup[512];
    RdpProfileStore *verify = NULL;
    FILE *file = NULL;
    size_t i;
    int had_original, backup_moved = 0, promoted = 0, result = -1;

    if (!path || !store || store->count > RDP_PROFILE_LIMIT ||
        strlen(path) + 5 >= sizeof(temp))
        return problem(error, cap, "Invalid profile store");

    for (i = 0; i < store->count; ++i) {
        if (!safe_name(store->items[i].name) ||
            rdp_profile_validate(&store->items[i].config, error, cap))
            return -1;
        for (size_t j = 0; j < i; ++j)
            if (same_name(store->items[i].name, store->items[j].name))
                return problem(error, cap, "Duplicate saved connection");
    }

    snprintf(temp, sizeof(temp), "%s.tmp", path);
    snprintf(backup, sizeof(backup), "%s.bak", path);

    /* An interrupted prior transaction must be investigated, not destroyed.
     * In particular, NEVER delete a pre-existing valid .bak as part of save. */
    if (profile_file_exists(temp) != 0 || profile_file_exists(backup) != 0)
        return problem(error, cap, "Recover stale .tmp/.bak before saving");

    had_original = profile_file_exists(path);
    if (had_original < 0)
        return problem(error, cap, "Cannot access saved profiles");

    verify = (RdpProfileStore *)malloc(sizeof(*verify));
    if (!verify)
        return problem(error, cap, "Out of memory verifying profiles");

    /* Refuse to overwrite a corrupt primary, even if a caller displays a
     * recovered backup. The original data must be repaired first. */
    if (had_original && read_file(path, verify, error, cap) != 0) {
        problem(error, cap, "Repair damaged profiles before saving");
        goto finish;
    }

    file = fopen(temp, "wb");
    if (!file) {
        problem(error, cap, "Cannot write SD profile file");
        goto finish;
    }
    if (fprintf(file, PROFILE_SIGNATURE "\n") < 0)
        goto write_error;
    for (i = 0; i < store->count; ++i) {
        const RdpProfile *p = &store->items[i];
        if (fprintf(file, "%s|%s|%u|%s|%s\n",
                    p->name, p->config.host, (unsigned)p->config.port,
                    p->config.username, p->config.domain) < 0)
            goto write_error;
    }
    if (fflush(file) != 0)
        goto write_error;
    if (fclose(file) != 0) {
        file = NULL;
        problem(error, cap, "SD write failed");
        goto finish;
    }
    file = NULL;
    if (read_file(temp, verify, error, cap) != 0 || !profiles_equal(verify, store)) {
        problem(error, cap, "Profile write verification failed");
        goto finish;
    }

    if (had_original) {
        if (rename(path, backup) != 0) {
            problem(error, cap, "Could not back up saved profiles");
            goto finish;
        }
        backup_moved = 1;
    }
    if (rename(temp, path) != 0) {
        problem(error, cap, "Could not replace saved profiles");
        goto finish;
    }
    promoted = 1;
    if (read_file(path, verify, error, cap) != 0 || !profiles_equal(verify, store)) {
        problem(error, cap, "Profile readback verification failed");
        goto finish;
    }
    if (backup_moved) {
        if (remove(backup) != 0) {
            problem(error, cap, "New profiles saved; backup cleanup failed");
            /* Leave both copies, never delete data just to report success. */
            backup_moved = 0;
            goto finish;
        }
        backup_moved = 0;
    }
    if (error && cap) error[0] = 0;
    result = 0;
    goto finish;

write_error:
    problem(error, cap, "SD write failed");
finish:
    if (file && fclose(file) != 0)
        problem(error, cap, "SD write close failed");
    if (result != 0 && backup_moved) {
        /* Best-effort rollback. Keep .bak if rollback cannot complete. */
        if (!promoted || remove(path) == 0) {
            if (rename(backup, path) == 0)
                backup_moved = 0;
        }
    }
    /* Never remove the pre-existing backup when rollback is uncertain. */
    if (result != 0 && profile_file_exists(temp) == 1)
        remove(temp);
    free(verify);
    return result;
}
