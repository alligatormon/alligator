#include "common/base64.h"
#define TEST_BASE64_1 "test"
#define TEST_BASE64_2 "ahjuNgohV8Shahngu4aevierahph8Roo8eeC7E"
#define TEST_BASE64_3 "jeimie4ooYuv1ohque6bof8eeSh3buim3quaeng1thuy7wooJi7bi8Ao2ahtoquahtheuveegahp7Noof9HahdaeWai6eib6jaec"

#define TEST_BASE64_4 "Etah4fauChaen2aekohnie0soh0ooxeid6ib9eid8waiC4queewah9ahxax8eiGo"
#define TEST_EBASE64_4 "RXRhaDRmYXVDaGFlbjJhZWtvaG5pZTBzb2gwb294ZWlkNmliOWVpZDh3YWlDNHF1ZWV3YWg5YWh4YXg4ZWlHbw=="

#define TEST_BASE64_5 "ohXipiel3jiegoo7iek4thothei8AighoCh9OThie7LaeChaij7biikileisdhaai6"
#define TEST_EBASE64_5 "b2hYaXBpZWwzamllZ29vN2llazR0aG90aGVpOEFpZ2hvQ2g5T1RoaWU3TGFlQ2hhaWo3Ymlpa2lsZWlzZGhhYWk2"

#define TEST_BASE64_6 "keez6eecheifaeWok6lohch6keinosid6eax0uP9Aich4beeBeefei9Shoopaif2akeet3av6ATo2Phairi0uapheosheeT7MaiWaitooFah9aik3eingeib9tu6Chaiwai3iPh7EoWaer4eipheitae8uad2SioQu4biezie1biqu5mai7vooqu2Xomahth0eiye9mief5uaChai1vuapheiY4miethohti5weith2gu4bi7cuoheeng9zo4tu"
#define TEST_EBASE64_6 "a2VlejZlZWNoZWlmYWVXb2s2bG9oY2g2a2Vpbm9zaWQ2ZWF4MHVQOUFpY2g0YmVlQmVlZmVpOVNob29wYWlmMmFrZWV0M2F2NkFUbzJQaGFpcmkwdWFwaGVvc2hlZVQ3TWFpV2FpdG9vRmFoOWFpazNlaW5nZWliOXR1NkNoYWl3YWkzaVBoN0VvV2FlcjRlaXBoZWl0YWU4dWFkMlNpb1F1NGJpZXppZTFiaXF1NW1haTd2b29xdTJYb21haHRoMGVpeWU5bWllZjV1YUNoYWkxdnVhcGhlaVk0bWlldGhvaHRpNXdlaXRoMmd1NGJpN2N1b2hlZW5nOXpvNHR1"

#define TEST_BASE64_7 "iach7Es1gohToo3viekee4deaX8ohgahchiel6ooX0aeZaeba2chahgh2ohphig8P"
#define TEST_EBASE64_7 "aWFjaDdFczFnb2hUb28zdmlla2VlNGRlYVg4b2hnYWhjaGllbDZvb1gwYWVaYWViYTJjaGFoZ2gyb2hwaGlnOFA="

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
	cut_assert_equal_string(TEST_BASE64_3, decode);
	cut_assert_equal_int(len, decode_length);
}

void test_base64_4()
{
	size_t len = strlen(TEST_BASE64_4);
	size_t encode_length;
	const char *encode = base64_encode(TEST_BASE64_4, len, &encode_length);
	cut_assert_equal_string(TEST_EBASE64_4, encode);
}

void test_base64_5()
{
	size_t len = strlen(TEST_BASE64_5);
	size_t encode_length;
	const char *encode = base64_encode(TEST_BASE64_5, len, &encode_length);
	cut_assert_equal_string(TEST_EBASE64_5, encode);
}

void test_base64_6()
{
	size_t len = strlen(TEST_EBASE64_6);
	size_t decode_length;
	char *decode = base64_decode(TEST_EBASE64_6, len, &decode_length);
	cut_assert_equal_string(TEST_BASE64_6, decode);
	cut_assert_equal_int(len, decode_length);
}

void test_base64_7()
{
	size_t len = strlen(TEST_EBASE64_7);
	size_t decode_length;
	char *decode = base64_decode(TEST_EBASE64_7, len, &decode_length);
	cut_assert_equal_string(TEST_BASE64_7, decode);
	cut_assert_equal_int(len, decode_length);
}
