#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>
#include "common/selector.h"
#include "metric/namespace.h"
#include "events/context_arg.h"
#include "common/aggregator.h"
#include "common/validator.h"
#include "main.h"
#define IPTABLES_LEN 1024
#define IPTABLES_ARGS " -L -v -x -n"

void firewall_handler(char *metrics, size_t size, context_arg *carg)
{
	uint64_t cur = 0;
	uint64_t copysize = 0;
	uint64_t end = 0;
	uint8_t skip_next = 0;
	uint64_t field_size;
	char field[IPTABLES_LEN];
	char chain[IPTABLES_LEN];
	char target[IPTABLES_LEN];
	char prot[IPTABLES_LEN];
	char opt[IPTABLES_LEN];
	char in[IPTABLES_LEN];
	char out[IPTABLES_LEN];
	char source[IPTABLES_LEN];
	char destination[IPTABLES_LEN];
	char comment[IPTABLES_LEN];
	char data[IPTABLES_LEN];
	char dports[IPTABLES_LEN];
	char match_set[IPTABLES_LEN];
	char *table = carg && carg->data ? carg->data : "unknown";

	for (; cur < size; )
	{
		end = strcspn(metrics + cur, "\n");
		*comment = 0;
		*dports = 0;
		*match_set = 0;

		field_size = copysize = end + 1 > IPTABLES_LEN ? IPTABLES_LEN : end + 1;
		strlcpy(field, metrics + cur, copysize);
		if (skip_next)
		{
			skip_next = 0;
		}
		else if (!strncmp(field, "Chain", 5))
		{
			uint64_t ccur = strspn(field + 5, " \t\r");
			uint64_t eend = strcspn(field + 5 + ccur, " \t\r\n");
			copysize = eend + 1 > IPTABLES_LEN ? IPTABLES_LEN : eend + 1;
			strlcpy(chain, field + 5 + ccur, copysize);

			ccur += 5 + copysize;
			char *policy = strstr(field + ccur, "DROP") ? "DROP" : NULL;
			if (!policy)
				policy = strstr(field + ccur, "ACCEPT") ? "ACCEPT" : NULL;

			if (policy)
			{
				ccur += strcspn(field + ccur, "0123456789");
				uint64_t packets = strtoull(field + ccur, NULL, 10);
				ccur += strspn(field + ccur, "0123456789");

				ccur += strcspn(field + ccur, "0123456789");
				uint64_t bytes = strtoull(field + ccur, NULL, 10);
				ccur += strspn(field + ccur, "0123456789");

				metric_add_labels3("firewall_bytes", &bytes, DATATYPE_UINT, carg, "policy", policy, "chain", chain, "table", table);
				metric_add_labels3("firewall_packages", &packets, DATATYPE_UINT, carg, "policy", policy, "chain", chain, "table", table);
			}

			skip_next = 1;
		}
		else
		{
			uint64_t cursor = 0;

			if (ac->log_level > 0)
				printf("firewall field '%s'\n", field);

			int64_t pkts = uint_get_next(field, field_size, ' ', &cursor);
			int64_t bytes = uint_get_next(field, field_size, ' ', &cursor);
			str_get_next(field, target, IPTABLES_LEN, " ", &cursor);
			cursor += strspn(field + cursor, " ");

			str_get_next(field, prot, IPTABLES_LEN, " ", &cursor);
			cursor += strspn(field + cursor, " ");

			str_get_next(field, opt, IPTABLES_LEN, " ", &cursor);
			cursor += strspn(field + cursor, " ");

			str_get_next(field, in, IPTABLES_LEN, " ", &cursor);
			cursor += strspn(field + cursor, " ");

			str_get_next(field, out, IPTABLES_LEN, " ", &cursor);
			cursor += strspn(field + cursor, " ");

			str_get_next(field, source, IPTABLES_LEN, " ", &cursor);
			cursor += strspn(field + cursor, " ");

			str_get_next(field, destination, IPTABLES_LEN, " ", &cursor);
			cursor += strspn(field + cursor, " ");

			size_t data_size = strlcpy(data, field + cursor, end - cursor + 1);
			for (uint64_t j = 0; j < data_size; )
			{
				size_t data_size = strcspn(data + j, " \t");
				if (!strncmp(data + j, "multiport", 9))
				{
				}
				else if (!strncmp(data + j, "dport", 5))
				{
					j += data_size;
					j += strspn(data + j, " \t");
					data_size = strcspn(data + j, " \t");

					strlcpy(dports, data + j, data_size + 1);
				}
				else if (!strncmp(data + j, "match-set", 9))
				{
					j += data_size;
					j += strspn(data + j, " \t");
					data_size = strcspn(data + j, " \t");

					strlcpy(match_set, data + j, data_size + 1);
				}
				else if (!strncmp(data + j, "/*", 2))
				{
					j += data_size;
					j += strspn(data + j, " \t");
					char *end = strstr(data + j, "*/");
					if (!end)
						data_size = size;
					else
						data_size = end - (data + j);

					strlcpy(comment, data + j, data_size);
					prometheus_metric_name_normalizer(comment, data_size - 1);
				}

				j += data_size;
				j += strspn(data + j, " \t");
			}
			//
			//if (ac->log_level > 0)
			//	printf("pkts is %"PRId64", bytes is %"PRId64", target is '%s', prot is '%s', opt is '%s', in is '%s', out is '%s', source is '%s', destination is '%s', comment is '%s'\n", pkts, bytes, target, prot, opt, in, out, source, destination, comment);
			metric_add_labels10("firewall_bytes", &bytes, DATATYPE_UINT, carg, "target", target, "chain", chain, "proto", prot, "opt", opt, "dst", destination, "src", source, "table", table, "comment", comment, "match_set", match_set, "dport", dports);
			metric_add_labels10("firewall_packages", &pkts, DATATYPE_UINT, carg, "target", target, "chain", chain, "proto", prot, "opt", opt, "dst", destination, "src", source, "table", table, "comment", comment, "match_set", match_set, "dport", dports);
		}

		cur += end;
		cur += strspn(metrics + cur, "\n");
	}
	carg->parser_status = 1;
}

void get_iptables_info(char *procfs, char *binarytable, char *table, context_arg *carg)
{
		//get_iptables_info("exec://grep -q conntrack /proc/modules && ip6tables -t nat", "nat", ac->system_carg);
	char url[1024];
	snprintf(url, 1024, "exec://grep -q conntrack %s/modules && %s -t %s", procfs, binarytable, table);
	context_arg *new = aggregator_oneshot(carg, url, strlen(url), strdup(IPTABLES_ARGS), strlen(IPTABLES_ARGS), firewall_handler, "firewall_handler", NULL, NULL, 0, NULL, NULL, 0, NULL, NULL);
	if (new)
	{
		new->data = table;
		new->no_exit_status = 1;
	}
}
