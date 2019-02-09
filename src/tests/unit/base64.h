#include "common/base64.h"
#define TEST_BASE64_1 "test"
#define TEST_BASE64_2 "ahjuNgohV8Shahngu4aevierahph8Roo8eeC7E"
#define TEST_BASE64_3 "jeimie4ooYuv1ohque6bof8eeSh3buim3quaeng1thuy7wooJi7bi8Ao2ahtoquahtheuveegahp7Noof9HahdaeWai6eib6jaec"
void test_base64_1()
{
	size_t len = strlen(TEST_BASE64_1);
	size_t encode_length;
	size_t decode_length;
	const char *encode = base64_encode(TEST_BASE64_1, len, &encode_length);
	char *decode = base64_decode(encode, encode_length, &decode_length);
	cut_assert_equal_string(TEST_BASE64_1, decode);
	cut_assert_equal_int(len, decode_length);
}

void test_base64_2()
{
	size_t len = strlen(TEST_BASE64_2);
	size_t encode_length;
	size_t decode_length;
	const char *encode = base64_encode(TEST_BASE64_2, len, &encode_length);
	char *decode = base64_decode(encode, encode_length, &decode_length);
	cut_assert_equal_string(TEST_BASE64_2, decode);
	cut_assert_equal_int(len, decode_length);
}

void test_base64_3()
{
	size_t len = strlen(TEST_BASE64_3);
	size_t encode_length;
	size_t decode_length;
	const char *encode = base64_encode(TEST_BASE64_3, len, &encode_length);
	char *decode = base64_decode(encode, encode_length, &decode_length);
	cut_assert_equal_string(TEST_BASE64_3, (char*)decode);
	cut_assert_equal_int(len, decode_length);
}
