#ifndef RDP_PROFILE_STORE_H
#define RDP_PROFILE_STORE_H

#include "config.h"
#include <stddef.h>

#define RDP_PROFILE_LIMIT 12
#define RDP_PROFILE_NAME_CAP 41

typedef struct {
    char name[RDP_PROFILE_NAME_CAP];
    RdpConfig config;
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
