#include "session_wait.h"
#include <assert.h>
#include <stdio.h>
#include <stdint.h>

int main(void)
{
    assert(!rdp_wait_result_valid(0, 0));
    assert(rdp_wait_result_valid(0, 1)); /* first signaled handle */
    assert(!rdp_wait_result_valid(1, 1));
    assert(rdp_wait_result_valid(63, 64));
    assert(!rdp_wait_result_valid(64, 64));
    assert(rdp_wait_result_valid(UINT32_C(0x102), 64)); /* normal timeout */
    assert(!rdp_wait_result_valid(UINT32_C(0x80), 64)); /* abandoned */
    assert(!rdp_wait_result_valid(UINT32_C(0xffffffff), 64)); /* failed */
    assert(!rdp_wait_result_valid(UINT32_C(0xfffffffe), 64));
    puts("WinPR event-wait status tests passed");
    return 0;
}
