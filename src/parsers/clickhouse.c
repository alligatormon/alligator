#include <stdio.h>
#include <inttypes.h>
#include "common/selector.h"
#include "metric/namespace.h"
#include "events/context_arg.h"
#include "common/aggregator.h"
#include "common/http.h"
#include "main.h"
#define d64	PRId64
#define CH_NAME_SIZE 255
void clickhouse_system_handler(char *metrics, size_t size, context_arg *carg)
{
	plain_parse(metrics, size, "\t", "\r\n", "Clickhouse_", 11, carg);
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
		//printf("db name_size %lld\n", name_size);
		if (name_size < 3)
			break;

		strlcpy(database, metrics+i, name_size);
		//printf("database %s\n", database);
		if (!metric_name_validator(database, cur))
		{
			i += strcspn(metrics+i, "\n")+1;
			continue;
		}
		cur++;
		i+=cur;

		cur = strcspn(metrics+i, "\t");
		name_size = CH_NAME_SIZE > cur+1 ? cur+1 : CH_NAME_SIZE;
		//printf("table name_size %lld\n", name_size);
		if (name_size < 3)
			break;

		strlcpy(table, metrics+i, name_size);
		//printf("table %s\n", table);
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

		char test[255];
		cur = strcspn(metrics+i, "\t");
		data_compressed_bytes = atoll(metrics+i);
		strlcpy(test, metrics+i, cur+1);
		//printf("(%lld/%zu) {%s/%s} 1 atoll from %s, result: %lld\n", i, size, database, table, test, data_compressed_bytes);
		cur++;
		i+=cur;

		cur = strcspn(metrics+i, "\t");
		data_uncompressed_bytes = atoll(metrics+i);
		strlcpy(test, metrics+i, cur+1);
		//printf("(%lld/%zu) {%s/%s} 2 atoll from %s, result: %lld\n", i, size, database, table, test, data_uncompressed_bytes);
		cur++;
		i+=cur;

		cur = strcspn(metrics+i, "\n");
		marks_bytes = atoll(metrics+i);
		strlcpy(test, metrics+i, cur+1);
		//printf("(%lld/%zu) {%s/%s} 3 atoll from %s, result: %lld\n", i, size, database, table, test, marks_bytes);
		cur++;
		i+=cur;

		metric_add_labels4("Clickhouse_Table_Stats", &data_compressed_bytes, DATATYPE_INT, carg, "database", database, "table", table, "column", column, "type", "data_compressed_bytes");
		metric_add_labels4("Clickhouse_Table_Stats", &data_uncompressed_bytes, DATATYPE_INT, carg, "database", database, "table", table, "column", column, "type", "data_uncompressed_bytes");
		metric_add_labels4("Clickhouse_Table_Stats", &marks_bytes, DATATYPE_INT, carg, "database", database, "table", table, "column", column, "type", "marks_bytes");
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

		metric_add_labels4("Clickhouse_Merges_Stats", &progress, DATATYPE_DOUBLE, carg, "database", database, "table", table, "mutation", is_mutation, "type", "progress");
		metric_add_labels4("Clickhouse_Merges_Stats", &num_parts, DATATYPE_INT, carg, "database", database, "table", table, "mutation", is_mutation, "type", "num_parts");
		metric_add_labels4("Clickhouse_Merges_Stats", &total_size_bytes_compressed, DATATYPE_INT, carg, "database", database, "table", table, "mutation", is_mutation, "type", "total_size_bytes_compressed");
		metric_add_labels4("Clickhouse_Merges_Stats", &total_size_marks, DATATYPE_INT, carg, "database", database, "table", table, "mutation", is_mutation, "type", "total_size_marks");
		metric_add_labels4("Clickhouse_Merges_Stats", &bytes_read_uncompressed, DATATYPE_INT, carg, "database", database, "table", table, "mutation", is_mutation, "type", "bytes_read_uncompressed");
		metric_add_labels4("Clickhouse_Merges_Stats", &rows_read, DATATYPE_INT, carg, "database", database, "table", table, "mutation", is_mutation, "type", "rows_read");
		metric_add_labels4("Clickhouse_Merges_Stats", &bytes_written_uncompressed, DATATYPE_INT, carg, "database", database, "table", table, "mutation", is_mutation, "type", "bytes_written_uncompressed");
		metric_add_labels4("Clickhouse_Merges_Stats", &rows_written, DATATYPE_INT, carg, "database", database, "table", table, "mutation", is_mutation, "type", "rows_written");
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

		metric_add_labels2("Clickhouse_Dictionary_Count", &query_count, DATATYPE_INT, carg, "name", name, "type", "query");
		metric_add_labels2("Clickhouse_Dictionary_Count", &element_count, DATATYPE_INT, carg, "name", name, "type", "element");

		metric_add_labels2("Clickhouse_Dictionary_Stats", &bytes_allocated, DATATYPE_INT, carg, "name", name, "type", "bytes_allocated");

		metric_add_labels2("Clickhouse_Dictionary_Stats", &hit_rate, DATATYPE_DOUBLE, carg, "name", name, "type", "hit_rate");
		metric_add_labels2("Clickhouse_Dictionary_Stats", &load_factor, DATATYPE_DOUBLE, carg, "name", name, "type", "load_factor");
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

		cur = strcspn(metrics+i, "\t");
		leader = atoll(metrics+i);
		cur++;
		i+=cur;

		cur = strcspn(metrics+i, "\t");
		readonly = atoll(metrics+i);
		cur++;
		i+=cur;


		cur = strcspn(metrics+i, "\t");
		future_parts = atoll(metrics+i);
		cur++;
		i+=cur;

		cur = strcspn(metrics+i, "\t");
		parts_to_check = atoll(metrics+i);
		cur++;
		i+=cur;

		cur = strcspn(metrics+i, "\t");
		queue_size = atoll(metrics+i);
		cur++;
		i+=cur;

		cur = strcspn(metrics+i, "\t");
		inserts_in_queue = atoll(metrics+i);
		cur++;
		i+=cur;

		cur = strcspn(metrics+i, "\t");
		merges_in_queue = atoll(metrics+i);
		cur++;
		i+=cur;

		cur = strcspn(metrics+i, "\t");
		log_max_index = atoll(metrics+i);
		cur++;
		i+=cur;

		cur = strcspn(metrics+i, "\t");
		log_pointer = atoll(metrics+i);
		cur++;
		i+=cur;

		cur = strcspn(metrics+i, "\t");
		total_replicas = atoll(metrics+i);
		cur++;
		i+=cur;

		cur = strcspn(metrics+i, "\n");
		active_replicas = atoll(metrics+i);
		cur++;
		i+=cur;

		metric_add_labels3("Clickhouse_Replicas_Stats", &leader, DATATYPE_INT, carg, "database", database, "table", table, "role", "leader");
		metric_add_labels3("Clickhouse_Replicas_Stats", &readonly, DATATYPE_INT, carg, "database", database, "table", table, "role", "readonly");
		metric_add_labels3("Clickhouse_Replicas_Stats", &future_parts, DATATYPE_INT, carg, "database", database, "table", table, "type", "future_parts");
		metric_add_labels3("Clickhouse_Replicas_Stats", &parts_to_check, DATATYPE_INT, carg, "database", database, "table", table, "type", "parts_to_check");
		metric_add_labels3("Clickhouse_Replicas_Stats", &queue_size, DATATYPE_INT, carg, "database", database, "table", table, "type", "queue_size");
		metric_add_labels3("Clickhouse_Replicas_Stats", &inserts_in_queue, DATATYPE_INT, carg, "database", database, "table", table, "type", "inserts_in_queue");
		metric_add_labels3("Clickhouse_Replicas_Stats", &merges_in_queue, DATATYPE_INT, carg, "database", database, "table", table, "type", "merges_in_queue");
		metric_add_labels3("Clickhouse_Replicas_Stats", &log_max_index, DATATYPE_INT, carg, "database", database, "table", table, "type", "log_max_index");
		metric_add_labels3("Clickhouse_Replicas_Stats", &log_pointer, DATATYPE_INT, carg, "database", database, "table", table, "type", "log_pointer");
		metric_add_labels3("Clickhouse_Replicas_Stats", &total_replicas, DATATYPE_INT, carg, "database", database, "table", table, "type", "total_replicas");
		metric_add_labels3("Clickhouse_Replicas_Stats", &active_replicas, DATATYPE_INT, carg, "database", database, "table", table, "type", "active_replicas");
	}
	free(database);
	free(table);
}

string *clickhouse_gen_url(host_aggregator_info *hi, char *addition)
{
	return string_init_add(gen_http_query(0, hi->query, addition, hi->host, "alligator", hi->auth, 1, NULL), 0, 0);
}

string* clickhouse_system_mesg(host_aggregator_info *hi, void *arg) { return clickhouse_gen_url(hi, "/?query=select%20metric,value\%20from\%20system.metrics"); }
string* clickhouse_system_asynchronous_mesg(host_aggregator_info *hi, void *arg) { return clickhouse_gen_url(hi, "/?query=select%20metric,value\%20from\%20system.asynchronous_metrics"); }
string* clickhouse_system_events_mesg(host_aggregator_info *hi, void *arg) { return clickhouse_gen_url(hi, "/?query=select%20event,value%20from\%20system.events"); }
string* clickhouse_columns_mesg(host_aggregator_info *hi, void *arg) { return clickhouse_gen_url(hi, "/?query=select%20database,table,name,data_compressed_bytes,data_uncompressed_bytes,marks_bytes%20from\%20system.columns%20where%20database!=%27system%27"); }
string* clickhouse_dictionary_mesg(host_aggregator_info *hi, void *arg) { return clickhouse_gen_url(hi, "/?query=select%20name,bytes_allocated,query_count,hit_rate,element_count,load_factor%20from\%20system.dictionaries"); }
string* clickhouse_merges_mesg(host_aggregator_info *hi, void *arg) { return clickhouse_gen_url(hi, "/?query=select%20database,is_mutation,table,progress,num_parts,total_size_bytes_compressed,total_size_marks,bytes_read_uncompressed,rows_read,bytes_written_uncompressed,rows_written%20from\%20system.merges"); }
string* clickhouse_replicas_mesg(host_aggregator_info *hi, void *arg) { return clickhouse_gen_url(hi, "/?query=select%20database,table,is_leader,is_readonly,future_parts,parts_to_check,queue_size,inserts_in_queue,merges_in_queue,log_max_index,log_pointer,total_replicas,active_replicas%20from\%20system.replicas"); }

void clickhouse_parser_push()
{
	aggregate_context *actx = calloc(1, sizeof(*actx));

	actx->key = strdup("clickhouse");
	actx->handlers = 7;
	actx->handler = calloc(1, sizeof(*actx->handler)*actx->handlers);

	actx->handler[0].name = clickhouse_system_handler;
	actx->handler[0].validator = NULL;
	actx->handler[0].mesg_func = clickhouse_system_mesg;
	strlcpy(actx->handler[0].key,"clickhouse_system", 255);

	actx->handler[1].name = clickhouse_system_handler;
	actx->handler[1].validator = NULL;
	actx->handler[1].mesg_func = clickhouse_system_asynchronous_mesg;
	strlcpy(actx->handler[1].key,"clickhouse_system_asynchronous", 255);

	actx->handler[2].name = clickhouse_system_handler;
	actx->handler[2].validator = NULL;
	actx->handler[2].mesg_func = clickhouse_system_events_mesg;
	strlcpy(actx->handler[2].key,"clickhouse_system_events", 255);

	actx->handler[3].name = clickhouse_columns_handler;
	actx->handler[3].validator = NULL;
	actx->handler[3].mesg_func = clickhouse_columns_mesg;
	strlcpy(actx->handler[3].key,"clickhouse_columns", 255);

	actx->handler[4].name = clickhouse_dictionary_handler;
	actx->handler[4].validator = NULL;
	actx->handler[4].mesg_func = clickhouse_dictionary_mesg;
	strlcpy(actx->handler[4].key,"clickhouse_dictionary", 255);

	actx->handler[5].name = clickhouse_merges_handler;
	actx->handler[5].validator = NULL;
	actx->handler[5].mesg_func = clickhouse_merges_mesg;
	strlcpy(actx->handler[5].key,"clickhouse_merges", 255);

	actx->handler[6].name = clickhouse_replicas_handler;
	actx->handler[6].validator = NULL;
	actx->handler[6].mesg_func = clickhouse_replicas_mesg;
	strlcpy(actx->handler[6].key,"clickhouse_replicas", 255);

	tommy_hashdyn_insert(ac->aggregate_ctx, &(actx->node), actx, tommy_strhash_u32(0, actx->key));
}
