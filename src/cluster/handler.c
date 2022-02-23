#include <stdio.h>
#include <string.h>
#include "common/selector.h"
#include "metric/namespace.h"
#include "events/context_arg.h"
#include "common/aggregator.h"
#include "query/query.h"
#include "main.h"
#define LABEL_LEN 255
#define CLUSTER_HANDLER_REPLICATYPE 0
#define CLUSTER_HANDLER_SERVER 1
#define CLUSTER_HANDLER_DATA 2

void cluster_sync_handler(char *metrics, size_t size, context_arg *carg)
{
	if (size > 1)
		printf("=======\ncluster_sync_handler (%s/%zu) metrics:\n%s\n", carg->key, size, metrics);

	char field[LABEL_LEN];
	char replica[LABEL_LEN];
	char data[LABEL_LEN];

	for (uint64_t i = 0; i < size; i++)
	{
		*field = 0;
		uint8_t cluster_type = CLUSTER_HANDLER_REPLICATYPE;
		uint64_t field_size = str_get_next(metrics, field, LABEL_LEN, "\n", &i);
		printf("\t%s\n", field);
		uint8_t is_primary = 0;
		*replica = 0;

		for (uint64_t j = 0; j < field_size; j++)
		{
			str_get_next(field, data, LABEL_LEN, "\t", &j);

			if (cluster_type == CLUSTER_HANDLER_REPLICATYPE)
			{
				if (!strcmp(data, "replica"))
				{
					cluster_type = CLUSTER_HANDLER_SERVER;
				}
				else
				{
					cluster_type = CLUSTER_HANDLER_DATA;
					is_primary = 1;
				}
			}
			else if (cluster_type == CLUSTER_HANDLER_SERVER)
			{
				strlcpy(replica, data, LABEL_LEN);
				cluster_type = CLUSTER_HANDLER_DATA;
			}
			else if (cluster_type == CLUSTER_HANDLER_DATA)
			{
				printf("\t\t{'primary': %"PRIu8", 'replica': '%s', 'data': '%s'\n", is_primary, replica, data);
			}
		}
	}
}
