#include <stdio.h>
#include <string.h>
#include "common/selector.h"
#include "metric/namespace.h"
#include "events/context_arg.h"
#include "main.h"
#define NGINX_UPSTREAM_CHECK_SIZE 1000

void nginx_upstream_check_handler(char *metrics, size_t size, context_arg *carg)
{
	extern aconf *ac;
	int64_t cur;
	int64_t i = 0;
	size_t name_size;
	char upstream[NGINX_UPSTREAM_CHECK_SIZE];
	char server[NGINX_UPSTREAM_CHECK_SIZE];
	char status[NGINX_UPSTREAM_CHECK_SIZE];
	char type[NGINX_UPSTREAM_CHECK_SIZE];

	char upstream_counter[NGINX_UPSTREAM_CHECK_SIZE];
	int64_t upstream_counter_live = 0;
	int64_t upstream_counter_dead = 0;
	*upstream_counter = 0;

	uint64_t rise;
	uint64_t fall;

	while(i<size)
	{
		//++i;
		cur = strcspn(metrics+i, ",");
		cur++;
		i+=cur;

		cur = strcspn(metrics+i, ",");
		name_size = NGINX_UPSTREAM_CHECK_SIZE > cur+1 ? cur+1 : NGINX_UPSTREAM_CHECK_SIZE;
		strlcpy(upstream, metrics+i, name_size);
		size_t upstream_len = name_size-1;
		if (!*upstream)
		{
			if (ac->log_level > 2)
				printf("nginx scrape metrics: empty field 'upstream' into stats:\n'%s'\n", metrics+i);
			break;
		}
		if (!metric_label_validator(upstream, cur))
		{
			i += strcspn(metrics+i, "\n")+1;
			continue;
		}
		if (*upstream_counter == 0)
			strlcpy(upstream_counter, upstream, upstream_len+1);
		cur++;
		i+=cur;

		cur = strcspn(metrics+i, ",");
		name_size = NGINX_UPSTREAM_CHECK_SIZE > cur+1 ? cur+1 : NGINX_UPSTREAM_CHECK_SIZE;
		strlcpy(server, metrics+i, name_size);
		size_t server_len = name_size-1;
		if (!*server)
		{
			if (ac->log_level > 2)
				printf("nginx scrape metrics: empty field 'server' into stats:\n'%s'\n", metrics+i);
			break;
		}
		cur++;
		i+=cur;

		cur = strcspn(metrics+i, ",");
		name_size = NGINX_UPSTREAM_CHECK_SIZE > cur+1 ? cur+1 : NGINX_UPSTREAM_CHECK_SIZE;
		strlcpy(status, metrics+i, name_size);
		size_t status_len = name_size-1;
		cur++;
		i+=cur;

		cur = strcspn(metrics+i, ",");
		rise = atoll(metrics+i);
		cur++;
		i+=cur;

		cur = strcspn(metrics+i, ",");
		fall = atoll(metrics+i);
		cur++;
		i+=cur;

		cur = strcspn(metrics+i, ",");
		name_size = NGINX_UPSTREAM_CHECK_SIZE > cur+1 ? cur+1 : NGINX_UPSTREAM_CHECK_SIZE;
		strlcpy(type, metrics+i, name_size);
		size_t type_len = name_size-1;
		cur++;
		i+=cur;

		if (ac->log_level > 3)
		{
			printf("server is '%s'\n", server);
			printf("upstream is '%s'\n", upstream);
			printf("type is '%s'\n", type);
			printf("status is '%s'\n", status);
		}
		if (!*server)
			continue;
		if (ac->log_level > 3)
			puts("validate server is not null");

		if (!*upstream)
			continue;
		if (ac->log_level > 3)
			puts("validate upstream is not null");

		if (!metric_label_validator(type, type_len))
		{
			puts("validate type is ERR");
			i += strcspn(metrics+i, "\n")+1;
			continue;
		}
		if (ac->log_level > 3)
			puts("validate type is OK");
		if (!metric_label_validator(status, status_len))
		{
			puts("validate status is ERR");
			i += strcspn(metrics+i, "\n")+1;
			continue;
		}
		if (ac->log_level > 3)
			puts("validate status is OK");
		if (!metric_label_validator(server, server_len))
		{
			puts("validate server is ERR");
			i += strcspn(metrics+i, "\n")+1;
			continue;
		}
		if (ac->log_level > 3)
			puts("validate server is OK");
		if (!metric_label_validator(upstream, upstream_len))
		{
			puts("validate upstream is ERR");
			i += strcspn(metrics+i, "\n")+1;
			continue;
		}
		if (ac->log_level > 3)
			puts("validate upstream is OK");

		uint64_t val = 1;
		uint64_t nval = 0;
		if (!strncmp(status, "up", 2))
		{
			metric_add_labels4("nginx_upstream_check_status", &val, DATATYPE_UINT, 0, "upstream", upstream, "server", server, "status", "up", "type", type);
			metric_add_labels4("nginx_upstream_check_status", &nval, DATATYPE_UINT, 0, "upstream", upstream, "server", server, "status", "down", "type", type);
			//printf("compare %s <> %s\n", upstream_counter, upstream);
			if (!strcmp(upstream_counter, upstream))
			{
				++upstream_counter_live;
			}
			else
			{
				double percent_live = (100.0 * upstream_counter_live) / (upstream_counter_live + upstream_counter_dead);
				double percent_dead = (100.0 * upstream_counter_dead) / (upstream_counter_live + upstream_counter_dead);

				metric_add_labels("nginx_upstream_upstream_live_count", &upstream_counter_live, DATATYPE_UINT, 0, "upstream", upstream_counter);
				metric_add_labels("nginx_upstream_upstream_dead_count", &upstream_counter_dead, DATATYPE_UINT, 0, "upstream", upstream_counter);
				metric_add_labels("nginx_upstream_upstream_live_percent", &percent_live, DATATYPE_DOUBLE, 0, "upstream", upstream_counter);
				metric_add_labels("nginx_upstream_upstream_dead_percent", &percent_dead, DATATYPE_DOUBLE, 0, "upstream", upstream_counter);

				upstream_counter_live = 1;
				upstream_counter_dead = 0;
				strlcpy(upstream_counter, upstream, upstream_len+1);
			}
		}
		else if (!strncmp(status, "down", 4))
		{
			metric_add_labels4("nginx_upstream_check_status", &nval, DATATYPE_UINT, 0, "upstream", upstream, "server", server, "status", "up", "type", type);
			metric_add_labels4("nginx_upstream_check_status", &val, DATATYPE_UINT, 0, "upstream", upstream, "server", server, "status", "down", "type", type);
			//printf("compare %s <> %s\n", upstream_counter, upstream);
			if (!strcmp(upstream_counter, upstream))
			{
				++upstream_counter_dead;
			}
			else
			{
				double percent_live = (100.0 * upstream_counter_live) / (upstream_counter_live + upstream_counter_dead);
				double percent_dead = (100.0 * upstream_counter_dead) / (upstream_counter_live + upstream_counter_dead);

				metric_add_labels("nginx_upstream_upstream_live_count", &upstream_counter_live, DATATYPE_UINT, 0, "upstream", upstream_counter);
				metric_add_labels("nginx_upstream_upstream_dead_count", &upstream_counter_dead, DATATYPE_UINT, 0, "upstream", upstream_counter);
				metric_add_labels("nginx_upstream_upstream_live_percent", &percent_live, DATATYPE_DOUBLE, 0, "upstream", upstream_counter);
				metric_add_labels("nginx_upstream_upstream_dead_percent", &percent_dead, DATATYPE_DOUBLE, 0, "upstream", upstream_counter);

				upstream_counter_live = 0;
				upstream_counter_dead = 1;
				strlcpy(upstream_counter, upstream, upstream_len+1);
			}
		}
		metric_add_labels3("nginx_upstream_check_rise_count", &rise, DATATYPE_UINT, 0, "upstream", upstream, "server", server, "type", type);
		metric_add_labels3("nginx_upstream_check_fall_count", &fall, DATATYPE_UINT, 0, "upstream", upstream, "server", server, "type", type);
	}

	double percent_live = (100.0 * upstream_counter_live) / (upstream_counter_live + upstream_counter_dead);
	double percent_dead = (100.0 * upstream_counter_dead) / (upstream_counter_live + upstream_counter_dead);

	metric_add_labels("nginx_upstream_upstream_live_count", &upstream_counter_live, DATATYPE_UINT, 0, "upstream", upstream_counter);
	metric_add_labels("nginx_upstream_upstream_dead_count", &upstream_counter_dead, DATATYPE_UINT, 0, "upstream", upstream_counter);
	metric_add_labels("nginx_upstream_upstream_live_percent", &percent_live, DATATYPE_DOUBLE, 0, "upstream", upstream_counter);
	metric_add_labels("nginx_upstream_upstream_dead_percent", &percent_dead, DATATYPE_DOUBLE, 0, "upstream", upstream_counter);
}
