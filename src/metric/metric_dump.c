#include "common/selector.h"
#include "main.h"

//void metric_dump_free(char *data)
//{
//	string_free(data);
//}

void metric_dump(int exit_sig)
{
	extern aconf *ac;
	//if (!ac->persistence_dir && exit_sig >= 0)
	//	exit(exit_sig);
	if (!ac->persistence_dir)
		return;

	string *body = string_init(10000000);
	metric_str_build(0, body);
	char dirtowrite[255];
	snprintf(dirtowrite, 255, "%s/metric_dump", ac->persistence_dir);
	if (exit_sig >= 0)
	{
		//int *code = malloc(sizeof(*code));
		//*code = 0;
		//write_to_file(dirtowrite, body->s, body->l, NULL, code);
		FILE *fd = fopen(dirtowrite, "w");
		fwrite(body->s, body->l, 1, fd);
		fclose(fd);
	}
	else
		write_to_file(dirtowrite, body->s, body->l, free, body);
}

void restore_callback(char *buf, size_t len, void *data)
{
	alligator_multiparser(buf, len, NULL, NULL, NULL);
}

void metric_restore()
{
	extern aconf *ac;

	char dirtoread[255];
	snprintf(dirtoread, 255, "%s/metric_dump", ac->persistence_dir);
	read_from_file(dirtoread, 0, restore_callback, NULL);
}
