#pragma once

#include <stdint.h>
#include "common/selector.h"

int pbwire_read_varint(const uint8_t **p, const uint8_t *end, uint64_t *out);
int pbwire_read_fixed64(const uint8_t **p, const uint8_t *end, uint64_t *out);
int pbwire_read_len(const uint8_t **p, const uint8_t *end, const uint8_t **start, size_t *len);
int pbwire_skip_field(const uint8_t **p, const uint8_t *end, uint8_t wire);

void pbwire_write_varint(string *dst, uint64_t v);
void pbwire_write_tag(string *dst, uint32_t field, uint8_t wire);
void pbwire_write_len(string *dst, char *data, size_t len);
void pbwire_write_field_lenmsg(string *dst, uint32_t field, string *msg);
void pbwire_write_field_fixed64(string *dst, uint32_t field, uint64_t raw);
