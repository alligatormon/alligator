#include <stdio.h>
#include <inttypes.h>
#include "common/selector.h"
#include "metric/namespace.h"
#include "events/context_arg.h"
#include "common/aggregator.h"
#include "common/http.h"
#include "common/selector.h"
#include "common/validator.h"
#include "common/logs.h"
#include <query/type.h>
#include "main.h"
#include "metric/metric_types.h"
#define d64	PRId64
#define CH_NAME_SIZE 64
#define CLICKHOUSE_ASYNC_METRIC_SIZE 1024
#define CH_OTHER 0
#define CH_FLOAT 1

typedef struct ch_columns_types {
	char *colname;
	uint8_t type;
} ch_columns_types;

typedef struct ch_table_kv {
	tommy_node node;
	char *key;
	char database[64];
	char table[64];
	int64_t marks_bytes;
	int64_t data_compressed_bytes;
	int64_t data_uncompressed_bytes;
} ch_table_kv;

int ch_table_kv_compare(const void* arg, const void* obj)
{
	char *s1 = (char*)arg;
	char *s2 = ((ch_table_kv*)obj)->key;
	return strcmp(s1, s2);
}

void table_struct_free(void *funcarg, void* arg)
{
	ch_table_kv *chtable = arg;
	context_arg *carg = funcarg;
	namespace_metric_family_set(NULL, carg, "clickhouse_table_data_compressed_bytes", METRIC_TYPE_GAUGE, "ClickHouse table compressed data size in bytes.");
	namespace_metric_family_set(NULL, carg, "clickhouse_table_data_uncompressed_bytes", METRIC_TYPE_GAUGE, "ClickHouse table uncompressed data size in bytes.");
	namespace_metric_family_set(NULL, carg, "clickhouse_table_marks_bytes", METRIC_TYPE_GAUGE, "ClickHouse table marks size in bytes.");
	metric_add_labels2("clickhouse_table_data_compressed_bytes", &chtable->data_compressed_bytes, DATATYPE_INT, carg, "database", chtable->database, "table", chtable->table);
	metric_add_labels2("clickhouse_table_data_uncompressed_bytes", &chtable->data_uncompressed_bytes, DATATYPE_INT, carg, "database", chtable->database, "table", chtable->table);
	metric_add_labels2("clickhouse_table_marks_bytes", &chtable->marks_bytes, DATATYPE_INT, carg, "database", chtable->database, "table", chtable->table);

	free(chtable->key);
	free(chtable);
}

void clickhouse_system_handler(char *metrics, size_t size, context_arg *carg)
{
	string *str = string_new();
	char metric_name[CLICKHOUSE_ASYNC_METRIC_SIZE];
	strlcpy(metric_name, "clickhouse_metrics_", 20);
	for (uint64_t i = 0; i < size; )
	{
		uint64_t len = strcspn(metrics + i, "\r\n");
		if (!len)
			break;

		string_copy(str, metrics + i, len);

		double value;

		size_t metric_name_size = strcspn_n(str->s, " \t", CLICKHOUSE_ASYNC_METRIC_SIZE-21);
		strlcpy(metric_name + 19, str->s, metric_name_size+1);
		if (!prometheus_metric_name_normalizer(metric_name, metric_name_size + 20)) {
			i += len;
			i += strspn(metrics + i, "\r\n");
			continue;
		}

		metric_name_size += strspn(str->s + metric_name_size, " \t");
		value = strtod(str->s + metric_name_size, NULL);
		carglog(carg, L_DEBUG, "Clickhouse system metric from string: '%s', metric_name: '%s', value: '%lf'\n", str->s, metric_name, value);
		namespace_metric_family_set(NULL, carg, metric_name, METRIC_TYPE_GAUGE, "ClickHouse system.metrics value.");
		metric_add_auto(metric_name, &value, DATATYPE_DOUBLE, carg);

		i += len;
		i += strspn(metrics + i, "\r\n");
	}
	string_free(str);
	carg->parser_status = 1;
}


void clickhouse_system_asynchronous_handler(char *metrics, size_t size, context_arg *carg)
{
	string *str = string_new();
	char metric_name[CLICKHOUSE_ASYNC_METRIC_SIZE];
	strlcpy(metric_name, "clickhouse_async_metrics_", 26);
	for (uint64_t i = 0; i < size; )
	{
		uint64_t len = strcspn(metrics + i, "\r\n");
		if (!len)
			break;
		if (!strncmp(metrics+i, "OS", 2)) {
			i += len;
			i += strspn(metrics + i, "\r\n");
			continue;
		}
		if (!strncmp(metrics+i, "Filesystem", 10)) {
			i += len;
			i += strspn(metrics + i, "\r\n");
			continue;
		}
		if (!strncmp(metrics + i, "CPU", 3)) {
			i += len;
			i += strspn(metrics + i, "\r\n");
			continue;
		}
		if (!strncmp(metrics + i, "Disk", 4)) {
			i += len;
			i += strspn(metrics + i, "\r\n");
			continue;
		}
		if (!strncmp(metrics + i, "Block", 5)) {
			i += len;
			i += strspn(metrics + i, "\r\n");
			continue;
		}
		if (!strncmp(metrics + i, "Network", 7)) {
			i += len;
			i += strspn(metrics + i, "\r\n");
			continue;
		}
		if (!strncmp(metrics + i, "PSI", 3)) {
			i += len;
			i += strspn(metrics + i, "\r\n");
			continue;
		}
		if (!strncmp(metrics + i, "CGroup", 6)) {
			i += len;
			i += strspn(metrics + i, "\r\n");
			continue;
		}
		if (!strncmp(metrics + i, "Memory", 6)) {
			i += len;
			i += strspn(metrics + i, "\r\n");
			continue;
		}
		if (!strncmp(metrics + i, "LoadAverage", 11)) {
			i += len;
			i += strspn(metrics + i, "\r\n");
			continue;
		}

		string_copy(str, metrics + i, len);

		double value;

		size_t metric_name_size = strcspn_n(str->s, " \t", CLICKHOUSE_ASYNC_METRIC_SIZE-27);
		strlcpy(metric_name + 25, str->s, metric_name_size+1);
		if (!prometheus_metric_name_normalizer(metric_name, metric_name_size + 26)) {
			i += len;
			i += strspn(metrics + i, "\r\n");
			continue;
		}

		metric_name_size += strspn(str->s + metric_name_size, " \t");
		value = strtod(str->s + metric_name_size, NULL);
		carglog(carg, L_DEBUG, "Clickhouse asynchronous metric from string: '%s', metric_name: '%s', size: '%zu', value: '%lf'\n", str->s, metric_name, metric_name_size, value);
		namespace_metric_family_set(NULL, carg, metric_name, METRIC_TYPE_GAUGE, "ClickHouse system.asynchronous_metrics value.");
		metric_add_auto(metric_name, &value, DATATYPE_DOUBLE, carg);

		i += len;
		i += strspn(metrics + i, "\r\n");
	}

	string_free(str);
	carg->parser_status = 1;
}

void clickhouse_system_events_handler(char *metrics, size_t size, context_arg *carg)
{
	string *str = string_new();
	char metric_name[CLICKHOUSE_ASYNC_METRIC_SIZE];
	const char *prefix = "clickhouse_profile_events_";
	size_t prefix_len = 26;

	strlcpy(metric_name, prefix, prefix_len + 1);
	for (uint64_t i = 0; i < size; )
	{
		uint64_t len = strcspn(metrics + i, "\r\n");
		if (!len)
			break;

		string_copy(str, metrics + i, len);

		size_t name_len = strcspn_n(str->s, " \t", CLICKHOUSE_ASYNC_METRIC_SIZE - prefix_len - 8);
		strlcpy(metric_name + prefix_len, str->s, name_len + 1);
		if (!prometheus_metric_name_normalizer(metric_name, prefix_len + name_len)) {
			i += len;
			i += strspn(metrics + i, "\r\n");
			continue;
		}
		strlcpy(metric_name + prefix_len + name_len, "_total", 7);

		size_t value_off = name_len + strspn(str->s + name_len, " \t");
		double value = strtod(str->s + value_off, NULL);
		namespace_metric_family_set(NULL, carg, metric_name, METRIC_TYPE_COUNTER, "ClickHouse system.events counter.");
		metric_add_auto(metric_name, &value, DATATYPE_DOUBLE, carg);

		i += len;
		i += strspn(metrics + i, "\r\n");
	}
	string_free(str);
	carg->parser_status = 1;
}

void clickhouse_columns_handler(char *metrics, size_t size, context_arg *carg)
{
	namespace_metric_family_set(NULL, carg, "clickhouse_column_data_compressed_bytes", METRIC_TYPE_GAUGE, "ClickHouse per-column compressed data size in bytes.");
	namespace_metric_family_set(NULL, carg, "clickhouse_column_data_uncompressed_bytes", METRIC_TYPE_GAUGE, "ClickHouse per-column uncompressed data size in bytes.");
	namespace_metric_family_set(NULL, carg, "clickhouse_column_marks_bytes", METRIC_TYPE_GAUGE, "ClickHouse per-column marks size in bytes.");

	int64_t i = 0;
	char *database = malloc(CH_NAME_SIZE);
	char *table = malloc(CH_NAME_SIZE);
	char *column = malloc(CH_NAME_SIZE);
	char key[128];
	size_t name_size;
	int64_t cur;
	int64_t data_compressed_bytes,data_uncompressed_bytes,marks_bytes;
	alligator_ht *table_stats = alligator_ht_init(NULL);
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

		metric_add_labels3("clickhouse_column_data_compressed_bytes", &data_compressed_bytes, DATATYPE_INT, carg, "database", database, "table", table, "column", column);
		metric_add_labels3("clickhouse_column_data_uncompressed_bytes", &data_uncompressed_bytes, DATATYPE_INT, carg, "database", database, "table", table, "column", column);
		metric_add_labels3("clickhouse_column_marks_bytes", &marks_bytes, DATATYPE_INT, carg, "database", database, "table", table, "column", column);

		snprintf(key, 128, "%s:%s", database, table);
		uint32_t key_hash = tommy_strhash_u32(0, key);
		ch_table_kv *chtable = alligator_ht_search(table_stats, ch_table_kv_compare, key, key_hash);
		if (!chtable)
		{
			chtable = calloc(1, sizeof(*chtable));
			chtable->key = strdup(key);
			chtable->data_compressed_bytes = data_compressed_bytes;
			chtable->data_uncompressed_bytes = data_uncompressed_bytes;
			chtable->marks_bytes = marks_bytes;
			strlcpy(chtable->database, database, CH_NAME_SIZE);
			strlcpy(chtable->table, table, CH_NAME_SIZE);
			alligator_ht_insert(table_stats, &(chtable->node), chtable, key_hash);
		}
		else
		{
			chtable->data_compressed_bytes += data_compressed_bytes;
			chtable->data_uncompressed_bytes += data_uncompressed_bytes;
			chtable->marks_bytes += marks_bytes;
		}
	}

	alligator_ht_foreach_arg(table_stats, table_struct_free, NULL);
	alligator_ht_done(table_stats);
	free(table_stats);
	free(database);
	free(column);
	free(table);
	carg->parser_status = 1;
}

void clickhouse_merges_handler(char *metrics, size_t size, context_arg *carg)
{
	namespace_metric_family_set(NULL, carg, "clickhouse_merge_progress", METRIC_TYPE_GAUGE, "ClickHouse merge/mutation progress ratio.");
	namespace_metric_family_set(NULL, carg, "clickhouse_merge_parts", METRIC_TYPE_GAUGE, "ClickHouse merge/mutation parts count.");
	namespace_metric_family_set(NULL, carg, "clickhouse_merge_size_compressed_bytes", METRIC_TYPE_GAUGE, "ClickHouse merge/mutation compressed size in bytes.");
	namespace_metric_family_set(NULL, carg, "clickhouse_merge_size_marks", METRIC_TYPE_GAUGE, "ClickHouse merge/mutation marks size.");
	namespace_metric_family_set(NULL, carg, "clickhouse_merge_read_uncompressed_bytes", METRIC_TYPE_GAUGE, "ClickHouse merge/mutation uncompressed bytes read.");
	namespace_metric_family_set(NULL, carg, "clickhouse_merge_rows_read", METRIC_TYPE_GAUGE, "ClickHouse merge/mutation rows read.");
	namespace_metric_family_set(NULL, carg, "clickhouse_merge_written_uncompressed_bytes", METRIC_TYPE_GAUGE, "ClickHouse merge/mutation uncompressed bytes written.");
	namespace_metric_family_set(NULL, carg, "clickhouse_merge_rows_written", METRIC_TYPE_GAUGE, "ClickHouse merge/mutation rows written.");

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

		metric_add_labels3("clickhouse_merge_progress", &progress, DATATYPE_DOUBLE, carg, "database", database, "table", table, "mutation", is_mutation);
		metric_add_labels3("clickhouse_merge_parts", &num_parts, DATATYPE_INT, carg, "database", database, "table", table, "mutation", is_mutation);
		metric_add_labels3("clickhouse_merge_size_compressed_bytes", &total_size_bytes_compressed, DATATYPE_INT, carg, "database", database, "table", table, "mutation", is_mutation);
		metric_add_labels3("clickhouse_merge_size_marks", &total_size_marks, DATATYPE_INT, carg, "database", database, "table", table, "mutation", is_mutation);
		metric_add_labels3("clickhouse_merge_read_uncompressed_bytes", &bytes_read_uncompressed, DATATYPE_INT, carg, "database", database, "table", table, "mutation", is_mutation);
		metric_add_labels3("clickhouse_merge_rows_read", &rows_read, DATATYPE_INT, carg, "database", database, "table", table, "mutation", is_mutation);
		metric_add_labels3("clickhouse_merge_written_uncompressed_bytes", &bytes_written_uncompressed, DATATYPE_INT, carg, "database", database, "table", table, "mutation", is_mutation);
		metric_add_labels3("clickhouse_merge_rows_written", &rows_written, DATATYPE_INT, carg, "database", database, "table", table, "mutation", is_mutation);
	}
	free(database);
	free(table);
	free(is_mutation);
	carg->parser_status = 1;
}

void clickhouse_dictionary_handler(char *metrics, size_t size, context_arg *carg)
{
	namespace_metric_family_set(NULL, carg, "clickhouse_dictionary_queries_total", METRIC_TYPE_COUNTER, "ClickHouse dictionary query count.");
	namespace_metric_family_set(NULL, carg, "clickhouse_dictionary_elements", METRIC_TYPE_GAUGE, "ClickHouse dictionary element count.");
	namespace_metric_family_set(NULL, carg, "clickhouse_dictionary_allocated_bytes", METRIC_TYPE_GAUGE, "ClickHouse dictionary allocated bytes.");
	namespace_metric_family_set(NULL, carg, "clickhouse_dictionary_hit_ratio", METRIC_TYPE_GAUGE, "ClickHouse dictionary hit ratio.");
	namespace_metric_family_set(NULL, carg, "clickhouse_dictionary_load_factor", METRIC_TYPE_GAUGE, "ClickHouse dictionary load factor.");

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

		cur = strcspn(metrics+i, "\t");
		bytes_allocated = atoll(metrics+i);
		cur++;
		i+=cur;

		cur = strcspn(metrics+i, "\t");
		query_count = atoll(metrics+i);
		cur++;
		i+=cur;

		cur = strcspn(metrics+i, "\t");
		hit_rate = atof(metrics+i);
		cur++;
		i+=cur;

		cur = strcspn(metrics+i, "\t");
		element_count = atoll(metrics+i);
		cur++;
		i+=cur;

		cur = strcspn(metrics+i, "\n");
		load_factor = atof(metrics+i);
		cur++;
		i+=cur;

		metric_add_labels("clickhouse_dictionary_queries_total", &query_count, DATATYPE_INT, carg, "name", name);
		metric_add_labels("clickhouse_dictionary_elements", &element_count, DATATYPE_INT, carg, "name", name);
		metric_add_labels("clickhouse_dictionary_allocated_bytes", &bytes_allocated, DATATYPE_INT, carg, "name", name);
		metric_add_labels("clickhouse_dictionary_hit_ratio", &hit_rate, DATATYPE_DOUBLE, carg, "name", name);
		metric_add_labels("clickhouse_dictionary_load_factor", &load_factor, DATATYPE_DOUBLE, carg, "name", name);
	}
	free(name);
	carg->parser_status = 1;
}

void clickhouse_replicas_handler(char *metrics, size_t size, context_arg *carg)
{
	namespace_metric_family_set(NULL, carg, "clickhouse_replica_is_leader", METRIC_TYPE_GAUGE, "1 if this ClickHouse replica is the leader.");
	namespace_metric_family_set(NULL, carg, "clickhouse_replica_readonly", METRIC_TYPE_GAUGE, "1 if this ClickHouse replica is readonly.");
	namespace_metric_family_set(NULL, carg, "clickhouse_replica_future_parts", METRIC_TYPE_GAUGE, "ClickHouse replica future parts.");
	namespace_metric_family_set(NULL, carg, "clickhouse_replica_parts_to_check", METRIC_TYPE_GAUGE, "ClickHouse replica parts to check.");
	namespace_metric_family_set(NULL, carg, "clickhouse_replica_queue_size", METRIC_TYPE_GAUGE, "ClickHouse replica queue size.");
	namespace_metric_family_set(NULL, carg, "clickhouse_replica_inserts_in_queue", METRIC_TYPE_GAUGE, "ClickHouse replica inserts in queue.");
	namespace_metric_family_set(NULL, carg, "clickhouse_replica_merges_in_queue", METRIC_TYPE_GAUGE, "ClickHouse replica merges in queue.");
	namespace_metric_family_set(NULL, carg, "clickhouse_replica_log_max_index", METRIC_TYPE_GAUGE, "ClickHouse replica log max index.");
	namespace_metric_family_set(NULL, carg, "clickhouse_replica_log_pointer", METRIC_TYPE_GAUGE, "ClickHouse replica log pointer.");
	namespace_metric_family_set(NULL, carg, "clickhouse_replicas", METRIC_TYPE_GAUGE, "ClickHouse total replicas.");
	namespace_metric_family_set(NULL, carg, "clickhouse_replica_absolute_delay_seconds", METRIC_TYPE_GAUGE, "ClickHouse replica absolute delay in seconds.");
	namespace_metric_family_set(NULL, carg, "clickhouse_replica_active", METRIC_TYPE_GAUGE, "ClickHouse active replicas.");

	int64_t i = 0;
	char *database = malloc(CH_NAME_SIZE);
	char *table = malloc(CH_NAME_SIZE);
	size_t name_size;
	int64_t cur;
	int64_t leader, readonly, future_parts,parts_to_check,queue_size,inserts_in_queue,merges_in_queue,log_max_index,log_pointer,total_replicas,absolute_delay,active_replicas;
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

		cur = strcspn(metrics+i, "\t");
		absolute_delay = atoll(metrics+i);
		cur++;
		i+=cur;

		cur = strcspn(metrics+i, "\n");
		active_replicas = atoll(metrics+i);
		cur++;
		i+=cur;

		metric_add_labels2("clickhouse_replica_is_leader", &leader, DATATYPE_INT, carg, "database", database, "table", table);
		metric_add_labels2("clickhouse_replica_readonly", &readonly, DATATYPE_INT, carg, "database", database, "table", table);
		metric_add_labels2("clickhouse_replica_future_parts", &future_parts, DATATYPE_INT, carg, "database", database, "table", table);
		metric_add_labels2("clickhouse_replica_parts_to_check", &parts_to_check, DATATYPE_INT, carg, "database", database, "table", table);
		metric_add_labels2("clickhouse_replica_queue_size", &queue_size, DATATYPE_INT, carg, "database", database, "table", table);
		metric_add_labels2("clickhouse_replica_inserts_in_queue", &inserts_in_queue, DATATYPE_INT, carg, "database", database, "table", table);
		metric_add_labels2("clickhouse_replica_merges_in_queue", &merges_in_queue, DATATYPE_INT, carg, "database", database, "table", table);
		metric_add_labels2("clickhouse_replica_log_max_index", &log_max_index, DATATYPE_INT, carg, "database", database, "table", table);
		metric_add_labels2("clickhouse_replica_log_pointer", &log_pointer, DATATYPE_INT, carg, "database", database, "table", table);
		metric_add_labels2("clickhouse_replicas", &total_replicas, DATATYPE_INT, carg, "database", database, "table", table);
		metric_add_labels2("clickhouse_replica_absolute_delay_seconds", &absolute_delay, DATATYPE_INT, carg, "database", database, "table", table);
		metric_add_labels2("clickhouse_replica_active", &active_replicas, DATATYPE_INT, carg, "database", database, "table", table);
	}
	free(database);
	free(table);
	carg->parser_status = 1;
}

void ch_columns_types_free(ch_columns_types *column_types, uint64_t size)
{
	for (uint64_t i = 0; i < size; i++)
		free(column_types[i].colname);

	free(column_types);
}

void clickhouse_response_catch(char *metrics, size_t size, context_arg *carg)
{
	if (1)
	{
		char X_ClickHouse_Summary[255];
		*X_ClickHouse_Summary = 0;
		char *cur = strstr(carg->full_body->s, "X-ClickHouse-Summary:" );
		if (cur)
		{
			cur += strcspn(cur, ":");
			cur += strspn(cur, ": \t");
			size_t len = strcspn(cur, "\r\n");
			size_t copy_len = len + 1;
			if (copy_len > sizeof(X_ClickHouse_Summary))
				copy_len = sizeof(X_ClickHouse_Summary);
			strlcpy(X_ClickHouse_Summary, cur, copy_len);
		}

		char X_ClickHouse_Exception_Code[255];
		*X_ClickHouse_Exception_Code = 0;
		cur = strstr(carg->full_body->s, "X-ClickHouse-Exception-Code:" );
		if (cur)
		{
			cur += strcspn(cur, ":");
			cur += strspn(cur, ": \t");
			size_t len = strcspn(cur, "\r\n");
			size_t copy_len = len + 1;
			if (copy_len > sizeof(X_ClickHouse_Exception_Code))
				copy_len = sizeof(X_ClickHouse_Exception_Code);
			strlcpy(X_ClickHouse_Exception_Code, cur, copy_len);
		}

		carglog(carg, L_DEBUG, "clickhouse summary %s\n", X_ClickHouse_Summary);
		carglog(carg, L_DEBUG, "clickhouse exception code %s\n", X_ClickHouse_Exception_Code);
		carglog(carg, L_DEBUG, "clickhouse body %s\n", metrics);
	}
}

void clickhouse_custom_execute_handler(char *metrics, size_t size, context_arg *carg)
{
	if (!strncmp(metrics, "Code:", 5)) {
		carglog(carg, L_ERROR, "clickhouse_custom_execute_handler error %zu:\n%s\n", size, metrics);
		return;
	}
	uint64_t cur = 0;
	char ch_field[CH_NAME_SIZE];

	uint8_t format_names = 1;
	uint8_t format_type = 0;
	query_node *qn = carg->data;
	//char *colname = NULL;
	uint64_t columns_count = selector_count_field(metrics, "\t\n", strcspn(metrics, "\n"));
	ch_columns_types *column_types = calloc(columns_count, sizeof(*column_types));

	while (cur < size) {
		alligator_ht *hash = alligator_ht_init(NULL);

		if (carg->ns)
			labels_hash_insert_nocache(hash, "dbname", carg->ns);

		uint64_t end = strcspn(metrics + cur, "\n") + cur;
		for (uint64_t i = 0; cur < end; i++) {
			uint64_t word_end = strcspn(metrics + cur, "\n\t");
			strlcpy(ch_field, metrics + cur, word_end + 1);

			cur += word_end + 1;

			//printf("ch: %s\n", ch_field);
			//printf("2 %d < %d\n", cur, end);

			if (format_names)
				column_types[i].colname = strdup(ch_field);
			else if (format_type)
			{
				if (!strncasecmp(ch_field, "Float", 5))
					column_types[i].type = CH_FLOAT;
			}
			else if (column_types[i].type == CH_FLOAT) // Float32 Float64
			{
				query_field *qf = query_field_get(qn->qf_hash, column_types[i].colname);
				if (qf)
				{
					carglog(carg, L_DEBUG, "clickhouse: query field value='%s'\n", ch_field);
					qf->d = strtod(ch_field, NULL);
					qf->type = DATATYPE_DOUBLE;
				}
				else
				{
					carglog(carg, L_DEBUG, "clickhouse: label field '%s'='%s'\n", column_types[i].colname, ch_field);
					labels_hash_insert_nocache(hash, column_types[i].colname, ch_field);
				}
			}
			else
			{
				query_field *qf = query_field_get(qn->qf_hash, column_types[i].colname);
				if (qf)
				{
					carglog(carg, L_DEBUG, "clickhouse: query field value='%s'\n", ch_field);
					qf->i = strtoll(ch_field, NULL, 10);
					qf->type = DATATYPE_INT;
				}
				else
				{
					carglog(carg, L_DEBUG, "clickhouse: label field '%s'='%s'\n", column_types[i].colname, ch_field);
					labels_hash_insert_nocache(hash, column_types[i].colname, ch_field);
				}
			}
		}

		//printf("1 %d < %d: %d:%d\n", cur, size, format_names, format_type);

		if (format_names)
		{
			format_names = 0;
			format_type = 1;
		}
		else if (format_type)
		{
			format_type = 0;
		}
		else
		{
			qn->labels = hash;
			qn->carg = carg;
			query_set_values(qn);
			labels_hash_free(hash);
			qn->labels = NULL;
		}
	}
	ch_columns_types_free(column_types, columns_count);
	carg->parser_status = 1;
}

void clickhouse_queries_foreach(void *funcarg, void* arg)
{
	context_arg *carg = (context_arg*)funcarg;
	query_node *qn = arg;
	carglog(carg, L_DEBUG, "run datasource '%s', make '%s': '%s'\n", qn->datasource, qn->make, qn->expr);
	char *key = malloc(255);
	snprintf(key, 255, "(tcp://%s:%u)/custom", carg->host, htons(carg->remote_addr.sin_port));

	string *query = string_init(1024);
	string_cat(query, qn->expr, strlen(qn->expr));
	string_cat(query, " FORMAT TabSeparatedWithNamesAndTypes", 37);

	char cl[20];
	snprintf(cl, 19, "%zu", query->l);
	env_struct_push_alloc(carg->env, "Content-Length", cl);
	char *generated_query = gen_http_query(HTTP_POST, carg->query_url, NULL, carg->host, "alligator", NULL, "1.0", carg->env, NULL, query);
	string_free(query);

	//printf("try again %s/%"u64"\n", generated_query, strlen(generated_query));
	try_again(carg, generated_query, strlen(generated_query), clickhouse_custom_execute_handler, "clickhouse_custom", NULL, key, qn);
}

void clickhouse_custom_handler(char *metrics, size_t size, context_arg *carg)
{
	if (carg->name)
	{
		query_ds *qds = query_get(carg->name);
		carglog(carg, L_DEBUG, "found queries for datasource: %s\n", carg->name);
		if (qds)
		{
			alligator_ht_foreach_arg(qds->hash, clickhouse_queries_foreach, carg);
		}
	}

	//if (!carg->data_lock)
	//	postgresql_get_databases(conn, carg);
}

string *clickhouse_gen_url(host_aggregator_info *hi, char *addition, void *env, void *proxy_settings)
{
	return string_init_add_auto(gen_http_query(0, hi->query, addition, hi->host, "alligator", hi->auth, "1.0", env, proxy_settings, NULL));
}

string* clickhouse_system_mesg(host_aggregator_info *hi, void *arg, void *env, void *proxy_settings) { return clickhouse_gen_url(hi, "/?query=select%20metric,value\%20from\%20system.metrics", env, proxy_settings); }
string* clickhouse_system_asynchronous_mesg(host_aggregator_info *hi, void *arg, void *env, void *proxy_settings) { return clickhouse_gen_url(hi, "/?query=select%20metric,value\%20from\%20system.asynchronous_metrics", env, proxy_settings); }
string* clickhouse_system_events_mesg(host_aggregator_info *hi, void *arg, void *env, void *proxy_settings) { return clickhouse_gen_url(hi, "/?query=select%20event,value%20from\%20system.events", env, proxy_settings); }
string* clickhouse_columns_mesg(host_aggregator_info *hi, void *arg, void *env, void *proxy_settings) { return clickhouse_gen_url(hi, "/?query=select%20database,table,name,data_compressed_bytes,data_uncompressed_bytes,marks_bytes%20from\%20system.columns%20where%20database!=%27system%27", env, proxy_settings); }
string* clickhouse_dictionary_mesg(host_aggregator_info *hi, void *arg, void *env, void *proxy_settings) { return clickhouse_gen_url(hi, "/?query=select%20name,bytes_allocated,query_count,hit_rate,element_count,load_factor%20from\%20system.dictionaries", env, proxy_settings); }
string* clickhouse_merges_mesg(host_aggregator_info *hi, void *arg, void *env, void *proxy_settings) { return clickhouse_gen_url(hi, "/?query=select%20database,is_mutation,table,progress,num_parts,total_size_bytes_compressed,total_size_marks,bytes_read_uncompressed,rows_read,bytes_written_uncompressed,rows_written%20from\%20system.merges", env, proxy_settings); }
string* clickhouse_replicas_mesg(host_aggregator_info *hi, void *arg, void *env, void *proxy_settings) { return clickhouse_gen_url(hi, "/?query=select%20database,table,is_leader,is_readonly,future_parts,parts_to_check,queue_size,inserts_in_queue,merges_in_queue,log_max_index,log_pointer,total_replicas,absolute_delay,active_replicas%20from\%20system.replicas", env, proxy_settings); }
string* clickhouse_custom_mesg(host_aggregator_info *hi, void *arg, void *env, void *proxy_settings) { return clickhouse_gen_url(hi, "/", env, proxy_settings); }

void clickhouse_parser_push()
{
	aggregate_context *actx = calloc(1, sizeof(*actx));

	actx->key = strdup("clickhouse");
	actx->handlers = 8;
	actx->handler = calloc(1, sizeof(*actx->handler)*actx->handlers);

	actx->handler[0].name = clickhouse_system_handler;
	actx->handler[0].validator = NULL;
	actx->handler[0].mesg_func = clickhouse_system_mesg;
	strlcpy(actx->handler[0].key,"clickhouse_system", 255);

	actx->handler[1].name = clickhouse_system_asynchronous_handler;
	actx->handler[1].validator = NULL;
	actx->handler[1].mesg_func = clickhouse_system_asynchronous_mesg;
	strlcpy(actx->handler[1].key,"clickhouse_system_asynchronous", 255);

	actx->handler[2].name = clickhouse_system_events_handler;
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

	actx->handler[7].name = clickhouse_custom_handler;
	actx->handler[7].validator = NULL;
	actx->handler[7].mesg_func = clickhouse_custom_mesg;
	strlcpy(actx->handler[7].key,"clickhouse_custom", 255);

	alligator_ht_insert(ac->aggregate_ctx, &(actx->node), actx, tommy_strhash_u32(0, actx->key));
}
