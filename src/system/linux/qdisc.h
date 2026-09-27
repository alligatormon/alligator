#pragma once

#ifdef __linux__

#include <stddef.h>
#include <stdint.h>

void get_qdisc_stats(void);

/* Parse TCA_* attributes for one qdisc and emit metrics. Root only; skips kind noqueue.
 * device is already resolved (caller may use if_indextoname). Returns 1 if emitted. */
int qdisc_parse_attrs_emit(const char *device, uint32_t parent, const void *attrs, size_t attrlen);

#endif
