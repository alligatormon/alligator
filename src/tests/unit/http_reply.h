#include "parsers/http_proto.h"
void test_http_reply_parser()
{
	puts("=========\ntest http_reply_parser \n==========\n");
	char path[1000];
	size_t size;

	char mesg_path[1000];
	size_t mesg_size;

	char ver_path[1000];
	int64_t ver;

	char code_path[1000];
	int64_t code;

	int i;
	for (i=1; i<6; i++)
	{
		snprintf(path, 1000, "tests/unit/http_reply_example/%d.http", i);
		char *data = gettextfile(path, &size);
		cut_assert_not_null(data);
		printf("iteration %s\n", path);

		snprintf(mesg_path, 1000, "tests/unit/http_reply_example/mesg%d.http", i);
		char *mesg = gettextfile(mesg_path, &mesg_size);
		mesg_size--;
		cut_assert_not_null(mesg);
		mesg[mesg_size]=0;

		snprintf(ver_path, 1000, "tests/unit/http_reply_example/ver%d.http", i);
		ver = getkvfile(ver_path);

		snprintf(code_path, 1000, "tests/unit/http_reply_example/code%d.http", i);
		code = getkvfile(code_path);

		http_reply_data *hrdata = http_reply_parser(data, size);
		cut_assert_not_null(hrdata);
		cut_assert_equal_string(hrdata->mesg, mesg);
		cut_assert_equal_int(hrdata->http_version, ver);
		cut_assert_equal_int(hrdata->http_code, code);
		free(data);
		free(mesg);
	}
}
