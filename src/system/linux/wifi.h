#pragma once

#ifdef __linux__

void get_wifi_stats(void);
void wifi_parse_dump(const char *buf, size_t size);

#endif
