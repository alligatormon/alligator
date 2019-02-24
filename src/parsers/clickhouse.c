#include <stdio.h>
#include <inttypes.h>
#include "common/selector.h"
#include "dstructures/metric.h"
//#include "args_parse.h"
#define d64	PRId64
#define CH_NAME_SIZE 255
void clickhouse_system_handler(char *metrics, size_t size, char *instance, int kind)
{
	selector_split_metric(metrics, size, "\n", 1, "\t", 1, "Clickhouse_", 11, 0, 0);
}

void clickhouse_columns_handler(char *metrics, size_t size, char *instance, int kind)
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

		metric_labels_add_lbl4("Clickhouse_Table_Stats", &data_compressed_bytes, ALLIGATOR_DATATYPE_INT, 0, "database", database, "table", table, "column", column, "type", "data_compressed_bytes");
		metric_labels_add_lbl4("Clickhouse_Table_Stats", &data_uncompressed_bytes, ALLIGATOR_DATATYPE_INT, 0, "database", database, "table", table, "column", column, "type", "data_uncompressed_bytes");
		metric_labels_add_lbl4("Clickhouse_Table_Stats", &marks_bytes, ALLIGATOR_DATATYPE_INT, 0, "database", database, "table", table, "column", column, "type", "marks_bytes");
	}
	free(database);
	free(column);
	free(table);
}

void clickhouse_merges_handler(char *metrics, size_t size, char *instance, int kind)
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

		metric_labels_add_lbl4("Clickhouse_Merges_Stats", &progress, ALLIGATOR_DATATYPE_DOUBLE, 0, "database", database, "table", table, "mutation", is_mutation, "type", "progress");
		metric_labels_add_lbl4("Clickhouse_Merges_Stats", &num_parts, ALLIGATOR_DATATYPE_INT, 0, "database", database, "table", table, "mutation", is_mutation, "type", "num_parts");
		metric_labels_add_lbl4("Clickhouse_Merges_Stats", &total_size_bytes_compressed, ALLIGATOR_DATATYPE_INT, 0, "database", database, "table", table, "mutation", is_mutation, "type", "total_size_bytes_compressed");
		metric_labels_add_lbl4("Clickhouse_Merges_Stats", &total_size_marks, ALLIGATOR_DATATYPE_INT, 0, "database", database, "table", table, "mutation", is_mutation, "type", "total_size_marks");
		metric_labels_add_lbl4("Clickhouse_Merges_Stats", &bytes_read_uncompressed, ALLIGATOR_DATATYPE_INT, 0, "database", database, "table", table, "mutation", is_mutation, "type", "bytes_read_uncompressed");
		metric_labels_add_lbl4("Clickhouse_Merges_Stats", &rows_read, ALLIGATOR_DATATYPE_INT, 0, "database", database, "table", table, "mutation", is_mutation, "type", "rows_read");
		metric_labels_add_lbl4("Clickhouse_Merges_Stats", &bytes_written_uncompressed, ALLIGATOR_DATATYPE_INT, 0, "database", database, "table", table, "mutation", is_mutation, "type", "bytes_written_uncompressed");
		metric_labels_add_lbl4("Clickhouse_Merges_Stats", &rows_written, ALLIGATOR_DATATYPE_INT, 0, "database", database, "table", table, "mutation", is_mutation, "type", "rows_written");
	}
	free(database);
	free(table);
	free(is_mutation);
}

void clickhouse_dictionary_handler(char *metrics, size_t size, char *instance, int kind)
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

		metric_labels_add_lbl2("Clickhouse_Dictionary_Count", &query_count, ALLIGATOR_DATATYPE_INT, 0, "name", name, "type", "query");
		metric_labels_add_lbl2("Clickhouse_Dictionary_Count", &element_count, ALLIGATOR_DATATYPE_INT, 0, "name", name, "type", "element");

		metric_labels_add_lbl2("Clickhouse_Dictionary_Stats", &bytes_allocated, ALLIGATOR_DATATYPE_INT, 0, "name", name, "type", "bytes_allocated");

		metric_labels_add_lbl2("Clickhouse_Dictionary_Stats", &hit_rate, ALLIGATOR_DATATYPE_DOUBLE, 0, "name", name, "type", "hit_rate");
		metric_labels_add_lbl2("Clickhouse_Dictionary_Stats", &load_factor, ALLIGATOR_DATATYPE_DOUBLE, 0, "name", name, "type", "load_factor");
	}
	free(name);
}

void clickhouse_replicas_handler(char *metrics, size_t size, char *instance, int kind)
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

		metric_labels_add_lbl3("Clickhouse_Replicas_Stats", &leader, ALLIGATOR_DATATYPE_INT, 0, "database", database, "table", table, "role", "leader");
		metric_labels_add_lbl3("Clickhouse_Replicas_Stats", &readonly, ALLIGATOR_DATATYPE_INT, 0, "database", database, "table", table, "role", "readonly");
		metric_labels_add_lbl3("Clickhouse_Replicas_Stats", &future_parts, ALLIGATOR_DATATYPE_INT, 0, "database", database, "table", table, "type", "future_parts");
		metric_labels_add_lbl3("Clickhouse_Replicas_Stats", &parts_to_check, ALLIGATOR_DATATYPE_INT, 0, "database", database, "table", table, "type", "parts_to_check");
		metric_labels_add_lbl3("Clickhouse_Replicas_Stats", &queue_size, ALLIGATOR_DATATYPE_INT, 0, "database", database, "table", table, "type", "queue_size");
		metric_labels_add_lbl3("Clickhouse_Replicas_Stats", &inserts_in_queue, ALLIGATOR_DATATYPE_INT, 0, "database", database, "table", table, "type", "inserts_in_queue");
		metric_labels_add_lbl3("Clickhouse_Replicas_Stats", &merges_in_queue, ALLIGATOR_DATATYPE_INT, 0, "database", database, "table", table, "type", "merges_in_queue");
		metric_labels_add_lbl3("Clickhouse_Replicas_Stats", &log_max_index, ALLIGATOR_DATATYPE_INT, 0, "database", database, "table", table, "type", "log_max_index");
		metric_labels_add_lbl3("Clickhouse_Replicas_Stats", &log_pointer, ALLIGATOR_DATATYPE_INT, 0, "database", database, "table", table, "type", "log_pointer");
		metric_labels_add_lbl3("Clickhouse_Replicas_Stats", &total_replicas, ALLIGATOR_DATATYPE_INT, 0, "database", database, "table", table, "type", "total_replicas");
		metric_labels_add_lbl3("Clickhouse_Replicas_Stats", &active_replicas, ALLIGATOR_DATATYPE_INT, 0, "database", database, "table", table, "type", "active_replicas");
	}
	free(database);
	free(table);
}

//void clickhouse_system_handler(char *metrics, size_t size, char *instance, int kind)
//{
//	printf("metric '%s'(%zu)\n", metrics, size);
//	char field[1000];
//	int64_t i;
//	for ( i=0; selector_getline(metrics, size, field, 1000, i) ; i++ )
//	{
//		//if ( kind == AGR_TYPE_CLICKHOUSE_REPLICAS )
//		//{
//			char database[255], table[255];
//			int64_t is_leader,is_readonly,future_parts,parts_to_check,queue_size,inserts_in_queue,merges_in_queue,log_max_index,total_replicas,active_replicas;
//
//
//			sscanf(field, "%s\t%s\t%"d64"\t%"d64"\t%"d64"\t%"d64"\t%"d64"\t%"d64"\t%"d64"\t%"d64"\t%"d64"\t%"d64"", database, table, &is_leader,&is_readonly,&future_parts,&parts_to_check,&queue_size,&inserts_in_queue,&merges_in_queue,&log_max_index,&total_replicas,&active_replicas);
//			metric_labels_add_lbl3("Clickhouse_Replicas_Leader", &is_leader, ALLIGATOR_DATATYPE_INT, 0, "instance", instance, "database", database, "table", table);
//			metric_labels_add_lbl3("Clickhouse_Replicas_Readonly", &is_readonly, ALLIGATOR_DATATYPE_INT, 0, "instance", instance, "database", database, "table", table);
//			metric_labels_add_lbl3("Clickhouse_Replicas_Future_Parts", &future_parts, ALLIGATOR_DATATYPE_INT, 0, "instance", instance, "database", database, "table", table);
//			metric_labels_add_lbl3("Clickhouse_Replicas_Parts_to_Check", &parts_to_check, ALLIGATOR_DATATYPE_INT, 0, "instance", instance, "database", database, "table", table);
//			metric_labels_add_lbl3("Clickhouse_Replicas_Queue_Size", &queue_size, ALLIGATOR_DATATYPE_INT, 0, "instance", instance, "database", database, "table", table);
//			metric_labels_add_lbl3("Clickhouse_Replicas_Inserts_In_Queue", &inserts_in_queue, ALLIGATOR_DATATYPE_INT, 0, "instance", instance, "database", database, "table", table);
//			metric_labels_add_lbl3("Clickhouse_Replicas_Merges_In_Queue", &merges_in_queue, ALLIGATOR_DATATYPE_INT, 0, "instance", instance, "database", database, "table", table);
//			metric_labels_add_lbl3("Clickhouse_Replicas_Log_Max_Index", &log_max_index, ALLIGATOR_DATATYPE_INT, 0, "instance", instance, "database", database, "table", table);
//			metric_labels_add_lbl3("Clickhouse_Replicas_Total_Replicas", &total_replicas, ALLIGATOR_DATATYPE_INT, 0, "instance", instance, "database", database, "table", table);
//			metric_labels_add_lbl3("Clickhouse_Replicas_Active_Replicas", &active_replicas, ALLIGATOR_DATATYPE_INT, 0, "instance", instance, "database", database, "table", table);
//		//}
//
//
//		//if ( kind == AGR_TYPE_CLICKHOUSE_PROCESS )
//		//{
//		//	char user[255], address[255], query[255];
//		//	int64_t read_rows,read_bytes,written_rows,written_bytes,total_rows_approx,memory_usage;
//		//	double elapsed;
//		//	sscanf(field, "%s\t%s\t%s\t%lf\t%"d64"\t%"d64"\t%"d64"\t%"d64"\t%"d64"\t%"d64"", user, address, query, &elapsed,&read_rows,&read_bytes,&written_rows,&written_bytes,&total_rows_approx,&memory_usage);
//		//	metric_labels_add_lbl3("Clickhouse_Process_Elapsed", &elapsed, ALLIGATOR_DATATYPE_DOUBLE, 0, "instance", instance,  "address", address, "query_id", query);
//		//	metric_labels_add_lbl3("Clickhouse_Process_Read_Bytes", &read_bytes, ALLIGATOR_DATATYPE_INT, 0, "instance", instance,  "address", address, "query_id", query);
//		//	metric_labels_add_lbl3("Clickhouse_Process_Written_Bytes", &written_bytes, ALLIGATOR_DATATYPE_INT, 0, "instance", instance,  "address", address, "query_id", query);
//		//	metric_labels_add_lbl3("Clickhouse_Process_Read_Rows", &read_rows, ALLIGATOR_DATATYPE_INT, 0, "instance", instance,  "address", address, "query_id", query);
//		//	metric_labels_add_lbl3("Clickhouse_Process_Written_Rows", &written_rows, ALLIGATOR_DATATYPE_INT, 0, "instance", instance,  "address", address, "query_id", query);
//		//	metric_labels_add_lbl3("Clickhouse_Process_Total_Rows_Approx", &total_rows_approx, ALLIGATOR_DATATYPE_INT, 0, "instance", instance,  "address", address, "query_id", query);
//		//	metric_labels_add_lbl3("Clickhouse_Process_Memory_Usage", &memory_usage, ALLIGATOR_DATATYPE_INT, 0, "instance", instance,  "address", address, "query_id", query);
//		//}
//	}
//}
