#include "common/selector.h"

void test_selector_getline()
{
	puts("=========\ntest_selector_getline\n============\n");
	char *field = malloc(10001);
	char *path = strdup("tests/unit/selector/getline1.txt");
	char *file = gettextfile(path, NULL);
	char strpath[1000];
	int i;
	for (i=0; i<10; i++)
	{
		snprintf(strpath, 1000, "tests/unit/selector/getline1str%d.txt", i);
		printf("iteration %s\n", strpath);
		size_t size;
		char *data = gettextfile(strpath, &size);
		size--;
		data[size] = 0;
		selector_getline(file, strlen(file), field, 10001, i);
		cut_assert_equal_string(field, data);
		cut_assert_equal_int(strlen(field), size);
	}
}

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
