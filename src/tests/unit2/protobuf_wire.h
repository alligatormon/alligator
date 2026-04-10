#include <string.h>
#include "common/protobuf_wire.h"

void test_protobuf_wire() {
	string *s = string_init(64);
	const uint8_t *p;
	const uint8_t *start;
	const uint8_t *p2;
	size_t len;
	uint64_t v = 0;
	uint64_t raw = 0;

	pbwire_write_varint(s, 300);
	assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 2, s->l);
	assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 0xac, (uint8_t)s->s[0]);
	assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 0x02, (uint8_t)s->s[1]);

	p = (const uint8_t *)s->s;
	assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 1, pbwire_read_varint(&p, (const uint8_t *)s->s + s->l, &v));
	assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 300, v);
	assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 0, (const uint8_t *)s->s + s->l - p);
	string_free(s);

	s = string_init(64);
	pbwire_write_tag(s, 4, 1);
	pbwire_write_field_fixed64(s, 6, 0x1122334455667788ULL);
	p = (const uint8_t *)s->s;
	assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 1, pbwire_read_varint(&p, (const uint8_t *)s->s + s->l, &v));
	assert_equal_int(__FILE__, __FUNCTION__, __LINE__, ((uint64_t)4 << 3) | 1, v);
	assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 1, pbwire_read_varint(&p, (const uint8_t *)s->s + s->l, &v));
	assert_equal_int(__FILE__, __FUNCTION__, __LINE__, ((uint64_t)6 << 3) | 1, v);
	assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 1, pbwire_read_fixed64(&p, (const uint8_t *)s->s + s->l, &raw));
	assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 0x1122334455667788ULL, raw);
	string_free(s);

	s = string_init(128);
	string *inner = string_init(32);
	pbwire_write_tag(inner, 1, 0);
	pbwire_write_varint(inner, 42);
	pbwire_write_field_lenmsg(s, 2, inner);

	p = (const uint8_t *)s->s;
	assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 1, pbwire_read_varint(&p, (const uint8_t *)s->s + s->l, &v));
	assert_equal_int(__FILE__, __FUNCTION__, __LINE__, ((uint64_t)2 << 3) | 2, v);
	assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 1, pbwire_read_len(&p, (const uint8_t *)s->s + s->l, &start, &len));
	assert_equal_int(__FILE__, __FUNCTION__, __LINE__, inner->l, len);

	p2 = start;
	assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 1, pbwire_read_varint(&p2, start + len, &v));
	assert_equal_int(__FILE__, __FUNCTION__, __LINE__, ((uint64_t)1 << 3) | 0, v);
	assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 1, pbwire_read_varint(&p2, start + len, &v));
	assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 42, v);

	string_free(inner);
	string_free(s);

    {
        const uint8_t bad_varint[] = { 0x80 };
        const uint8_t *bad_p = bad_varint;
        assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 0,
            pbwire_read_varint(&bad_p, bad_varint + sizeof(bad_varint), &v));
    }

    {
        const uint8_t short_fixed[] = { 1, 2, 3, 4, 5, 6, 7 };
        const uint8_t *short_p = short_fixed;
        assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 0,
            pbwire_read_fixed64(&short_p, short_fixed + sizeof(short_fixed), &raw));
    }

    {
        const uint8_t bad_len[] = { 0x05, 'a', 'b' };
        const uint8_t *bad_p = bad_len;
        const uint8_t *msg_start = NULL;
        size_t msg_len = 0;
        assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 0,
            pbwire_read_len(&bad_p, bad_len + sizeof(bad_len), &msg_start, &msg_len));
    }

    {
        const uint8_t fixed32[] = { 0xde, 0xad, 0xbe, 0xef };
        const uint8_t *p32 = fixed32;
        assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 1,
            pbwire_skip_field(&p32, fixed32 + sizeof(fixed32), 5));
        assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 0, (fixed32 + sizeof(fixed32)) - p32);
    }

    {
        const uint8_t empty[] = {};
        const uint8_t *ep = empty;
        assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 0,
            pbwire_skip_field(&ep, empty + sizeof(empty), 9));
    }

    s = string_init(16);
    pbwire_write_len(s, "", 0);
    assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 1, s->l);
    assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 0, (uint8_t)s->s[0]);
    string_free(s);
}
