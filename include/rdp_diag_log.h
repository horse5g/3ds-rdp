#ifndef RDP_DIAG_LOG_H
#define RDP_DIAG_LOG_H

/* Opt-in diagnostic log. Never creates rdplog.txt automatically.
 * Only literal, non-sensitive event names and numeric values are allowed.
 * The caller must not submit credentials or certificate/entropy contents. */
void rdp_diag_log_init(void);
int rdp_diag_log_enabled(void);
void rdp_diag_log_event(const char *event, unsigned long a,
                        unsigned long b, unsigned long c);
#endif
