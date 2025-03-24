#include <stdio.h>
#include <string.h>
#include "common/selector.h"
#include "metric/namespace.h"
#include "events/context_arg.h"
#include "main.h"

void auditd_handler(char *metrics, size_t size, context_arg *carg)
{
	char line[2048];
	char token[512];
	char key[256];
	char value[256];
	for (uint64_t i = 0; i < size; i++)
	{
		alligator_ht *lbl = alligator_ht_init(NULL);
		uint64_t field_len = str_get_next(metrics, line, 2048, "\n", &i);
		for (uint64_t j = 0, k = 0; j < field_len; j++, k++)
		{
			str_get_next(line, token, 512, " ", &j);

			uint64_t k = 0;
			str_get_next(token, key, 256, "=", &k);
			k += strspn(token + k, "=\"'");
			str_get_next(token, value, 256, "=\"", &k);
            if (!strcmp(key, "type")) {
				labels_hash_insert_nocache(lbl, key, value);
			} else if (!strcmp(key, "SYSCALL")) {
				labels_hash_insert_nocache(lbl, key, value);
			} else if (!strcmp(key, "exe")) {
				labels_hash_insert_nocache(lbl, key, value);
			} else if (!strcmp(key, "key")) {
				labels_hash_insert_nocache(lbl, key, value);
			} else if (!strcmp(key, "success")) {
				labels_hash_insert_nocache(lbl, key, value);
			} else if (!strcmp(key, "AUID")) {
				labels_hash_insert_nocache(lbl, key, value);
			} else if (!strcmp(key, "UID")) {
				labels_hash_insert_nocache(lbl, key, value);
			} else if (!strcmp(key, "GID")) {
				labels_hash_insert_nocache(lbl, key, value);
			}

			//printf("key=%s, value=%s\n", key, value);
		}
		uint64_t okval = 1;
		metric_update("auditd_event", lbl, &okval, DATATYPE_UINT, carg);
	}


	carg->parser_status = 1;
}
