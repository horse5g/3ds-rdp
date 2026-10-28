#ifndef RDP_SESSION_WAIT_H
#define RDP_SESSION_WAIT_H

#include <stdint.h>

/* WinPR / Win32 WaitForMultipleObjects returns one of the signaled index
 * values or WAIT_TIMEOUT (0x102). WAIT_FAILED, WAIT_ABANDONED_*, or an
 * out-of-range handle index are NOT successful RDP event wakeups.
 * Independent of WinPR so the bounds logic can be regression tested on host. */
static inline int rdp_wait_result_valid(uint32_t status, uint32_t count)
{
    return count != 0 &&
           (status == UINT32_C(0x102) || status < count);
}

#endif
