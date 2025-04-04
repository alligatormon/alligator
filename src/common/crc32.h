#pragma once
unsigned int xcrc32 (const unsigned char *buf, int len, unsigned int init);
uint64_t crc32(const char *buf, uint32_t len, uint32_t init);
uint64_t crc32_get(const char *buf);
