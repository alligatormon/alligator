#pragma once
#include <jansson.h>
#include "dstructures/tommy.h"
#include "dstructures/maglev.h"
#include "common/selector.h"
#define CLUSER_TYPE_OPLOG 0
#define CLUSER_TYPE_SHAREDLOCK 1

typedef struct oplog_node
{
	string *data;
	int64_t ttl;
} oplog_node;

typedef struct oplog_record
{
	oplog_node *container;
	uint64_t size;
	uint64_t begin;
	uint64_t end;
} oplog_record;

typedef struct cluster_server_oplog
{
	uint8_t is_me;
	oplog_record *oprec;
	uint64_t index;
	char *name;
} cluster_server_oplog;

typedef struct cluster_node
{
	char *name;
	uint64_t replica_factor;
	uint64_t timeout;
	uint64_t update_count;

	uint64_t servers_size;
	cluster_server_oplog *servers;
	maglev_lookup_hash m_maglev_hash;

	uint64_t sharding_key_size;
	char **sharding_key;

	uint64_t size;

	uint8_t type;
	string *shared_lock_instance;
	uint64_t ttl;
	uint64_t shared_lock_set_time;
	uint8_t parser_status;

	tommy_node node;
} cluster_node;

cluster_node* cluster_get(char *name);
int cluster_compare(const void* arg, const void* obj);
char* server_get_server_offset(char *replica, char *name, uint64_t offset, uint64_t *size, uint64_t *nowoffset);
oplog_node* oplog_record_insert(oplog_record *oplog, string *data);
oplog_record *oplog_record_init(uint64_t size);
string* oplog_record_get_string(oplog_record *oplog);
string* oplog_record_shift_string(oplog_record *oplog);
string* cluster_get_server_data(char *replica, char *name);
string* cluster_shift_server_data(char *replica, char *name);
string* cluster_get_sharedlock(char *name);
string* cluster_set_sharedlock(char *replica, char *name, void *data);
void cluster_push_json(json_t *cluster);
void cluster_del_json(json_t *cluster);
void oplog_record_free(oplog_record *oplog);
void cluster_del_all();
void cluster_handler_stop();
void cluster_handler();
