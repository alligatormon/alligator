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
