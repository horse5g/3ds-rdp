/*
 * MIT-compatible 3DS pthread thread-specific-data bridge.
 * The devkitARM/newlib pthread_key_* stubs return ENOSYS on some builds.
 * See docs/openssl-ps-entropy-runtime.md for threading/cleanup limits.
 */
#ifndef RDP_PTHREAD_TSD_H
#define RDP_PTHREAD_TSD_H
#ifdef __cplusplus
extern "C" {
#endif
/* Call at the end of each libctru thread that used pthread TSD.
   libctru's threadCreate is not a pthread_create wrapper. */
void rdp_3ds_tsd_cleanup_current_thread(void);
#ifdef __cplusplus
}
#endif
#endif
