#include "rdp_environment.h"
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#define ENV_PATH_MAX 384

static int fail(char *error, size_t cap, const char *message)
{
    if (error && cap) snprintf(error, cap, "%s", message);
    return -1;
}

static int mkdir_existing(const char *path)
{
    struct stat info;
    if (mkdir(path, 0700) == 0) return 0;
    if (errno != EEXIST) return -1;
    if (stat(path, &info) != 0 || !S_ISDIR(info.st_mode)) return -1;
    return 0;
}

/* Works both for POSIX absolute paths and for newlib's sdmc:/... prefix.
 * Do not attempt to mkdir("sdmc:") or mkdir("/") on the way to the root. */
static int make_parents(const char *path)
{
    char temp[ENV_PATH_MAX];
    size_t i, len;
    if (!path) return -1;
    len = strlen(path);
    if (!len || len >= sizeof(temp)) return -1;
    memcpy(temp, path, len + 1);
    for (i = 1; i < len; ++i) {
        if (temp[i] != '/' || temp[i - 1] == ':') continue;
        temp[i] = 0;
        if (mkdir_existing(temp) != 0) return -1;
        temp[i] = '/';
    }
    return mkdir_existing(temp);
}

int rdp_environment_init(const char *home, char *error, size_t cap)
{
    static const struct {
        const char *var;
        const char *subdir;
    } dirs[] = {
        {"XDG_CONFIG_HOME", "/.config"},
        {"XDG_CACHE_HOME", "/.cache"},
        {"XDG_DATA_HOME", "/.local/share"},
        {"TMPDIR", "/tmp"},
        {"XDG_RUNTIME_DIR", "/tmp"}
    };
    char path[ENV_PATH_MAX];
    size_t i;
    int n;
    if (!home || !home[0] || strlen(home) > 230)
        return fail(error, cap, "Invalid WinPR SD home directory");
    if (make_parents(home) != 0)
        return fail(error, cap, "Cannot create WinPR SD home directory");
    /* WinPR's GetPath_HOME() calls getenv("HOME"), and its config/cache
     * paths use the XDG variables. Install these before freerdp_new() or
     * freerdp_context_new(); later changing settings->HomePath is too late. */
    if (setenv("HOME", home, 1) != 0)
        return fail(error, cap, "Cannot set WinPR HOME environment");
    for (i = 0; i < sizeof(dirs) / sizeof(dirs[0]); ++i) {
        n = snprintf(path, sizeof(path), "%s%s", home, dirs[i].subdir);
        if (n <= 0 || (size_t)n >= sizeof(path) ||
            make_parents(path) != 0)
            return fail(error, cap, "Cannot create WinPR config/cache directory");
        if (setenv(dirs[i].var, path, 1) != 0)
            return fail(error, cap, "Cannot set WinPR XDG environment");
    }
    if (error && cap) error[0] = 0;
    return 0;
}
