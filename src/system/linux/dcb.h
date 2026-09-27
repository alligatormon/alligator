#pragma once

#ifdef __linux__

#include <stddef.h>

void get_dcb_pfc_stats(void);

/* Parse dcbmsg + attributes from an RTM_GETDCB IEEE reply and emit PFC counters.
 * payload starts at struct dcbmsg. Returns 1 if emitted. */
int dcb_parse_ieee_emit(const char *device, const void *payload, size_t len);

#endif
