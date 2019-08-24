#include <pcre.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include "pcre_parser.h"
#include "main.h"

regex_match* regex_match_init(char *regexstring, regex_metric *metrics)
{
	regex_match *rematch = calloc(1, sizeof(*rematch));
	rematch->jstack = pcre_jit_stack_alloc(1000,10000);

	int pcreErrorOffset;
	const char *pcreErrorStr;
	// 2 arg is option for pcre (PCRE_UTF8 and etc.)
	rematch->regex_compiled = pcre_compile(regexstring, 0, &pcreErrorStr, &pcreErrorOffset, NULL);

	if(rematch->regex_compiled == NULL) {
		printf("ERROR: Could not compile '%s': %s\n", regexstring, pcreErrorStr);
		free(rematch);
		pcre_jit_stack_free(rematch->jstack);
		return 0;
	}

	rematch->pcreExtra = pcre_study(rematch->regex_compiled, PCRE_STUDY_JIT_COMPILE, &pcreErrorStr);
	if(pcreErrorStr != NULL) {
		printf("ERROR: Could not study '%s': %s\n", regexstring, pcreErrorStr);
		free(rematch);
		pcre_jit_stack_free(rematch->jstack);
		pcre_free(rematch->regex_compiled);
		return 0;
	}

	rematch->metrics = metrics;

	return rematch;
}

void regex_match_free(regex_match *rematch)
{
	pcre_jit_stack_free(rematch->jstack);
	pcre_free(rematch->regex_compiled);
	pcre_free_study(rematch->pcreExtra);
	free(rematch);
}

char *regex_get(pcre *regex_compiled, int *str_vector, int pcreExecRet, const char *regex_match_string, char *pre_key, char *fallback)
{
	int64_t i, j;
	char *key = malloc(255);
	*key = 0;
	for (i=0, j=0; pre_key[i]; ++i, ++j)
	{
		if(pre_key[i] == '$')
		{
			char match[255];
			strlcpy(match, pre_key+i+1, strcspn(pre_key+i, "}"));
			int num = pcre_get_stringnumber(regex_compiled, match);
			if (num < 1)
				return fallback;

			const char *matched_string;
			pcre_get_substring(regex_match_string, str_vector, pcreExecRet, num, &(matched_string));
			strcat(key, matched_string);
			j += strlen(matched_string);
		}
		else
			key[j] = pre_key[i];
	}
	key[j] = 0;

	return key;
}

void pcre_match(regex_match *rematch, const char *regex_match_string)
{
	pcre *regex_compiled = rematch->regex_compiled;
	int str_vector[50];

	pcre_extra *pcreExtra = rematch->pcreExtra;
	pcre_jit_stack* jstack = rematch->jstack;

	int pcreExecRet = pcre_jit_exec(regex_compiled, pcreExtra, regex_match_string, strlen(regex_match_string), 0, 0, str_vector, 50, jstack);

	if(pcreExecRet < 0)
	{
		switch(pcreExecRet)
		{
			case PCRE_ERROR_NOMATCH: ++rematch->nomatch ;break;
			case PCRE_ERROR_NULL: ++rematch->null; break;
			case PCRE_ERROR_BADOPTION: ++rematch->badoption; break;
			case PCRE_ERROR_BADMAGIC: ++rematch->badmagic; break;
			case PCRE_ERROR_UNKNOWN_NODE: ++rematch->unknown_node; break;
			case PCRE_ERROR_NOMEMORY: ++rematch->nomemory; break;
			default: ++rematch->unknown_error; break;
		}
	}
	else
	{
		if(pcreExecRet == 0)
		{
			printf("But too many substrings were found to fit in str_vector!\n");
			pcreExecRet = 50 / 3;
		}

		regex_metric_node *metrics = rematch->metrics->head;
		for(; metrics; metrics = metrics->next)
		{
			char metric_v[255];
			strcpy(metric_v, metrics->metric_name);

			regex_labels_node *label = metrics->labels->head;
			strcat(metric_v, " {");
			for(; label; label = label->next)
			{
				char *key = regex_get(regex_compiled, str_vector, pcreExecRet, regex_match_string, label->name, label->name);
				strcat(metric_v, key);
				strcat(metric_v, "=\"");
				key = regex_get(regex_compiled, str_vector, pcreExecRet, regex_match_string, label->key, label->key);
				strcat(metric_v, key);

				if (label->next)
					strcat(metric_v, "\", ");
				else
					strcat(metric_v, "\"");
			}
			strcat(metric_v, "} ");

			//uint64_t val = atoll(regex_get(regex_compiled, str_vector, pcreExecRet, metrics->from, "1");
			char *val;
			if (metrics->from)
			{
				val = regex_get(regex_compiled, str_vector, pcreExecRet, regex_match_string, metrics->from, "0");
				//val = atoll(val_s);
				//free(val_s);
			}
			else
				val = strdup("1");

			strcat(metric_v, val);
			printf("%s\n", metric_v);

			//if (
				//metric_increase
		}

		//pcre_free_substring(psubStrMatchStr);
	}
}

void pcre_match_multi(regex_match **rematch, size_t rematch_size, const char *regex_match_string)
{
	uint64_t i;
	for (i=0; i<rematch_size; ++i)
		pcre_match(rematch[i], regex_match_string);
}

int ____main(int argc, char **argv) {
	char *testStrings[] = { "This should match... hello",
				"<191>Jun	9 21:05:32 5e7d8b1f4e81 nginx: 2019/06/09 21:05:32 [debug] 17896#0: *210608 write new buf t:1 f:0 000056324EA354D8, pos 000056324EA354D8, size: 239 file: 0, size: 0",
				"<191>Jun	9 21:05:32 5e7d8b1f4e81 nginx: 2019/06/09 21:05:32 [debug] 17896#0: *210608 http write filter: l:0 f:0 s:239",
				"<191>Jun	9 21:05:32 5e7d8b1f4e81 nginx: 2019/06/09 21:05:32 [debug] 17896#0: *210608 http output filter \"/index.html?",
				"<191>Jun	9 21:05:32 5e7d8b1f4e81 nginx: 2019/06/09 21:05:32 [debug] 17896#0: *210608 http copy filter: \"/index.html?",
				"<191>Jun	9 21:05:32 5e7d8b1f4e81 nginx: 2019/06/09 21:05:32 [debug] 17896#0: *210608 image filter",
				"<191>Jun	9 21:05:32 5e7d8b1f4e81 nginx: 2019/06/09 21:05:32 [debug] 17896#0: *210608 xslt filter body",
				"<191>Jun	9 21:05:32 5e7d8b1f4e81 nginx: 2019/06/09 21:05:32 [debug] 17896#0: *210608 http postpone filter \"/index.html?\"",
				"<191>Jun	9 21:05:32 5e7d8b1f4e81 nginx: 2019/06/09 21:05:32 [debug] 17896#0: *210608 write old buf t:1 f:0 000056324EA354D8, pos 000056324EA354D8, size: 239 file: 0, size: 0",
				"<191>Jun	9 21:05:32 5e7d8b1f4e81 nginx: 2019/06/09 21:05:32 [debug] 17896#0: *210608 write new buf t:0 f:1 0000000000000000, pos 0000000000000000, size: 0 file: 0, size: 3700",
				"<191>Jun	9 21:05:32 5e7d8b1f4e81 nginx: 2019/06/09 21:05:32 [debug] 17896#0: *210608 http write filter: l:1 f:0 s:3939",
				"<191>Jun	9 21:05:32 5e7d8b1f4e81 nginx: 2019/06/09 21:05:32 [debug] 17896#0: *210608 http write filter limit 0",
				"<191>Jun	9 21:05:32 5e7d8b1f4e81 nginx: 2019/06/09 21:05:32 [debug] 17896#0: *210608 tcp_nopush",
				"<191>Jun	9 21:05:32 5e7d8b1f4e81 nginx: 2019/06/09 21:05:32 [debug] 17896#0: *210608 writev: 239 of 239",
				"<191>Jun	9 21:05:32 5e7d8b1f4e81 nginx: 2019/06/09 21:05:32 [debug] 17896#0: *210608 sendfile: @0 3700",
				"<191>Jun	9 21:05:32 5e7d8b1f4e81 nginx: 2019/06/09 21:05:32 [debug] 17896#0: *210608 sendfile: 3700 of 3700 @0",
				"<191>Jun	9 21:05:32 5e7d8b1f4e81 nginx: 2019/06/09 21:05:32 [debug] 17896#0: *210608 http write filter 0000000000000000",
				"<191>Jun	9 21:05:32 5e7d8b1f4e81 nginx: 2019/06/09 21:05:32 [debug] 17896#0: *210608 http copy filter: 0 \"/index.html?",
				"<191>Jun	9 21:05:32 5e7d8b1f4e81 nginx: 2019/06/09 21:05:32 [debug] 17896#0: *210608 http finalize request: 0, \"/index.html?\" a:1, c:2",
				"<191>Jun	9 21:05:32 5e7d8b1f4e81 nginx: 2019/06/09 21:05:32 [debug] 17896#0: *210608 http request count:2 blk:0",
				"<191>Jun	9 21:05:32 5e7d8b1f4e81 nginx: 2019/06/09 21:05:32 [debug] 17896#0: *210608 http keepalive handler",
				"<191>Jun	9 21:05:32 5e7d8b1f4e81 nginx: 2019/06/09 21:05:32 [debug] 17896#0: *210608 malloc: 000056324E9D8090:1024",
				"<191>Jun	9 21:05:32 5e7d8b1f4e81 nginx: 2019/06/09 21:05:32 [debug] 17896#0: *210608 recv: eof:1, avail:1",
				"<191>Jun	9 21:05:32 5e7d8b1f4e81 nginx: 2019/06/09 21:05:32 [debug] 17896#0: *210608 recv: fd:8 0 of 1024",
				"<190>Jun	9 21:05:32 5e7d8b1f4e81 nginx: 2019/06/09 21:05:32 [info] 17896#0: *210608 client 127.0.0.1 closed keepalive connection",
				"<191>Jun	9 21:05:32 5e7d8b1f4e81 nginx: 2019/06/09 21:05:32 [debug] 17896#0: *210608 close http connection: 8",
				"<191>Jun	9 21:05:32 5e7d8b1f4e81 nginx: 2019/06/09 21:05:32 [debug] 17896#0: *210608 event timer del: 8: 1560114397586",
				"<191>Jun	9 21:05:32 5e7d8b1f4e81 nginx: 2019/06/09 21:05:32 [debug] 17896#0: *210608 reusable connection: 0",
				"<191>Jun	9 21:05:32 5e7d8b1f4e81 nginx: 2019/06/09 21:05:32 [debug] 17896#0: *210608 free: 000056324E9D8090",
				"<191>Jun	9 21:05:32 5e7d8b1f4e81 nginx: 2019/06/09 21:05:32 [debug] 17896#0: *210608 free: 000056324EA34C90, unused: 136",
				"<191>Jun	9 21:05:32 5e7d8b1f4e81 nginx: 2019/06/09 21:05:32 [debug] 17896#0: timer delta: 7",
				"<191>Jun	9 21:05:32 5e7d8b1f4e81 nginx: 2019/06/09 21:05:32 [debug] 17896#0: worker cycle",
				"<191>Jun	9 21:05:32 5e7d8b1f4e81 nginx: 2019/06/09 21:05:32 [debug] 17896#0: epoll timer: -1",
				"This could match... hello!",
				"More than one hello.. hello",
				"No chance of a match...",
				NULL};

	regex_match **rematch = malloc(sizeof(void*));

	regex_metric *metrics;
	regex_metric_node *metric;
	regex_labels *labels;
	regex_labels_node *label;

	metrics = malloc(sizeof(*metrics));
	metrics->size = 1;
	metric = metrics->head = metrics->tail = malloc(sizeof(*metric));
	metric->next = 0;
	metric->metric_name = "nginx_ad";
	metric->from = 0;
	metric->value = 1;
	labels = malloc(sizeof(*labels));
	metric->labels = labels;
	labels->size = 1;
	label = labels->head = labels->tail = malloc(sizeof(*label));
	label->next = 0;
	label->name = "severity";
	label->key = "aad_$severity";
	rematch[0] = regex_match_init(argv[1], metrics);

	metrics = malloc(sizeof(*metrics));
	metrics->size = 1;
	metric = metrics->head = metrics->tail = malloc(sizeof(*metric));
	metric->next = 0;
	metric->metric_name = "ef";
	metric->from = "$unused";
	metric->value = 0;
	labels = malloc(sizeof(*labels));
	metric->labels = labels;
	labels->size = 1;
	label = labels->head = labels->tail = malloc(sizeof(*label));
	label->next = 0;
	label->name = "eqf2398f";
	label->key = "efv234124wv";
	label->next = labels->tail = malloc(sizeof(*label));
	label = label->next;
	label->next = 0;
	label->name = "efr";
	label->key = "fv";
	rematch[1] = regex_match_init(argv[2], metrics);
	char **regex_match_string;


	for(regex_match_string=testStrings; *regex_match_string; ++regex_match_string)
		pcre_match_multi(rematch, 2, *regex_match_string);
	

	regex_match_free(rematch[0]);
	regex_match_free(rematch[1]);
	free(rematch);

	return 0;

}
