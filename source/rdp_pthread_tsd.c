/*
 * MIT-licensed 3DS C compatibility layer for pthread thread-specific data.
 *
 * Design inspired by MIT OR Apache-2.0 rust3ds/pthread-3ds (thread_keys.rs):
 * https://github.com/rust3ds/pthread-3ds
 *
 * This is intentionally NOT a replacement for pthread mutex/once/rwlock;
 * it supplies linker-wrapped versions of the four broken key functions.
 * It relies on libctru's working GCC __thread storage for per-thread values.
 * libctru threadCreate users MUST invoke the cleanup hook on thread exit.
 * devkitARM libsysbase.a already defines pthread_key_create (ENOSYS),
 * so strong duplicate names would fail to link. Link with --wrap for all four.\n * No synthetic entropy, process-global "TLS" values or no-threads workaround.
 */
#ifdef __3DS__
#include "rdp_pthread_tsd.h"
#include <3ds/synchronization.h>
#include <pthread.h>
#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

#define RDP_TSD_SLOTS 64u
#define RDP_TSD_KEY_BITS 8u
#define RDP_TSD_KEY_INDEX_MASK 0xFFu
#define RDP_TSD_MAX_GENERATION 0x00FFFFFFu
#define RDP_TSD_DTOR_PASSES 4u

typedef void (*RdpTsdDestructor)(void *);
typedef struct {
    bool active;
    uint32_t generation;
    RdpTsdDestructor destructor;
} RdpTsdKey;

typedef struct {
    uint32_t generation[RDP_TSD_SLOTS];
    void *value[RDP_TSD_SLOTS];
} RdpThreadValues;

/* libctru documents a zero-initialized LightLock as unlocked. */
static LightLock rdp_tsd_lock;
static RdpTsdKey rdp_tsd_keys[RDP_TSD_SLOTS];
static __thread RdpThreadValues rdp_thread_values;

static bool decode_key(pthread_key_t key, unsigned *index, uint32_t *generation)
{
    const uint32_t raw = (uint32_t)key;
    const unsigned offset = raw & RDP_TSD_KEY_INDEX_MASK;
    const uint32_t gen = raw >> RDP_TSD_KEY_BITS;
    if (offset == 0u || offset > RDP_TSD_SLOTS || gen == 0u)
        return false;
    *index = offset - 1u;
    *generation = gen;
    return true;
}

static bool key_valid(unsigned index, uint32_t generation)
{
    return rdp_tsd_keys[index].active &&
           rdp_tsd_keys[index].generation == generation;
}

int __wrap_pthread_key_create(pthread_key_t *key, void (*destructor)(void *))
{
    unsigned i;
    if (!key)
        return EINVAL;
    LightLock_Lock(&rdp_tsd_lock);
    for (i = 0u; i < RDP_TSD_SLOTS; ++i) {
        RdpTsdKey *slot = &rdp_tsd_keys[i];
        if (!slot->active) {
            if (slot->generation == 0u)
                slot->generation = 1u;
            slot->destructor = destructor;
            slot->active = true;
            *key = (pthread_key_t)((slot->generation << RDP_TSD_KEY_BITS) | (i + 1u));
            LightLock_Unlock(&rdp_tsd_lock);
            return 0;
        }
    }
    LightLock_Unlock(&rdp_tsd_lock);
    return EAGAIN;
}

int __wrap_pthread_key_delete(pthread_key_t key)
{
    unsigned i;
    uint32_t gen;
    if (!decode_key(key, &i, &gen))
        return EINVAL;
    LightLock_Lock(&rdp_tsd_lock);
    if (!key_valid(i, gen)) {
        LightLock_Unlock(&rdp_tsd_lock);
        return EINVAL;
    }
    rdp_tsd_keys[i].active = false;
    rdp_tsd_keys[i].destructor = NULL;
    /* Invalidate stale per-thread values when a slot is reused. */
    rdp_tsd_keys[i].generation =
        gen == RDP_TSD_MAX_GENERATION ? 1u : gen + 1u;
    LightLock_Unlock(&rdp_tsd_lock);
    return 0;
}

void *__wrap_pthread_getspecific(pthread_key_t key)
{
    unsigned i;
    uint32_t gen;
    bool valid;
    if (!decode_key(key, &i, &gen))
        return NULL;
    LightLock_Lock(&rdp_tsd_lock);
    valid = key_valid(i, gen);
    LightLock_Unlock(&rdp_tsd_lock);
    return valid && rdp_thread_values.generation[i] == gen
        ? rdp_thread_values.value[i] : NULL;
}

int __wrap_pthread_setspecific(pthread_key_t key, const void *value)
{
    unsigned i;
    uint32_t gen;
    if (!decode_key(key, &i, &gen))
        return EINVAL;
    LightLock_Lock(&rdp_tsd_lock);
    if (!key_valid(i, gen)) {
        LightLock_Unlock(&rdp_tsd_lock);
        return EINVAL;
    }
    rdp_thread_values.generation[i] = gen;
    rdp_thread_values.value[i] = (void *)value;
    LightLock_Unlock(&rdp_tsd_lock);
    return 0;
}

void rdp_3ds_tsd_cleanup_current_thread(void)
{
    unsigned pass, i;
    /* POSIX permits destructors to set values again. Iterate up to four
     * times; never hold the key registry lock while invoking a callback. */
    for (pass = 0; pass < RDP_TSD_DTOR_PASSES; ++pass) {
        bool found = false;
        for (i = 0; i < RDP_TSD_SLOTS; ++i) {
            RdpTsdDestructor destructor = NULL;
            const uint32_t gen = rdp_thread_values.generation[i];
            void *value = rdp_thread_values.value[i];
            if (!value)
                continue;
            found = true;
            rdp_thread_values.value[i] = NULL;
            rdp_thread_values.generation[i] = 0u;
            LightLock_Lock(&rdp_tsd_lock);
            if (key_valid(i, gen))
                destructor = rdp_tsd_keys[i].destructor;
            LightLock_Unlock(&rdp_tsd_lock);
            if (destructor)
                destructor(value);
        }
        if (!found)
            break;
    }
}
#endif
