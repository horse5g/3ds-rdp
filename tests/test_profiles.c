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
    /* Stale transaction sidecars must never be overwritten or deleted. */
    {
        FILE *side = fopen("test-profiles.tmp.bak", "wb");
        assert(side);
        assert(fputs("Important previous backup\n", side) >= 0);
        assert(fclose(side) == 0);
        assert(rdp_profiles_save(file, &store, error, sizeof(error)) < 0);
        assert(rdp_profiles_load(file, &loaded, error, sizeof(error)) == 0);
        assert(remove("test-profiles.tmp.bak") == 0);
        side = fopen("test-profiles.tmp.tmp", "wb");
        assert(side);
        assert(fputs("Interrupted old save\n", side) >= 0);
        assert(fclose(side) == 0);
        assert(rdp_profiles_save(file, &store, error, sizeof(error)) < 0);
        assert(remove("test-profiles.tmp.tmp") == 0);
    }
    /* Refuse to destroy a malformed primary even without a backup. */
    {
        const char *guard = "test-profiles-corrupt.tmp";
        FILE *bad = fopen(guard, "wb");
        assert(bad);
        assert(fputs("INVALID FILE\n", bad) >= 0);
        assert(fclose(bad) == 0);
        assert(rdp_profiles_save(guard, &store, error, sizeof(error)) < 0);
        bad = fopen(guard, "rb");
        assert(bad);
        char line[40] = {0};
        assert(fgets(line, sizeof(line), bad));
        assert(strcmp(line, "INVALID FILE\n") == 0);
        assert(fclose(bad) == 0);
        assert(remove(guard) == 0);
    }
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
    /* A corrupt file with two records of the same name must not silently
       replace the first record with the second (case insensitive). */
    {
        const char *duplicate = "test-profiles-duplicates.tmp";
        FILE *bad = fopen(duplicate, "wb");
        assert(bad);
        assert(fputs("3DSRDP2\n", bad) >= 0);
        assert(fputs("Office|first.example.com|3389|alice|CORP|0|32|2|1|0|0|12\n", bad) >= 0);
        assert(fputs("office|second.example.com|3389|alice|CORP|0|32|2|1|0|0|12\n", bad) >= 0);
        assert(fclose(bad) == 0);
        assert(rdp_profiles_load(duplicate, &loaded, error, sizeof(error)) != 0);
        assert(error[0] != 0); /* no backup: loader reports damaged profiles */
        assert(loaded.count == 0);
        assert(remove(duplicate) == 0);
    }
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
