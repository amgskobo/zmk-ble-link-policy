/* SPDX-License-Identifier: GPL-3.0-only */
/* Named admission controls. Compile only through ZBLP_LINK_SOURCE with the
 * selected sanitizer and its recovering runtime; normal fixtures never use it.
 * The real policy and every existing kernel scenario remain unchanged. */
#include "../../src/link_service.c"
#include <limits.h>

#if defined(ZBLP_ASAN_ADMISSION_CONTROL)
static volatile unsigned char admission_buffer[8];
static volatile unsigned admission_index = 8;
#elif defined(ZBLP_UBSAN_ADMISSION_CONTROL)
static volatile int admission_maximum = INT_MAX;
static volatile int admission_one = 1;
static volatile int admission_result;
#else
#error "Select the named ASan or UBSan admission control"
#endif

static int sanitizer_admission_fault(void)
{
#if defined(ZBLP_ASAN_ADMISSION_CONTROL)
    admission_buffer[admission_index] = 1;
#else
    admission_result = admission_maximum + admission_one;
#endif
    return 0;
}

SYS_INIT(sanitizer_admission_fault, APPLICATION, 99);
