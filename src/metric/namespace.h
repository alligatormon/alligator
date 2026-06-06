#pragma once
#include "metric/expiretree.h"
#include "metric/metrictree.h"
#include "events/context_arg.h"
#include "action/type.h"
#include "query/promql.h"

#define DATATYPE_LIST_STRING 8
#define DATATYPE_LIST_UINT 7
#define DATATYPE_LIST_INT 6
#define DATATYPE_LIST_DOUBLE 5
#define DATATYPE_STRING 4
#define DATATYPE_UINT 3
#define DATATYPE_INT 2
#define DATATYPE_DOUBLE 1
#define DATATYPE_NONE 0
#define METRIC_STORAGE_BUFFER_DEFAULT 1024
#define METRIC_SERIALIZER_OPENMETRICS 0
#define METRIC_SERIALIZER_JSON 1
#define METRIC_SERIALIZER_DSV 2
#define METRIC_SERIALIZER_GRAPHITE 3
#define METRIC_SERIALIZER_CARBON2 4
#define METRIC_SERIALIZER_INFLUXDB 5
#define METRIC_SERIALIZER_CLICKHOUSE 6
#define METRIC_SERIALIZER_PG 7
#define METRIC_SERIALIZER_ELASTICSEARCH 8
#define METRIC_SERIALIZER_MONGODB 9
#define METRIC_SERIALIZER_CASSANDRA 10
#define METRIC_SERIALIZER_STATSD 11
#define METRIC_SERIALIZER_DOGSTATSD 12
#define METRIC_SERIALIZER_DYNATRACE 13
#define METRIC_SERIALIZER_OTLP 14
#define METRIC_SERIALIZER_OTLP_PROTOBUF 15
#define MAIN_METRIC_NAME "__name__"
#define MAIN_METRIC_LEN 8
#define MAIN_METRIC_HASH 3262809090

typedef struct namespace_struct
{
	tommy_node node;
	char *key;
	uint64_t max_emit;
	pthread_mutex_t *max_emit_lock;
	expire_tree *expiretree;
	metric_tree *metrictree;
	alligator_ht *metric_families;
} namespace_struct;

typedef struct serializer_context {
	int serializer;
	action_node *an;
	char *last_metric; // for SQL-like databases
	json_t *json;
	string *str;
	//string **multistring;
	string_tokens *multistring;
	string *index_name;
	//uint64_t ms_size;
	string *engine;
	char delimiter;
} serializer_context;

typedef struct query_pass {
	action_node *an;
	metric_query_context *mqc;
	char *new_name;
	uint8_t action_need_run;
} query_pass;

void metric_add(char *name, alligator_ht *labels, void* value, int8_t type, context_arg *carg);
void metric_add_auto(char *name, void* value, int8_t type, context_arg *carg);
void metric_add_labels(char *name, void* value, int8_t type, context_arg *carg, char *name1, char *key1);
void metric_add_labels2(char *name, void* value, int8_t type, context_arg *carg, char *name1, char *key1, char *name2, char *key2);
void metric_add_labels3(char *name, void* value, int8_t type, context_arg *carg, char *name1, char *key1, char *name2, char *key2, char *name3, char *key3);
void metric_add_labels4(char *name, void* value, int8_t type, context_arg *carg, char *name1, char *key1, char *name2, char *key2, char *name3, char *key3, char *name4, char *key4);
void metric_add_labels5(char *name, void* value, int8_t type, context_arg *carg, char *name1, char *key1, char *name2, char *key2, char *name3, char *key3, char *name4, char *key4, char *name5, char *key5);
void metric_add_labels6(char *name, void* value, int8_t type, context_arg *carg, char *name1, char *key1, char *name2, char *key2, char *name3, char *key3, char *name4, char *key4, char *name5, char *key5, char *name6, char *key6);
void metric_add_labels7(char *name, void* value, int8_t type, context_arg *carg, char *name1, char *key1, char *name2, char *key2, char *name3, char *key3, char *name4, char *key4, char *name5, char *key5, char *name6, char *key6, char *name7, char *key7);
void metric_add_labels8(char *name, void* value, int8_t type, context_arg *carg, char *name1, char *key1, char *name2, char *key2, char *name3, char *key3, char *name4, char *key4, char *name5, char *key5, char *name6, char *key6, char *name7, char *key7, char *name8, char *key8);
void metric_add_labels9(char *name, void* value, int8_t type, context_arg *carg, char *name1, char *key1, char *name2, char *key2, char *name3, char *key3, char *name4, char *key4, char *name5, char *key5, char *name6, char *key6, char *name7, char *key7, char *name8, char *key8, char* name9, char* key9);
void metric_add_labels10(char *name, void* value, int8_t type, context_arg *carg, char *name1, char *key1, char *name2, char *key2, char *name3, char *key3, char *name4, char *key4, char *name5, char *key5, char *name6, char *key6, char *name7, char *key7, char *name8, char *key8, char* name9, char* key9, char* name10, char* key10);
void metric_update(char *name, alligator_ht *labels, void* value, int8_t type, context_arg *carg);
void metric_update_labels2(char *name, void* value, int8_t type, context_arg *carg, char *name1, char *key1, char *name2, char *key2);
void metric_update_labels3(char *name, void* value, int8_t type, context_arg *carg, char *name1, char *key1, char *name2, char *key2, char *name3, char *key3);
void metric_update_labels7(char *name, void* value, int8_t type, context_arg *carg, char *name1, char *key1, char *name2, char *key2, char *name3, char *key3, char *name4, char *key4, char *name5, char *key5, char *name6, char *key6, char *name7, char *key7);
void metric_gset(metric_node *mnode, int8_t type, void* value, expire_tree *expiretree, int64_t ttl);
labels_t* labels_initiate(namespace_struct *ns, alligator_ht *hash, char *name, char *namespace, namespace_struct *arg_ns, uint8_t no_del);
serializer_context *serializer_init(int serializer, string *str, char delimiter, string *engine, string *index_template, action_node *an);
char* metric_transform_name(char *name, action_node *an);
char *metric_transform_alt_for_include(const char *export_name, const char *tree_metric_key);
void metric_transform_labels(char *metric_name, char *metric_name_alt, alligator_ht *labels, json_t *metricstransform, context_arg *carg, action_node *an);
char* metric_transform_label_value(char *metric_name, char *metric_name_alt, char *label_name, char *label_value, json_t *metricstransform, context_arg *carg, action_node *an);
char *metric_transform_label_key(char *metric_name, char *metric_name_alt, char *label_name, char *label_value, json_t *metricstransform, context_arg *carg, action_node *an);
/* True if a stored label should be omitted because add_label overrides it on the wire (same key after export). False when metricstransform renames the key away from the add_label name so both may appear. */
int metric_serializer_stored_label_superseded_by_add_label(char *metric_name_for_transform, char *metric_transform_alt, labels_t *labels, json_t *metricstransform, action_node *an, alligator_ht *add_labels);
namespace_struct *get_namespace_by_carg(context_arg *carg);
namespace_struct *get_namespace(char *key);
namespace_struct *insert_namespace(char *key, uint64_t max_emit);
namespace_struct *get_namespace_or_null(char *key);
string* namespace_print(char *namespace, namespace_struct *arg_ns);
void free_namespaces();
void namespace_free(char *namespace, namespace_struct *arg_ns);
void metrictree_gen(metric_tree *tree, labels_t* labels, string *groupkey, alligator_ht *hash, size_t labels_count, double opval);
void metric_str_build (char *namespace, string *str, int openmetrics);
void metric_node_serialize(metric_node *x, serializer_context *sc);
void serializer_do(serializer_context *sc, string *str);
void metrictree_serialize_query(metric_tree *tree, labels_t* labels, string *groupkey, serializer_context *sc, size_t labels_count);
void serializer_free(serializer_context *sc);
void expire_purge(uint64_t key, char *namespace, namespace_struct *ns);
void ts_initialize();
void namespace_metric_family_set(char *namespace, context_arg *carg, const char *metric_name, uint8_t type, const char *help);
metric_family_metadata *namespace_metric_family_get(namespace_struct *ns, const char *metric_name);
/* Strip Prometheus histogram suffix (_bucket/_sum/_count); returns 1 if stripped. */
int prom_family_strip_histogram_suffix(const char *metric_name, char *base_out, size_t base_out_size);
/* Strip Prometheus summary suffix (_sum/_count/_quantile); returns 1 if stripped. */
int prom_family_strip_summary_suffix(const char *metric_name, char *base_out, size_t base_out_size);
/* Resolve exposition family name (base for histogram/summary components). */
const char *prom_family_exposition_resolve(namespace_struct *ns, const char *metric_name,
    metric_family_metadata **meta_out, char *buf, size_t buf_size);
void namespace_metric_family_set_prom_type(context_arg *carg, const char *metric_name, uint8_t metric_type);
void namespace_metric_family_set_prom_help(context_arg *carg, const char *metric_name, const char *help);
