#include "main.h"
#include "events/context_arg.h"
#include "common/rtime.h"

void aggregator_events_metric_add(context_arg *srv_carg, context_arg *carg, char *key, char *proto, char *type, char *host)
{
	if (!key)
		key = carg->key;

	metric_add_labels4("alligator_connect", &srv_carg->conn_counter, DATATYPE_UINT, carg, "key", key, "proto", proto, "type", type, "host", host);
	metric_add_labels4("alligator_read", &srv_carg->read_counter, DATATYPE_UINT, carg, "key", key, "proto", proto, "type", type, "host", host);
	metric_add_labels4("alligator_write", &srv_carg->write_counter, DATATYPE_UINT, carg, "key", key, "proto", proto, "type", type, "host", host);
	metric_add_labels4("alligator_timeout", &srv_carg->timeout_counter, DATATYPE_UINT, carg, "key", key, "proto", proto, "type", type, "host", host);
	metric_add_labels4("alligator_tls_write", &srv_carg->tls_write_counter, DATATYPE_UINT, carg, "key", key, "proto", proto, "type", type, "host", host);
	metric_add_labels4("alligator_tls_read", &srv_carg->tls_read_counter, DATATYPE_UINT, carg, "key", key, "proto", proto, "type", type, "host", host);
	metric_add_labels4("alligator_tls_init", &srv_carg->tls_init_counter, DATATYPE_UINT, carg, "key", key, "proto", proto, "type", type, "host", host);
	metric_add_labels4("alligator_close", &srv_carg->close_counter, DATATYPE_UINT, carg, "key", key, "proto", proto, "type", type, "host", host);
	metric_add_labels4("alligator_shutdown", &srv_carg->shutdown_counter, DATATYPE_UINT, carg, "key", key, "proto", proto, "type", type, "host", host);
	metric_add_labels4("alligator_write_bytes", &srv_carg->write_bytes_counter, DATATYPE_UINT, carg, "key", key, "proto", proto, "type", type, "host", host);
	metric_add_labels4("alligator_read_bytes", &srv_carg->read_bytes_counter, DATATYPE_UINT, carg, "key", key, "proto", proto, "type", type, "host", host);
	metric_add_labels4("alligator_tls_write_bytes", &srv_carg->tls_write_bytes_counter, DATATYPE_UINT, carg, "key", key, "proto", proto, "type", type, "host", host);
	metric_add_labels4("alligator_tls_read_bytes", &srv_carg->tls_read_bytes_counter, DATATYPE_UINT, carg, "key", key, "proto", proto, "type", type, "host", host);

	uint64_t connect_time = getrtime_mcs(carg->connect_time, carg->connect_time_finish, 0);
	srv_carg->connect_time_counter += connect_time;
	uint64_t tls_connect_time = getrtime_mcs(carg->tls_connect_time, carg->tls_connect_time_finish, 0);
	srv_carg->tls_connect_time_counter += tls_connect_time;
	uint64_t write_time = getrtime_mcs(carg->write_time, carg->write_time_finish, 0);
	srv_carg->write_time_counter += write_time;
	uint64_t read_time = getrtime_mcs(carg->read_time, carg->read_time_finish, 0);
	srv_carg->read_time_counter += read_time;
	uint64_t tls_read_time = getrtime_mcs(carg->tls_read_time, carg->tls_read_time_finish, 0);
	srv_carg->tls_read_time_counter += tls_read_time;
	uint64_t tls_write_time = getrtime_mcs(carg->tls_write_time, carg->tls_write_time_finish, 0);
	srv_carg->tls_write_time_counter += tls_write_time;
	uint64_t shutdown_time = getrtime_mcs(carg->shutdown_time, carg->shutdown_time_finish, 0);
	srv_carg->shutdown_time_counter += shutdown_time;
	uint64_t close_time = getrtime_mcs(carg->close_time, carg->close_time_finish, 0);
	srv_carg->close_time_counter += close_time;
	uint64_t exec_time = getrtime_mcs(carg->exec_time, carg->exec_time_finish, 0);
	srv_carg->exec_time_counter += exec_time;

	metric_add_labels4("alligator_connect_time", &connect_time, DATATYPE_UINT, carg, "key", key, "proto", proto, "type", type, "host", host);
	metric_add_labels4("alligator_tls_connect_time", &tls_connect_time, DATATYPE_UINT, carg, "key", key, "proto", proto, "type", type, "host", host);
	metric_add_labels4("alligator_write_time", &write_time, DATATYPE_UINT, carg, "key", key, "proto", proto, "type", type, "host", host);
	metric_add_labels4("alligator_tls_write_time", &tls_write_time, DATATYPE_UINT, carg, "key", key, "proto", proto, "type", type, "host", host);
	metric_add_labels4("alligator_read_time", &read_time, DATATYPE_UINT, carg, "key", key, "proto", proto, "type", type, "host", host);
	metric_add_labels4("alligator_tls_read_time", &tls_read_time, DATATYPE_UINT, carg, "key", key, "proto", proto, "type", type, "host", host);
	metric_add_labels4("alligator_execute_time", &exec_time, DATATYPE_UINT, carg, "key", key, "proto", proto, "type", type, "host", host);

	metric_add_labels4("alligator_connect_total_time", &srv_carg->connect_time_counter, DATATYPE_UINT, carg, "key", key, "proto", proto, "type", type, "host", host);
	metric_add_labels4("alligator_tls_connect_total_time", &srv_carg->tls_connect_time_counter, DATATYPE_UINT, carg, "key", key, "proto", proto, "type", type, "host", host);
	metric_add_labels4("alligator_write_total_time", &srv_carg->write_time_counter, DATATYPE_UINT, carg, "key", key, "proto", proto, "type", type, "host", host);
	metric_add_labels4("alligator_tls_write_total_time", &srv_carg->tls_write_time_counter, DATATYPE_UINT, carg, "key", key, "proto", proto, "type", type, "host", host);
	metric_add_labels4("alligator_read_total_time", &srv_carg->read_time_counter, DATATYPE_UINT, carg, "key", key, "proto", proto, "type", type, "host", host);
	metric_add_labels4("alligator_tls_read_total_time", &srv_carg->tls_read_time_counter, DATATYPE_UINT, carg, "key", key, "proto", proto, "type", type, "host", host);
	metric_add_labels4("alligator_execute_total_time", &srv_carg->exec_time_counter, DATATYPE_UINT, carg, "key", key, "proto", proto, "type", type, "host", host);
}
