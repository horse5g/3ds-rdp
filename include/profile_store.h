#ifndef RDP_PROFILE_STORE_H
#define RDP_PROFILE_STORE_H

#include "config.h"
#include <stddef.h>

#define RDP_PROFILE_LIMIT 12
#define RDP_PROFILE_NAME_CAP 41

typedef struct {
    uint8_t display_mode;     /* 0=400x240, 1=640x360, 2=1280x720, 3=1920x1080, 4=800x480, 5=960x540 */
    uint8_t color_depth;      /* 16 or 32 */
    uint8_t touch_speed;      /* 1..3 */
    uint8_t scroll_step;      /* 1..3 */
    uint8_t reverse_scroll;   /* 0/1 */
    uint8_t low_bandwidth;    /* 0/1: avoid wallpaper */
    uint8_t timeout_seconds;  /* 8, 12 or 20 */
} RdpUiOptions;
RdpUiOptions rdp_options_default(void);
int rdp_options_valid(const RdpUiOptions *options);

typedef struct {
    char name[RDP_PROFILE_NAME_CAP];
    RdpConfig config;
    RdpUiOptions options;
} RdpProfile;

typedef struct {
    RdpProfile items[RDP_PROFILE_LIMIT];
    size_t count;
} RdpProfileStore;

/* Portable, password-free SD profile store. Missing file means zero profiles.
 * A .bak file is used for recovery if a previous write was interrupted. */
int rdp_profiles_load(const char *path, RdpProfileStore *out, char *error, size_t cap);
int rdp_profiles_save(const char *path, const RdpProfileStore *store, char *error, size_t cap);
int rdp_profiles_upsert(RdpProfileStore *store, const char *name, const RdpConfig *config,
                        char *error, size_t cap);
void rdp_profiles_delete(RdpProfileStore *store, size_t index);
int rdp_profile_validate(const RdpConfig *config, char *error, size_t cap);

#endif
