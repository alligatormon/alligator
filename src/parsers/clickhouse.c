#include <stdio.h>
#include <inttypes.h>
#include "common/selector.h"
#include "metric/namespace.h"
#include "events/context_arg.h"
//#include "args_parse.h"
#define d64	PRId64
#define CH_NAME_SIZE 255
void clickhouse_system_handler(char *metrics, size_t size, context_arg *carg)
{
	selector_split_metric(metrics, size, "\n", 1, "\t", 1, "Clickhouse_", 11, 0, 0);
}

void clickhouse_columns_handler(char *metrics, size_t size, context_arg *carg)
{
	int64_t i = 0;
	char *database = malloc(CH_NAME_SIZE);
	char *table = malloc(CH_NAME_SIZE);
	char *column = malloc(CH_NAME_SIZE);
	size_t name_size;
	int64_t cur;
	int64_t data_compressed_bytes,data_uncompressed_bytes,marks_bytes;
	while(i<size)
	{
		cur = strcspn(metrics+i, "\t");
		name_size = CH_NAME_SIZE > cur+1 ? cur+1 : CH_NAME_SIZE;
		strlcpy(database, metrics+i, name_size);
		if (!metric_name_validator(database, cur))
		{
			i += strcspn(metrics+i, "\n")+1;
			continue;
		}
		cur++;
		i+=cur;

		cur = strcspn(metrics+i, "\t");
		name_size = CH_NAME_SIZE > cur+1 ? cur+1 : CH_NAME_SIZE;
		strlcpy(table, metrics+i, name_size);
		if (!metric_name_validator(table, cur))
		{
			i += strcspn(metrics+i, "\n")+1;
			continue;
		}
		cur++;
		i+=cur;

		cur = strcspn(metrics+i, "\t");
		name_size = CH_NAME_SIZE > cur+1 ? cur+1 : CH_NAME_SIZE;
		strlcpy(column, metrics+i, name_size);
		if (!metric_name_validator(column, cur))
		{
			i += strcspn(metrics+i, "\n")+1;
			continue;
		}
		cur++;
		i+=cur;

		cur = strcspn(metrics+i, "\t");
		data_compressed_bytes = atoll(metrics+i);
		cur++;
		i+=cur;

		cur = strcspn(metrics+i, "\t");
		data_uncompressed_bytes = atoll(metrics+i);
		cur++;
		i+=cur;

		cur = strcspn(metrics+i, "\n");
		marks_bytes = atoll(metrics+i);
		cur++;
		i+=cur;

		metric_add_labels4("Clickhouse_Table_Stats", &data_compressed_bytes, DATATYPE_INT, 0, "database", database, "table", table, "column", column, "type", "data_compressed_bytes");
		metric_add_labels4("Clickhouse_Table_Stats", &data_uncompressed_bytes, DATATYPE_INT, 0, "database", database, "table", table, "column", column, "type", "data_uncompressed_bytes");
		metric_add_labels4("Clickhouse_Table_Stats", &marks_bytes, DATATYPE_INT, 0, "database", database, "table", table, "column", column, "type", "marks_bytes");
	}
	free(database);
	free(column);
	free(table);
}

void clickhouse_merges_handler(char *metrics, size_t size, context_arg *carg)
{
	int64_t i = 0;
	char *database = malloc(CH_NAME_SIZE);
	char *table = malloc(CH_NAME_SIZE);
	char *is_mutation = malloc(CH_NAME_SIZE);
	size_t name_size;
	int64_t cur;
	int64_t num_parts,total_size_bytes_compressed,total_size_marks,bytes_read_uncompressed,rows_read,bytes_written_uncompressed,rows_written;
	double progress;
	while(i<size)
	{
		cur = strcspn(metrics+i, "\t");
		name_size = CH_NAME_SIZE > cur+1 ? cur+1 : CH_NAME_SIZE;
		strlcpy(database, metrics+i, name_size);
		if (!metric_name_validator(database, cur))
		{
			i += strcspn(metrics+i, "\n")+1;
			continue;
		}
		cur++;
		i+=cur;

		cur = strcspn(metrics+i, "\t");
		name_size = CH_NAME_SIZE > cur+1 ? cur+1 : CH_NAME_SIZE;
		strlcpy(table, metrics+i, name_size);
		if (!metric_name_validator(table, cur))
		{
			i += strcspn(metrics+i, "\n")+1;
			continue;
		}
		cur++;
		i+=cur;

		cur = strcspn(metrics+i, "\t");
		name_size = CH_NAME_SIZE > cur+1 ? cur+1 : CH_NAME_SIZE;
		strlcpy(is_mutation, metrics+i, name_size);
		if (!metric_name_validator(is_mutation, cur))
		{
			i += strcspn(metrics+i, "\n")+1;
			continue;
		}
		cur++;
		i+=cur;

		cur = strcspn(metrics+i, "\t");
		progress = atof(metrics+i);
		cur++;
		i+=cur;

		cur = strcspn(metrics+i, "\n");
		num_parts = atoll(metrics+i);
		cur++;
		i+=cur;

		cur = strcspn(metrics+i, "\n");
		total_size_bytes_compressed = atoll(metrics+i);
		cur++;
		i+=cur;

		cur = strcspn(metrics+i, "\n");
		total_size_marks = atoll(metrics+i);
		cur++;
		i+=cur;

		cur = strcspn(metrics+i, "\n");
		bytes_read_uncompressed = atoll(metrics+i);
		cur++;
		i+=cur;

		cur = strcspn(metrics+i, "\n");
		rows_read = atoll(metrics+i);
		cur++;
		i+=cur;

		cur = strcspn(metrics+i, "\n");
		bytes_written_uncompressed = atoll(metrics+i);
		cur++;
		i+=cur;

		cur = strcspn(metrics+i, "\n");
		rows_written = atoll(metrics+i);
		cur++;
		i+=cur;

		metric_add_labels4("Clickhouse_Merges_Stats", &progress, DATATYPE_DOUBLE, 0, "database", database, "table", table, "mutation", is_mutation, "type", "progress");
		metric_add_labels4("Clickhouse_Merges_Stats", &num_parts, DATATYPE_INT, 0, "database", database, "table", table, "mutation", is_mutation, "type", "num_parts");
		metric_add_labels4("Clickhouse_Merges_Stats", &total_size_bytes_compressed, DATATYPE_INT, 0, "database", database, "table", table, "mutation", is_mutation, "type", "total_size_bytes_compressed");
		metric_add_labels4("Clickhouse_Merges_Stats", &total_size_marks, DATATYPE_INT, 0, "database", database, "table", table, "mutation", is_mutation, "type", "total_size_marks");
		metric_add_labels4("Clickhouse_Merges_Stats", &bytes_read_uncompressed, DATATYPE_INT, 0, "database", database, "table", table, "mutation", is_mutation, "type", "bytes_read_uncompressed");
		metric_add_labels4("Clickhouse_Merges_Stats", &rows_read, DATATYPE_INT, 0, "database", database, "table", table, "mutation", is_mutation, "type", "rows_read");
		metric_add_labels4("Clickhouse_Merges_Stats", &bytes_written_uncompressed, DATATYPE_INT, 0, "database", database, "table", table, "mutation", is_mutation, "type", "bytes_written_uncompressed");
		metric_add_labels4("Clickhouse_Merges_Stats", &rows_written, DATATYPE_INT, 0, "database", database, "table", table, "mutation", is_mutation, "type", "rows_written");
	}
	free(database);
	free(table);
	free(is_mutation);
}

void clickhouse_dictionary_handler(char *metrics, size_t size, context_arg *carg)
{
	int64_t i = 0;
	char *name = malloc(CH_NAME_SIZE);
	size_t name_size;
	int64_t cur;
	int64_t bytes_allocated,query_count,element_count;
	double hit_rate,load_factor;
	while(i<size)
	{
		cur = strcspn(metrics+i, "\t");
		name_size = CH_NAME_SIZE > cur+1 ? cur+1 : CH_NAME_SIZE;
		strlcpy(name, metrics+i, name_size);
		if (!metric_name_validator(name, cur))
		{
			i += strcspn(metrics+i, "\n")+1;
			continue;
		}
		cur++;
		i+=cur;

		cur = strcspn(metrics+i, "\n");
		bytes_allocated = atoll(metrics+i);
		cur++;
		i+=cur;

		cur = strcspn(metrics+i, "\n");
		query_count = atoll(metrics+i);
		cur++;
		i+=cur;

		cur = strcspn(metrics+i, "\n");
		hit_rate = atof(metrics+i);
		cur++;
		i+=cur;

		cur = strcspn(metrics+i, "\n");
		element_count = atoll(metrics+i);
		cur++;
		i+=cur;

		cur = strcspn(metrics+i, "\n");
		load_factor = atof(metrics+i);
		cur++;
		i+=cur;

		metric_add_labels2("Clickhouse_Dictionary_Count", &query_count, DATATYPE_INT, 0, "name", name, "type", "query");
		metric_add_labels2("Clickhouse_Dictionary_Count", &element_count, DATATYPE_INT, 0, "name", name, "type", "element");

		metric_add_labels2("Clickhouse_Dictionary_Stats", &bytes_allocated, DATATYPE_INT, 0, "name", name, "type", "bytes_allocated");

		metric_add_labels2("Clickhouse_Dictionary_Stats", &hit_rate, DATATYPE_DOUBLE, 0, "name", name, "type", "hit_rate");
		metric_add_labels2("Clickhouse_Dictionary_Stats", &load_factor, DATATYPE_DOUBLE, 0, "name", name, "type", "load_factor");
	}
	free(name);
}

void clickhouse_replicas_handler(char *metrics, size_t size, context_arg *carg)
{
	int64_t i = 0;
	char *database = malloc(CH_NAME_SIZE);
	char *table = malloc(CH_NAME_SIZE);
	size_t name_size;
	int64_t cur;
	int64_t leader, readonly, future_parts,parts_to_check,queue_size,inserts_in_queue,merges_in_queue,log_max_index,log_pointer,total_replicas,active_replicas;
	while(i<size)
	{
		cur = strcspn(metrics+i, "\t");
		name_size = CH_NAME_SIZE > cur+1 ? cur+1 : CH_NAME_SIZE;
		strlcpy(database, metrics+i, name_size);
		if (!metric_name_validator(database, cur))
		{
			i += strcspn(metrics+i, "\n")+1;
			continue;
		}
		cur++;
		i+=cur;

		cur = strcspn(metrics+i, "\t");
		name_size = CH_NAME_SIZE > cur+1 ? cur+1 : CH_NAME_SIZE;
		strlcpy(table, metrics+i, name_size);
		if (!metric_name_validator(table, cur))
		{
			i += strcspn(metrics+i, "\n")+1;
			continue;
		}
		cur++;
		i+=cur;

		cur = strcspn(metrics+i, "\n");
		leader = atoll(metrics+i);
		cur++;
		i+=cur;

		cur = strcspn(metrics+i, "\n");
		readonly = atoll(metrics+i);
		cur++;
		i+=cur;


		cur = strcspn(metrics+i, "\n");
		future_parts = atoll(metrics+i);
		cur++;
		i+=cur;

		cur = strcspn(metrics+i, "\n");
		parts_to_check = atoll(metrics+i);
		cur++;
		i+=cur;

		cur = strcspn(metrics+i, "\n");
		queue_size = atoll(metrics+i);
		cur++;
		i+=cur;

		cur = strcspn(metrics+i, "\n");
		inserts_in_queue = atoll(metrics+i);
		cur++;
		i+=cur;

		cur = strcspn(metrics+i, "\n");
		merges_in_queue = atoll(metrics+i);
		cur++;
		i+=cur;

		cur = strcspn(metrics+i, "\n");
		log_max_index = atoll(metrics+i);
		cur++;
		i+=cur;

		cur = strcspn(metrics+i, "\n");
		log_pointer = atoll(metrics+i);
		cur++;
		i+=cur;

		cur = strcspn(metrics+i, "\n");
		total_replicas = atoll(metrics+i);
		cur++;
		i+=cur;

		cur = strcspn(metrics+i, "\n");
		active_replicas = atoll(metrics+i);
		cur++;
		i+=cur;

		metric_add_labels3("Clickhouse_Replicas_Stats", &leader, DATATYPE_INT, 0, "database", database, "table", table, "role", "leader");
		metric_add_labels3("Clickhouse_Replicas_Stats", &readonly, DATATYPE_INT, 0, "database", database, "table", table, "role", "readonly");
		metric_add_labels3("Clickhouse_Replicas_Stats", &future_parts, DATATYPE_INT, 0, "database", database, "table", table, "type", "future_parts");
		metric_add_labels3("Clickhouse_Replicas_Stats", &parts_to_check, DATATYPE_INT, 0, "database", database, "table", table, "type", "parts_to_check");
		metric_add_labels3("Clickhouse_Replicas_Stats", &queue_size, DATATYPE_INT, 0, "database", database, "table", table, "type", "queue_size");
		metric_add_labels3("Clickhouse_Replicas_Stats", &inserts_in_queue, DATATYPE_INT, 0, "database", database, "table", table, "type", "inserts_in_queue");
		metric_add_labels3("Clickhouse_Replicas_Stats", &merges_in_queue, DATATYPE_INT, 0, "database", database, "table", table, "type", "merges_in_queue");
		metric_add_labels3("Clickhouse_Replicas_Stats", &log_max_index, DATATYPE_INT, 0, "database", database, "table", table, "type", "log_max_index");
		metric_add_labels3("Clickhouse_Replicas_Stats", &log_pointer, DATATYPE_INT, 0, "database", database, "table", table, "type", "log_pointer");
		metric_add_labels3("Clickhouse_Replicas_Stats", &total_replicas, DATATYPE_INT, 0, "database", database, "table", table, "type", "total_replicas");
		metric_add_labels3("Clickhouse_Replicas_Stats", &active_replicas, DATATYPE_INT, 0, "database", database, "table", table, "type", "active_replicas");
	}
	free(database);
	free(table);
}
