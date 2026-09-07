#pragma once

#ifdef __linux__

void get_wireguard_stats(void);
void wireguard_parse_dump(const char *buf, size_t size);

#endif
