#include <stdio.h>
#include <string.h>
#include "common/selector.h"
#include "metric/namespace.h"
#include "events/context_arg.h"
#include "main.h"

static inline void auditd_label_insert(alligator_ht *lbl, char *key, char *value)
{
	if (!strcmp(key, "type")) {
		labels_hash_insert_nocache(lbl, key, value);
	} else if (!strcmp(key, "exe")) {
		labels_hash_insert_nocache(lbl, key, value);
	} else if (!strcmp(key, "key")) {
		labels_hash_insert_nocache(lbl, key, value);
	} else if (!strcmp(key, "success")) {
		labels_hash_insert_nocache(lbl, key, value);
	} else if (!strcmp(key, "auid") || !strcmp(key, "AUID")) {
		labels_hash_insert_nocache(lbl, "AUID", value);
	} else if (!strcmp(key, "uid") || !strcmp(key, "UID")) {
		labels_hash_insert_nocache(lbl, "UID", value);
	} else if (!strcmp(key, "gid") || !strcmp(key, "GID")) {
		labels_hash_insert_nocache(lbl, "GID", value);
	}
}

static inline void auditd_parse_line(char *line, uint64_t line_len, alligator_ht *lbl)
{
	char key[256];
	char value[256];
	uint64_t i = 0;

	while (i < line_len)
	{
		i += strspn(line + i, " \t\r");
		if (i >= line_len)
			break;

		uint64_t key_start = i;
		uint64_t eq = strcspn(line + i, "=");
		if (eq == 0 || i + eq >= line_len || line[i + eq] != '=')
		{
			i += strcspn(line + i, " \t\r");
			continue;
		}

		uint64_t key_len = eq < (sizeof(key) - 1) ? eq : (sizeof(key) - 1);
		memcpy(key, line + key_start, key_len);
		key[key_len] = 0;

		i += eq + 1;

		uint64_t val_start = i;
		uint64_t val_len = 0;

		if (i < line_len && (line[i] == '"' || line[i] == '\''))
		{
			char quote = line[i++];
			val_start = i;
			while (i < line_len && line[i] != quote)
				i++;
			val_len = i - val_start;
			if (i < line_len && line[i] == quote)
				i++;
		}
		else
		{
			val_len = strcspn(line + i, " \t\r");
			i += val_len;
		}

		uint64_t copy_len = val_len < (sizeof(value) - 1) ? val_len : (sizeof(value) - 1);
		memcpy(value, line + val_start, copy_len);
		value[copy_len] = 0;

		auditd_label_insert(lbl, key, value);
	}
}

void auditd_handler(char *metrics, size_t size, context_arg *carg)
{
	char line[2048];

	for (uint64_t i = 0; i < size; i++)
	{
		uint64_t field_len = str_get_next(metrics, line, sizeof(line), "\n", &i);
		if (field_len == 0 || strspn(line, " \t\r") == field_len)
			continue;

		alligator_ht *lbl = alligator_ht_init(NULL);
		auditd_parse_line(line, field_len, lbl);
		uint64_t okval = 1;
		metric_update("auditd_event", lbl, &okval, DATATYPE_UINT, carg);
	}

	carg->parser_status = 1;
}
