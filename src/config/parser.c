#include <dirent.h>
#include <sys/stat.h>
#include <jansson.h>
#include "common/selector.h"
#include "common/yaml.h"
#include "config/plain.h"
#include "main.h"

void config_json(char *json)
{
	http_api_v1(NULL, NULL, json);
}

void config_parse_entry(char *filepath)
{
	string* context = get_file_content(filepath);
	if (!context)
		return;

	json_error_t error;
	json_t *root = json_loads(context->s, 0, &error);
	if (root)
	{
		json_decref(root);
		puts("json loaded");
		config_json(context->s);
		string_free(context);
		return;
	}
	else {
		if (strstr(filepath, ".json"))
			printf("is not a json %d: %s\n", error.line, error.text);
	}

	char *json = NULL;
	if (strstr(filepath, ".yaml"))
	{
		json = yaml_file_to_json_str(filepath);
		if (ac->log_level)
			printf("config yaml to json converted:\n'%s'\n", json);
		root = json_loads(json, 0, &error);
		if (root)
		{
			json_decref(root);
			puts("yaml loaded");
			config_json(json);
			free(json);
			return;
		}
	}
	else
	{
		if (ac->log_level > 0)
			printf("File %s is not a json or yaml, use plain config parser\n", filepath);
	}

	json = config_plain_to_json(context);
	if (ac->log_level > 0)
		printf("config_plain_to_json return:\n'%s'\n", json);
	config_json(json);
	free(json);
}

void parse_configs(char *dirpath)
{
	struct stat path_stat;

	char gendir[1000];
	snprintf(gendir, 1000, "%s.json", dirpath);
	FILE *fd = fopen(gendir, "r");
	if (!fd)
	{
		if (ac->log_level > 0)
			printf("Skip open %s\n", gendir);

		snprintf(gendir, 1000, "%s.yaml", dirpath);
		fd = fopen(gendir, "r");
		if (!fd)
		{
			if (ac->log_level > 0)
				printf("Skip open %s\n", gendir);

			snprintf(gendir, 1000, "%s.conf", dirpath);
			fd = fopen(gendir, "r");
			if (!fd)
			{
				if (ac->log_level > 0)
					printf("Skip open %s\n", gendir);
			}
			else
			{
				if (ac->log_level > 0)
					printf("Use config %s\n", gendir);
				config_parse_entry(gendir);
			}
		}
		else
		{
			if (ac->log_level > 0)
				printf("Use config %s\n", gendir);
			config_parse_entry(gendir);
		}
	}
	else
	{
		if (ac->log_level > 0)
			printf("Use config %s\n", gendir);
		config_parse_entry(gendir);
	}

	int rc = stat(dirpath, &path_stat);
	if (rc)
	{
		if (ac->log_level > 0)
			printf("1 Skip directory: %s: %d\n", dirpath, rc);
	} 
	else if (S_ISDIR(path_stat.st_mode))
	{
		struct dirent *entry;

		DIR *dp = opendir(dirpath);
		if (!dp)
		{	
			if (ac->log_level > 0)
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
				if (ac->log_level > 0)
					printf("Use config %s\n", filepath);
				config_parse_entry(filepath);

			}

			closedir(dp);
		}
	}
	else if (S_ISREG(path_stat.st_mode))
	{
		if (ac->log_level > 0)
			printf("Use config %s\n", dirpath);
		config_parse_entry(dirpath);
	}
}
