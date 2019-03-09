#include <stdio.h>
#include <string.h>
#include "common/selector.h"
#include "dstructures/metric.h"
#define GEARMAND_NAME_SIZE 1000

void gearmand_handler(char *metrics, size_t size, char *instance, int kind)
{
	char cdc[GEARMAND_NAME_SIZE];
	int64_t i;
	int64_t total = 0;
	int64_t running = 0;
	int64_t available_workers = 0;

	int64_t ctx_total = 0;
	int64_t ctx_running = 0;
	int64_t ctx_available_workers = 0;

	size_t name_size;

	for (i=0; i<size; )
	{
		int64_t cursor = 0;
		int to = strcspn(metrics+i, "\t");
		name_size = GEARMAND_NAME_SIZE > to+1 ? to+1 : GEARMAND_NAME_SIZE;
		strlcpy(cdc, metrics+i, name_size);
		if ((to == 1) || (*cdc == '.' && to == 2))
			break;

		ctx_total = int_get_next(metrics+i, size, '\t', &cursor);
		metric_labels_add_lbl("gearmand_total", &ctx_total, ALLIGATOR_DATATYPE_INT, 0, "function", cdc);

		ctx_running = int_get_next(metrics+i, size, '\t', &cursor);
		metric_labels_add_lbl("gearmand_running", &ctx_running, ALLIGATOR_DATATYPE_INT, 0, "function", cdc);

		ctx_available_workers = int_get_next(metrics+i, size, '\t', &cursor);
		metric_labels_add_lbl("gearmand_available_workers", &ctx_available_workers, ALLIGATOR_DATATYPE_INT, 0, "function", cdc);

		total += ctx_total;
		running += ctx_running;
		available_workers += ctx_available_workers;

		i += cursor;
	}

	metric_labels_add_auto("gearmand_server_total", &total, ALLIGATOR_DATATYPE_INT, 0);
	metric_labels_add_auto("gearmand_server_running", &running, ALLIGATOR_DATATYPE_INT, 0);
	metric_labels_add_auto("gearmand_server_available_workers", &available_workers, ALLIGATOR_DATATYPE_INT, 0);
}
