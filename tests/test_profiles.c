#include "profile_store.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

int main(void)
{
    const char *file = "test-profiles.tmp";
    RdpProfileStore store = {0}, loaded;
    RdpConfig cfg = {0};
    char error[128];
    int index;
    /* Fixed fields must reject unterminated values without reading beyond
     * RdpConfig's structure (important for damaged SD configuration). */
    {
        RdpConfig malformed = {0};
        memset(malformed.host, 'x', sizeof(malformed.host));
        malformed.port = 3389;
        assert(rdp_profile_validate(&malformed, error, sizeof(error)) < 0);
        memset(malformed.username, 'u', sizeof(malformed.username));
        malformed.host[0] = 'h'; malformed.host[1] = 0;
        assert(rdp_profile_validate(&malformed, error, sizeof(error)) < 0);
    }
    cfg.port = 3389;
    strcpy(cfg.host, "workstation.example.com");
    strcpy(cfg.username, "alice");
    strcpy(cfg.domain, "CORP");
    index = rdp_profiles_upsert(&store, "Office", &cfg, error, sizeof(error));
    assert(index == 0);
    assert(rdp_profiles_upsert(&store, "office", &cfg, error, sizeof(error)) == 0);
    assert(store.count == 1);
    assert(rdp_profiles_upsert(&store, "bad|name", &cfg, error, sizeof(error)) < 0);
    assert(rdp_profiles_save(file, &store, error, sizeof(error)) == 0);
    assert(rdp_profiles_load(file, &loaded, error, sizeof(error)) == 0);
    assert(loaded.count == 1 && strcmp(loaded.items[0].name, "office") == 0);
    assert(strcmp(loaded.items[0].config.host, cfg.host) == 0);
    assert(strcmp(loaded.items[0].config.domain, "CORP") == 0);
    assert(rename(file, "test-profiles.tmp.bak") == 0);
    {
        FILE *broken = fopen(file, "wb");
        assert(broken);
        assert(fputs("broken profile file\n", broken) >= 0);
        assert(fclose(broken) == 0);
    }
    assert(rdp_profiles_load(file, &loaded, error, sizeof(error)) == 0);
    assert(loaded.count == 1); /* corrupt primary recovered from backup */
    assert(remove(file) == 0);
    assert(rename("test-profiles.tmp.bak", file) == 0);
    strcpy(cfg.username, "alice@corp.example.com");
    assert(rdp_profile_validate(&cfg, error, sizeof(error)) < 0);
    cfg.domain[0] = 0;
    assert(rdp_profile_validate(&cfg, error, sizeof(error)) == 0);
    rdp_profiles_delete(&loaded, 0);
    assert(loaded.count == 0);
    assert(rdp_profiles_save(file, &loaded, error, sizeof(error)) == 0);
    assert(rdp_profiles_load(file, &loaded, error, sizeof(error)) == 0);
    assert(loaded.count == 0);
    remove(file);
    remove("test-profiles.tmp.bak");
    puts("Saved profile tests passed");
    return 0;
}
