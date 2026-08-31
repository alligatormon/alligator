#include <ctype.h>
#include <dirent.h>
#include <sys/stat.h>
#include <jansson.h>
#include "common/selector.h"
#include "common/yaml.h"
#include "config/plain.h"
#include "common/logs.h"
#include "api/api.h"
#include "main.h"

void config_json(char *json)
{
	http_api_v1(NULL, NULL, json);
}

void config_parse_entry(char *filepath)
{
	string* context = get_file_content(filepath, 1);
	if (!context)
		return;

	json_error_t error;
	json_t *root = json_loads(context->s, 0, &error);
	if (root)
	{
		json_decref(root);
		glog(L_INFO, "json loaded: '%s'\n", filepath);
		config_json(context->s);
		string_free(context);
		return;
	}
	else {
		const char *s = context->s;
		while (s && *s && isspace((unsigned char)*s))
			s++;
		if (s && *s == '{') {
			glog(L_ERROR,
			    "config: invalid JSON in '%s' at line %d column %d: %s "
			    "(plain parser not used; fix JSON syntax and reload)\n",
			    filepath, error.line, error.column,
			    error.text[0] ? error.text : "parse error");
			string_free(context);
			return;
		}
		if (strstr(filepath, ".json"))
			glog(L_WARN, "is not a json %d: %s\n", error.line, error.text);
	}

	char *json = NULL;
	if (strstr(filepath, ".yaml"))
	{
		json = yaml_file_to_json_str(filepath);
		glog(L_TRACE, "config yaml to json converted:\n'%s'\n", json);
		root = json_loads(json, 0, &error);
		if (root)
		{
			json_decref(root);
			glog(L_INFO, "yaml loaded: '%s'\n", filepath);
			config_json(json);
			free(json);
			return;
		}
	}
	else
	{
		glog(L_INFO, "File %s is not a json or yaml, use plain config parser\n", filepath);
	}

	json = config_plain_to_json(context);

	glog(L_ERROR, "config_plain_to_json returned json:\n'%s'\n", json);
	config_json(json);
	free(json);
	string_free(context);
}

void parse_configs(char *dirpath)
{
	struct stat path_stat;

	char gendir[1000];
	snprintf(gendir, 1000, "%s.json", dirpath);
	FILE *fd = fopen(gendir, "r");
	if (!fd)
	{
		glog(L_DEBUG, "config: skip path %s\n", gendir);

		snprintf(gendir, 1000, "%s.yaml", dirpath);
		fd = fopen(gendir, "r");
		if (!fd)
		{
			glog(L_DEBUG, "config: skip path %s\n", gendir);

			snprintf(gendir, 1000, "%s.conf", dirpath);
			fd = fopen(gendir, "r");
			if (!fd)
			{
				glog(L_DEBUG, "config: skip path %s\n", gendir);
			}
			else
			{
				glog(L_INFO, "config: loading %s\n", gendir);
				config_parse_entry(gendir);
				fclose(fd);
			}
		}
		else
		{
			glog(L_INFO, "config: loading %s\n", gendir);
			config_parse_entry(gendir);
			fclose(fd);
		}
	}
	else
	{
		glog(L_INFO, "config: loading %s\n", gendir);
		config_parse_entry(gendir);
		fclose(fd);
	}

	int rc = stat(dirpath, &path_stat);
	if (rc)
	{
		glog(L_DEBUG, "config: skip directory %s (stat failed: %d)\n", dirpath, rc);
	} 
	else if (S_ISDIR(path_stat.st_mode))
	{
		struct dirent *entry;

		DIR *dp = opendir(dirpath);
		if (!dp)
		{	
			glog(L_DEBUG, "config: skip directory %s (opendir failed)\n", dirpath);
		}
		else
		{
			while((entry = readdir(dp)))
			{
				if (entry->d_name[0] == '.')
					continue;

				char filepath[1000];
				snprintf(filepath, 1000, "%s/%s", dirpath, entry->d_name);
				glog(L_INFO, "config: loading %s\n", filepath);
				config_parse_entry(filepath);

			}

			closedir(dp);
		}
	}
	else if (S_ISREG(path_stat.st_mode))
	{
		glog(L_INFO, "config: loading %s\n", dirpath);
		config_parse_entry(dirpath);
	}
}
