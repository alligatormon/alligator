#include <dirent.h>
#include <sys/stat.h>
#include <jansson.h>
#include "common/selector.h"
#include "common/yaml.h"
#include "config/plain.h"

void config_json(char *json)
{
	http_api_v1(NULL, NULL, json);
}

void config_parse_entry(char *filepath)
{
	string* context = get_file_content(filepath);

	json_error_t error;
	json_t *root = json_loads(context->s, 0, &error);
	if (root)
	{
		json_decref(root);
		puts("json loaded");
		config_json(context->s);
		return;
	}
	else {
		fprintf(stderr, "json error on line %d: %s\n", error.line, error.text);
	}

	char *json = yaml_file_to_json_str(filepath);
	root = json_loads(json, 0, &error);
	if (root)
	{
		json_decref(root);
		puts("yaml loaded");
		config_json(json);
		free(json);
		return;
	}
	else
	{
		printf("File %s is not a json or yaml\n", filepath);
	}

	json = config_plain_to_json(context);
	printf("config_plain_to_json return:\n'%s'\n", json);
	config_json(json);
}

void parse_configs(char *dirpath)
{
	struct stat path_stat;

	char gendir[1000];
	snprintf(gendir, 1000, "%s.json", dirpath);
	FILE *fd = fopen(gendir, "r");
	if (!fd)
	{
		printf("Skip open %s\n", gendir);

		snprintf(gendir, 1000, "%s.yaml", dirpath);
		fd = fopen(gendir, "r");
		if (!fd)
		{
			printf("Skip open %s\n", gendir);

			snprintf(gendir, 1000, "%s.conf", dirpath);
			fd = fopen(gendir, "r");
			if (!fd)
			{
				printf("Skip open %s\n", gendir);
			}
			else
			{
				printf("Use config %s\n", gendir);
				config_parse_entry(gendir);
			}
		}
		else
		{
			printf("Use config %s\n", gendir);
			config_parse_entry(gendir);
		}
	}
	else
	{
		printf("Use config %s\n", gendir);
		config_parse_entry(gendir);
	}

	int rc = stat(dirpath, &path_stat);
	if (rc)
	{
		printf("1 Skip directory: %s: %d\n", dirpath, rc);
	}

	if (S_ISDIR(path_stat.st_mode))
	{
		struct dirent *entry;

		DIR *dp = opendir(dirpath);
		if (!dp)
		{	
			printf("2 Skip directory: %s\n", dirpath);
		}
		else
		{
			while((entry = readdir(dp)))
			{
				if (entry->d_name[0] == '.')
					continue;

				char filepath[1000];
				snprintf(filepath, 1000, "%s/%s", dirpath, entry->d_name);
				printf("Use config %s\n", filepath);
				config_parse_entry(filepath);

			}

			closedir(dp);
		}
	}
	else if (S_ISREG(path_stat.st_mode))
	{
		printf("Use config %s\n", dirpath);
		config_parse_entry(dirpath);
	}
}
