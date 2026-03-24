#pragma once
/* Queues IPMI scrape on libuv thread pool; does not block uv_run. If a scrape is
 * still in progress, the new request is skipped until it finishes. */
void ipmi_schedule_get_status(void);
