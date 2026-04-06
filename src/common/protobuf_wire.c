#include "common/protobuf_wire.h"

int pbwire_read_varint(const uint8_t **p, const uint8_t *end, uint64_t *out)
{
	uint64_t v = 0;
	unsigned shift = 0;
	while (*p < end && shift < 64)
	{
		uint8_t b = *(*p)++;
		v |= (uint64_t)(b & 0x7f) << shift;
		if (!(b & 0x80))
		{
			*out = v;
			return 1;
		}
		shift += 7;
	}
	return 0;
}

int pbwire_read_fixed64(const uint8_t **p, const uint8_t *end, uint64_t *out)
{
	uint64_t v = 0;
	if ((size_t)(end - *p) < 8)
		return 0;
	for (int i = 0; i < 8; i++)
		v |= ((uint64_t)(*p)[i]) << (8 * i);
	*p += 8;
	*out = v;
	return 1;
}

int pbwire_read_len(const uint8_t **p, const uint8_t *end, const uint8_t **start, size_t *len)
{
	uint64_t l;
	if (!pbwire_read_varint(p, end, &l))
		return 0;
	if (l > (uint64_t)(end - *p))
		return 0;
	*start = *p;
	*len = (size_t)l;
	*p += l;
	return 1;
}

int pbwire_skip_field(const uint8_t **p, const uint8_t *end, uint8_t wire)
{
	uint64_t t;
	const uint8_t *s;
	size_t l;

	switch (wire)
	{
	case 0:
		return pbwire_read_varint(p, end, &t);
	case 1:
		return pbwire_read_fixed64(p, end, &t);
	case 2:
		return pbwire_read_len(p, end, &s, &l);
	case 5:
		if ((size_t)(end - *p) < 4)
			return 0;
		*p += 4;
		return 1;
	default:
		return 0;
	}
}

void pbwire_write_varint(string *dst, uint64_t v)
{
	uint8_t b[10];
	size_t n = 0;
	do {
		b[n] = (uint8_t)(v & 0x7f);
		v >>= 7;
		if (v)
			b[n] |= 0x80;
		n++;
	} while (v && n < sizeof(b));
	string_cat(dst, (char *)b, n);
}

void pbwire_write_tag(string *dst, uint32_t field, uint8_t wire)
{
	pbwire_write_varint(dst, ((uint64_t)field << 3) | (uint64_t)(wire & 0x7));
}

void pbwire_write_len(string *dst, char *data, size_t len)
{
	pbwire_write_varint(dst, len);
	if (len)
		string_cat(dst, data, len);
}

void pbwire_write_field_lenmsg(string *dst, uint32_t field, string *msg)
{
	pbwire_write_tag(dst, field, 2);
	pbwire_write_len(dst, msg->s, msg->l);
}

void pbwire_write_field_fixed64(string *dst, uint32_t field, uint64_t raw)
{
	uint8_t b[8];
	for (int i = 0; i < 8; i++)
		b[i] = (uint8_t)((raw >> (8 * i)) & 0xff);
	pbwire_write_tag(dst, field, 1);
	string_cat(dst, (char *)b, 8);
}
