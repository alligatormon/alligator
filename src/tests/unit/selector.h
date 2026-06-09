#include "common/selector.h"

void test_get_file_size()
{
	cut_assert_equal_int(0, get_file_size("tests/unit/selector/filesize0"));
	cut_assert_equal_int(1, get_file_size("tests/unit/selector/filesize1"));
	cut_assert_equal_int(12, get_file_size("tests/unit/selector/filesize12"));
	cut_assert_equal_int(1024, get_file_size("tests/unit/selector/filesize1024"));
	cut_assert_equal_int(4096, get_file_size("tests/unit/selector/filesize4096"));
}

void test_string()
{
	string *str = string_init(12);
	string_cat(str, "123456789101112", 15);
	uint newsize = str->m > 12;
	cut_assert_equal_int(1, newsize);
}

void test1_string()
{
	char *test1 = strdup("BIG-sizE");
	to_lower_s(test1, 2);
	cut_assert_equal_string("biG-sizE", test1);
}

void test2_string()
{
	char *test = strdup("ALLIGATOR__ENTRYPOINT0__TCP0=1111");
	to_lower_s(test, 22);
	cut_assert_equal_string("alligator__entrypoint0__TCP0=1111", test);
}

void test3_string()
{
	char *test = strdup("ALLIGATOR__ENTRYPOINT0__HANDLER=RsYsLoG");
	to_lower_before(test, "=");
	cut_assert_equal_string("alligator__entrypoint0__handler=RsYsLoG", test);
}
