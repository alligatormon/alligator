#include <stdio.h>
#include <inttypes.h>
#include "common/selector.h"
#include "dstructures/metric.h"
//#include "args_parse.h"
#define d64	PRId64
void clickhouse_system_handler(char *metrics, size_t size, char *instance, int kind)
{
	char field[1000];
	int64_t i;
	for ( i=0; selector_getline(metrics, size, field, 1000, i) ; i++ )
	{
		//if ( kind == AGR_TYPE_CLICKHOUSE_REPLICAS )
		//{
			char database[255], table[255];
			int64_t is_leader,is_readonly,future_parts,parts_to_check,queue_size,inserts_in_queue,merges_in_queue,log_max_index,total_replicas,active_replicas;


			sscanf(field, "%s\t%s\t%"d64"\t%"d64"\t%"d64"\t%"d64"\t%"d64"\t%"d64"\t%"d64"\t%"d64"\t%"d64"\t%"d64"", database, table, &is_leader,&is_readonly,&future_parts,&parts_to_check,&queue_size,&inserts_in_queue,&merges_in_queue,&log_max_index,&total_replicas,&active_replicas);
			metric_labels_add_lbl3("Clickhouse_Replicas_Leader", &is_leader, ALLIGATOR_DATATYPE_INT, 0, "instance", instance, "database", database, "table", table);
			metric_labels_add_lbl3("Clickhouse_Replicas_Readonly", &is_readonly, ALLIGATOR_DATATYPE_INT, 0, "instance", instance, "database", database, "table", table);
			metric_labels_add_lbl3("Clickhouse_Replicas_Future_Parts", &future_parts, ALLIGATOR_DATATYPE_INT, 0, "instance", instance, "database", database, "table", table);
			metric_labels_add_lbl3("Clickhouse_Replicas_Parts_to_Check", &parts_to_check, ALLIGATOR_DATATYPE_INT, 0, "instance", instance, "database", database, "table", table);
			metric_labels_add_lbl3("Clickhouse_Replicas_Queue_Size", &queue_size, ALLIGATOR_DATATYPE_INT, 0, "instance", instance, "database", database, "table", table);
			metric_labels_add_lbl3("Clickhouse_Replicas_Inserts_In_Queue", &inserts_in_queue, ALLIGATOR_DATATYPE_INT, 0, "instance", instance, "database", database, "table", table);
			metric_labels_add_lbl3("Clickhouse_Replicas_Merges_In_Queue", &merges_in_queue, ALLIGATOR_DATATYPE_INT, 0, "instance", instance, "database", database, "table", table);
			metric_labels_add_lbl3("Clickhouse_Replicas_Log_Max_Index", &log_max_index, ALLIGATOR_DATATYPE_INT, 0, "instance", instance, "database", database, "table", table);
			metric_labels_add_lbl3("Clickhouse_Replicas_Total_Replicas", &total_replicas, ALLIGATOR_DATATYPE_INT, 0, "instance", instance, "database", database, "table", table);
			metric_labels_add_lbl3("Clickhouse_Replicas_Active_Replicas", &active_replicas, ALLIGATOR_DATATYPE_INT, 0, "instance", instance, "database", database, "table", table);
		//}


		//if ( kind == AGR_TYPE_CLICKHOUSE_PROCESS )
		//{
		//	char user[255], address[255], query[255];
		//	int64_t read_rows,read_bytes,written_rows,written_bytes,total_rows_approx,memory_usage;
		//	double elapsed;
		//	sscanf(field, "%s\t%s\t%s\t%lf\t%"d64"\t%"d64"\t%"d64"\t%"d64"\t%"d64"\t%"d64"", user, address, query, &elapsed,&read_rows,&read_bytes,&written_rows,&written_bytes,&total_rows_approx,&memory_usage);
		//	metric_labels_add_lbl3("Clickhouse_Process_Elapsed", &elapsed, ALLIGATOR_DATATYPE_DOUBLE, 0, "instance", instance,  "address", address, "query_id", query);
		//	metric_labels_add_lbl3("Clickhouse_Process_Read_Bytes", &read_bytes, ALLIGATOR_DATATYPE_INT, 0, "instance", instance,  "address", address, "query_id", query);
		//	metric_labels_add_lbl3("Clickhouse_Process_Written_Bytes", &written_bytes, ALLIGATOR_DATATYPE_INT, 0, "instance", instance,  "address", address, "query_id", query);
		//	metric_labels_add_lbl3("Clickhouse_Process_Read_Rows", &read_rows, ALLIGATOR_DATATYPE_INT, 0, "instance", instance,  "address", address, "query_id", query);
		//	metric_labels_add_lbl3("Clickhouse_Process_Written_Rows", &written_rows, ALLIGATOR_DATATYPE_INT, 0, "instance", instance,  "address", address, "query_id", query);
		//	metric_labels_add_lbl3("Clickhouse_Process_Total_Rows_Approx", &total_rows_approx, ALLIGATOR_DATATYPE_INT, 0, "instance", instance,  "address", address, "query_id", query);
		//	metric_labels_add_lbl3("Clickhouse_Process_Memory_Usage", &memory_usage, ALLIGATOR_DATATYPE_INT, 0, "instance", instance,  "address", address, "query_id", query);
		//}
	}
}
