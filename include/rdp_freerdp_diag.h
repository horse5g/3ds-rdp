#ifndef RDP_FREERDP_DIAG_H
#define RDP_FREERDP_DIAG_H
/* O3DS-only symbols patched into pinned FreeRDP 3.32.2. No secrets logged. */
#ifdef __cplusplus
extern "C" {
#endif
unsigned int freerdp_3ds_context_init_stage(void);
unsigned int freerdp_3ds_rdp_init_stage(void);
unsigned int freerdp_3ds_timer_init_stage(void);
unsigned int freerdp_3ds_channels_init_stage(void);
/* Diagnostic only, meaningful after the timer worker fails at T5. */
void freerdp_3ds_timer_thread_diag(unsigned int *priority,
                                  unsigned int *threads_used,
                                  unsigned int *threads_max,
                                  unsigned int *stack_alloc_ok);
#ifdef __cplusplus
}
#endif
#endif
