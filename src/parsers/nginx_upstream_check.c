#include <stdio.h>
#include <string.h>
#include "common/selector.h"
#include "metric/namespace.h"
#include "events/client_info.h"
#include "main.h"
#define NGINX_UPSTREAM_CHECK_SIZE 1000

void nginx_upstream_check_handler(char *metrics, size_t size, client_info *cinfo)
{
	extern aconf *ac;
	int64_t cur;
	int64_t i = 0;
	size_t name_size;
	char upstream[NGINX_UPSTREAM_CHECK_SIZE];
	char server[NGINX_UPSTREAM_CHECK_SIZE];
	char status[NGINX_UPSTREAM_CHECK_SIZE];
	char type[NGINX_UPSTREAM_CHECK_SIZE];
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
		if (!*upstream)
		{
			if (ac->log_level > 2)
				printf("nginx scrape metrics: empty field 'upstream' into stats:\n%s'\n", metrics+i);
		}
		if (!metric_label_validator(upstream, cur))
		{
			i += strcspn(metrics+i, "\n")+1;
			continue;
		}
		cur++;
		i+=cur;

		cur = strcspn(metrics+i, ",");
		name_size = NGINX_UPSTREAM_CHECK_SIZE > cur+1 ? cur+1 : NGINX_UPSTREAM_CHECK_SIZE;
		strlcpy(server, metrics+i, name_size);
		if (!*server)
		{
			if (ac->log_level > 2)
				printf("nginx scrape metrics: empty field 'server' into stats:\n%s'\n", metrics+i);
		}
		if (!metric_label_validator(server, cur))
		{
			i += strcspn(metrics+i, "\n")+1;
			continue;
		}
		cur++;
		i+=cur;

		cur = strcspn(metrics+i, ",");
		name_size = NGINX_UPSTREAM_CHECK_SIZE > cur+1 ? cur+1 : NGINX_UPSTREAM_CHECK_SIZE;
		strlcpy(status, metrics+i, name_size);
		if (!metric_label_validator(server, cur))
		{
			i += strcspn(metrics+i, "\n")+1;
			continue;
		}
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
		if (!metric_label_validator(server, cur))
		{
			i += strcspn(metrics+i, "\n")+1;
			continue;
		}
		cur++;
		i+=cur;

		if (!*server)
			continue;
		if (!*upstream)
			continue;

		uint64_t val = 1;
		uint64_t nval = 0;
		if (!strncmp(status, "up", 2))
		{
			metric_add_labels4("nginx_upstream_check_status", &val, DATATYPE_UINT, 0, "upstream", upstream, "server", server, "status", "up", "type", type);
			metric_add_labels4("nginx_upstream_check_status", &nval, DATATYPE_UINT, 0, "upstream", upstream, "server", server, "status", "down", "type", type);
		}
		else if (!strncmp(status, "down", 4))
		{
			metric_add_labels4("nginx_upstream_check_status", &nval, DATATYPE_UINT, 0, "upstream", upstream, "server", server, "status", "up", "type", type);
			metric_add_labels4("nginx_upstream_check_status", &val, DATATYPE_UINT, 0, "upstream", upstream, "server", server, "status", "down", "type", type);
		}
		metric_add_labels3("nginx_upstream_check_rise_count", &rise, DATATYPE_UINT, 0, "upstream", upstream, "server", server, "type", type);
		metric_add_labels3("nginx_upstream_check_fall_count", &fall, DATATYPE_UINT, 0, "upstream", upstream, "server", server, "type", type);
	}
}
