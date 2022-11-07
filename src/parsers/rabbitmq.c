#include <stdio.h>
#include <inttypes.h>
#include <jansson.h>
#include "common/selector.h"
#include "metric/namespace.h"
#include "events/context_arg.h"
#include "common/http.h"
#include "main.h"
#define RABBITMQ_LEN 1000

void rabbitmq_overview_handler(char *metrics, size_t size, context_arg *carg)
{
	json_parser_entry(metrics, 0, NULL, "rabbitmq", carg);
	carg->parser_status = 1;
}
void rabbitmq_nodes_handler(char *metrics, size_t size, context_arg *carg)
{
	json_error_t error;
	json_t *root = json_loads(metrics, 0, &error);
	if (!root)
	{
		fprintf(stderr, "json error on line %d: %s\n", error.line, error.text);
		return;
	}

	size_t i;
	size_t node_size = json_array_size(root);

	char node_str_prefix[RABBITMQ_LEN];
	char node_str1[RABBITMQ_LEN];
	char node_str2[RABBITMQ_LEN];
	strlcpy(node_str_prefix, "rabbitmq_nodes_", 16);
	strlcpy(node_str1, "rabbitmq_nodes_", 16);
	strlcpy(node_str2, "rabbitmq_nodes_gc_queue_length_", 32);
	int64_t ival;
	double dval;

	for (i = 0; i < node_size; i++)
	{
		json_t *node_obj = json_array_get(root, i);
		json_t *node_name_json = json_object_get(node_obj, "name");
		char *node_name = (char*)json_string_value(node_name_json);
		//printf("node_name: %s\n", node_name);

		const char *key;
		json_t *value;
		json_object_foreach(node_obj, key, value)
		{
			int node_type = json_typeof(value);
			if (node_type == JSON_INTEGER)
			{
				strlcpy(node_str1+15, key, RABBITMQ_LEN-15);
				ival = json_integer_value(value);
				//printf("%s: %"d64"\n", node_str1, ival);
				metric_add_labels(node_str1, &ival, DATATYPE_INT, carg, "node", node_name);
			}
			else if (node_type == JSON_REAL)
			{
				strlcpy(node_str1+15, key, RABBITMQ_LEN-15);
				dval = json_real_value(value);
				//printf("%s: %lf\n", node_str1, dval);
				metric_add_labels(node_str1, &dval, DATATYPE_DOUBLE, carg, "node", node_name);
			}
			else if (node_type == JSON_TRUE)
			{
				strlcpy(node_str1+15, key, RABBITMQ_LEN-15);
				ival = 1;
				//printf("%s: %"d64"\n", node_str1, ival);
				metric_add_labels(node_str1, &ival, DATATYPE_INT, carg, "node", node_name);
			}
			else if (node_type == JSON_FALSE)
			{
				strlcpy(node_str1+15, key, RABBITMQ_LEN-15);
				ival = 0;
				//printf("%s: %"d64"\n", node_str1, ival);
				metric_add_labels(node_str1, &ival, DATATYPE_INT, carg, "node", node_name);
			}
		}

		json_t *metrics_gc_queue_length_json = json_object_get(node_obj, "metrics_gc_queue_length");
		if (metrics_gc_queue_length_json)
		{
			const char *key_gc;
			json_t *value_gc;
			json_object_foreach(metrics_gc_queue_length_json, key_gc, value_gc)
			{
				int gc_type = json_typeof(value_gc);
				if (gc_type == JSON_INTEGER)
				{
					strlcpy(node_str2+31, key_gc, RABBITMQ_LEN-31);
					ival = json_integer_value(value_gc);
					//printf("%s: %"d64"\n", node_str2, ival);
					metric_add_labels(node_str2, &ival, DATATYPE_INT, carg, "node", node_name);
				}
				else if (gc_type == JSON_INTEGER)
				{
					strlcpy(node_str2+31, key_gc, RABBITMQ_LEN-31);
					dval = json_real_value(value_gc);
					//printf("%s: %lf\n", node_str2, dval);
					metric_add_labels(node_str2, &dval, DATATYPE_DOUBLE, carg, "node", node_name);
				}
			}
		}

		json_t *cluster_links_json = json_object_get(node_obj, "cluster_links");
		if (cluster_links_json)
		{
			size_t cluster_size = json_array_size(cluster_links_json);
			uint64_t j;
			for (j = 0; j < cluster_size; j++)
			{
				json_t *cluster_obj = json_array_get(cluster_links_json, i);

				json_t *cluster_name_json = json_object_get(cluster_obj, "name");
				if (!cluster_name_json)
					continue;
				char *cluster_name = (char*)json_string_value(cluster_name_json);

				json_t *recv_bytes_json = json_object_get(cluster_obj, "recv_bytes");
				int64_t recv_bytes = json_integer_value(recv_bytes_json);

				json_t *send_bytes_json = json_object_get(cluster_obj, "send_bytes");
				int64_t send_bytes = json_integer_value(send_bytes_json);

				//printf("%s: send: %"d64", recv: %"d64"\n", cluster_name, recv_bytes, send_bytes);
				metric_add_labels3("rabbitmq_nodes_cluster_links", &recv_bytes, DATATYPE_INT, carg, "node", node_name, "dst", cluster_name, "type", "recv_bytes");
				metric_add_labels3("rabbitmq_nodes_cluster_links", &send_bytes, DATATYPE_INT, carg, "node", node_name, "dst", cluster_name, "type", "send_bytes");
			}
		}
	}

	json_decref(root);
	carg->parser_status = 1;
}
void rabbitmq_exchanges_handler(char *metrics, size_t size, context_arg *carg)
{
	json_error_t error;
	json_t *root = json_loads(metrics, 0, &error);
	if (!root)
	{
		fprintf(stderr, "json error on line %d: %s\n", error.line, error.text);
		return;
	}

	char string1[RABBITMQ_LEN];
	strlcpy(string1, "rabbitmq_exchanges_", 20);
	int64_t ival;

	size_t i;
	size_t exchanges_size = json_array_size(root);
	for (i=0; i<exchanges_size; i++)
	{
		json_t *exc_obj = json_array_get(root, i);

		json_t *exc_name_json = json_object_get(exc_obj, "name");
		char *exc_name = (char*)json_string_value(exc_name_json);
		if (strlen(exc_name) == 0)
			continue;

		json_t *exc_type_json = json_object_get(exc_obj, "type");
		char *exc_type = (char*)json_string_value(exc_type_json);

		json_t *exc_vhost_json = json_object_get(exc_obj, "vhost");
		char *exc_vhost = (char*)json_string_value(exc_vhost_json);

		const char *key;
		json_t *value;
		json_object_foreach(exc_obj, key, value)
		{
			int node_type = json_typeof(value);
			if (node_type == JSON_TRUE)
			{
				ival = 1;
				strlcpy(string1+19, key, RABBITMQ_LEN-19);
				//printf("%s: %"d64"\n", string1, ival);
				metric_add_labels3(string1, &ival, DATATYPE_INT, carg, "name", exc_name, "type", exc_type, "vhost", exc_vhost);
			}
			else if (node_type == JSON_FALSE)
			{
				ival = 0;
				strlcpy(string1+19, key, RABBITMQ_LEN-19);
				//printf("%s: %"d64"\n", string1, ival);
				metric_add_labels3(string1, &ival, DATATYPE_INT, carg, "name", exc_name, "type", exc_type, "vhost", exc_vhost);
			}
		}


		json_t *message_stats_json = json_object_get(exc_obj, "message_stats");
		if (message_stats_json)
		{
			json_t *publish_in_json = json_object_get(message_stats_json, "publish_in");
			json_t *publish_out_json = json_object_get(message_stats_json, "publish_in");
			int64_t publish_in = json_integer_value(publish_in_json);
			int64_t publish_out = json_integer_value(publish_out_json);
			metric_add_labels3("rabbitmq_exchanges_publish_in", &publish_in, DATATYPE_INT, carg, "name", exc_name, "type", exc_type, "vhost", exc_vhost);
			metric_add_labels3("rabbitmq_exchanges_publish_out", &publish_out, DATATYPE_INT, carg, "name", exc_name, "type", exc_type, "vhost", exc_vhost);
			//printf("rabbitmq_exchanges_publish{in}: %"d64"\n", publish_in);
			//printf("rabbitmq_exchanges_publish{out}: %"d64"\n", publish_out);
		}
	}

	json_decref(root);
	carg->parser_status = 1;
}
void rabbitmq_connections_handler(char *metrics, size_t size, context_arg *carg)
{
	json_error_t error;
	json_t *root = json_loads(metrics, 0, &error);
	if (!root)
	{
		fprintf(stderr, "json error on line %d: %s\n", error.line, error.text);
		return;
	}

	char string1[RABBITMQ_LEN];
	char string2[RABBITMQ_LEN];
	strlcpy(string1, "rabbitmq_connections_", 22);
	strlcpy(string2, "rabbitmq_connections_gc_", 25);
	int64_t ival;
	double dval;

	size_t i;
	size_t connections_size = json_array_size(root);
	for (i=0; i<connections_size; i++)
	{
		json_t *connections_obj = json_array_get(root, i);

		json_t *connections_host_json = json_object_get(connections_obj, "host");
		char *connections_host = (char*)json_string_value(connections_host_json);
		if (!connections_host)
			continue;

		json_t *connections_peer_host_json = json_object_get(connections_obj, "peer_host");
		char *connections_peer_host = (char*)json_string_value(connections_peer_host_json);
		if (!connections_peer_host)
			continue;

		json_t *connections_type_json = json_object_get(connections_obj, "type");
		char *connections_type = (char*)json_string_value(connections_type_json);
		if (!connections_type)
			continue;

		json_t *connections_vhost_json = json_object_get(connections_obj, "vhost");
		char *connections_vhost = (char*)json_string_value(connections_vhost_json);
		if (!connections_vhost)
			continue;

		json_t *connections_user_json = json_object_get(connections_obj, "user");
		char *connections_user = (char*)json_string_value(connections_user_json);
		if (!connections_user)
			continue;

		json_t *connections_protocol_json = json_object_get(connections_obj, "protocol");
		char *connections_protocol = (char*)json_string_value(connections_protocol_json);
		if (!connections_protocol)
			continue;

		json_t *connections_auth_mechanism_json = json_object_get(connections_obj, "auth_mechanism");
		char *connections_auth_mechanism = (char*)json_string_value(connections_auth_mechanism_json);
		if (!connections_auth_mechanism)
			continue;

		json_t *connections_state_json = json_object_get(connections_obj, "state");
		char *connections_state = (char*)json_string_value(connections_state_json);
		if (!connections_state)
			ival = 0;
		else {
			if (!strncmp(connections_state, "running", 7))
				ival = 1;
			else
				ival = 0;
		}
		metric_add_labels7("rabbitmq_connections_state", &ival, DATATYPE_INT, carg, "host", connections_host, "peer", connections_peer_host, "type", connections_type, "vhost", connections_vhost, "user", connections_user, "protocol", connections_protocol, "auth_mechanism", connections_auth_mechanism);

		const char *key;
		json_t *value;
		json_object_foreach(connections_obj, key, value)
		{
			int node_type = json_typeof(value);
			if (node_type == JSON_TRUE)
			{
				ival = 1;
				strlcpy(string1+21, key, RABBITMQ_LEN-21);
				metric_add_labels7(string1, &ival, DATATYPE_INT, carg, "host", connections_host, "peer", connections_peer_host, "type", connections_type, "vhost", connections_vhost, "user", connections_user, "protocol", connections_protocol, "auth_mechanism", connections_auth_mechanism);
			}
			else if (node_type == JSON_FALSE)
			{
				ival = 0;
				strlcpy(string1+21, key, RABBITMQ_LEN-21);
				metric_add_labels7(string1, &ival, DATATYPE_INT, carg, "host", connections_host, "peer", connections_peer_host, "type", connections_type, "vhost", connections_vhost, "user", connections_user, "protocol", connections_protocol, "auth_mechanism", connections_auth_mechanism);
			}
			else if (node_type == JSON_REAL)
			{
				dval = json_real_value(value);
				strlcpy(string1+21, key, RABBITMQ_LEN-21);
				metric_add_labels7(string1, &dval, DATATYPE_DOUBLE, carg, "host", connections_host, "peer", connections_peer_host, "type", connections_type, "vhost", connections_vhost, "user", connections_user, "protocol", connections_protocol, "auth_mechanism", connections_auth_mechanism);
			}
			else if (node_type == JSON_INTEGER)
			{
				ival = json_integer_value(value);
				strlcpy(string1+21, key, RABBITMQ_LEN-21);
				metric_add_labels7(string1, &ival, DATATYPE_INT, carg, "host", connections_host, "peer", connections_peer_host, "type", connections_type, "vhost", connections_vhost, "user", connections_user, "protocol", connections_protocol, "auth_mechanism", connections_auth_mechanism);
			}
		}

		json_t *garbage_collection_json = json_object_get(connections_obj, "garbage_collection");
		if (garbage_collection_json)
		{
			const char *gc_key;
			json_t *gc_value;
			json_object_foreach(garbage_collection_json, gc_key, gc_value)
			{
				int gc_type = json_typeof(gc_value);
				if (gc_type == JSON_TRUE)
				{
					ival = 1;
					strlcpy(string2+24, gc_key, RABBITMQ_LEN-24);
					metric_add_labels7(string2, &ival, DATATYPE_INT, carg, "host", connections_host, "peer", connections_peer_host, "type", connections_type, "vhost", connections_vhost, "user", connections_user, "protocol", connections_protocol, "auth_mechanism", connections_auth_mechanism);
				}
				else if (gc_type == JSON_FALSE)
				{
					ival = 0;
					strlcpy(string2+24, gc_key, RABBITMQ_LEN-24);
					metric_add_labels7(string2, &ival, DATATYPE_INT, carg, "host", connections_host, "peer", connections_peer_host, "type", connections_type, "vhost", connections_vhost, "user", connections_user, "protocol", connections_protocol, "auth_mechanism", connections_auth_mechanism);
				}
				else if (gc_type == JSON_REAL)
				{
					dval = json_real_value(gc_value);
					strlcpy(string2+24, gc_key, RABBITMQ_LEN-24);
					metric_add_labels7(string2, &dval, DATATYPE_DOUBLE, carg, "host", connections_host, "peer", connections_peer_host, "type", connections_type, "vhost", connections_vhost, "user", connections_user, "protocol", connections_protocol, "auth_mechanism", connections_auth_mechanism);
				}
				else if (gc_type == JSON_INTEGER)
				{
					ival = json_integer_value(gc_value);
					strlcpy(string2+24, gc_key, RABBITMQ_LEN-24);
					metric_add_labels7(string2, &ival, DATATYPE_INT, carg, "host", connections_host, "peer", connections_peer_host, "type", connections_type, "vhost", connections_vhost, "user", connections_user, "protocol", connections_protocol, "auth_mechanism", connections_auth_mechanism);
				}
			}
		}
	}

	json_decref(root);
	carg->parser_status = 1;
}

void rabbitmq_queues_handler(char *metrics, size_t size, context_arg *carg)
{
	json_error_t error;
	json_t *root = json_loads(metrics, 0, &error);
	if (!root)
	{
		fprintf(stderr, "json error on line %d: %s\n", error.line, error.text);
		return;
	}

	char string1[RABBITMQ_LEN];
	char string2[RABBITMQ_LEN];
	char string3[RABBITMQ_LEN];
	char string4[RABBITMQ_LEN];
	strlcpy(string1, "rabbitmq_queues_", 17);
	strlcpy(string2, "rabbitmq_queues_message_stats_", 31);
	strlcpy(string3, "rabbitmq_queues_backing_queue_status_", 38);
	strlcpy(string4, "rabbitmq_queues_garbage_collection_", 36);
	int64_t ival;
	double dval;

	size_t i;
	uint64_t j;
	size_t queues_size = json_array_size(root);
	for (i=0; i<queues_size; i++)
	{
		json_t *queues_obj = json_array_get(root, i);

		json_t *queues_name_json = json_object_get(queues_obj, "name");
		char *queues_name = (char*)json_string_value(queues_name_json);
		if (strlen(queues_name) == 0)
			continue;

		json_t *queues_vhost_json = json_object_get(queues_obj, "vhost");
		char *queues_vhost = (char*)json_string_value(queues_vhost_json);

		json_t *queues_state_json = json_object_get(queues_obj, "state");
		char *queues_state = (char*)json_string_value(queues_state_json);
		if (!queues_state)
			ival = 0;
		else {
			if (!strncmp(queues_state, "running", 7))
				ival = 1;
			else
				ival = 0;
		}
		metric_add_labels2("rabbitmq_queues_state", &ival, DATATYPE_INT, carg, "name", queues_name, "vhost", queues_vhost);

		const char *key;
		json_t *value;
		json_object_foreach(queues_obj, key, value)
		{
			int node_type = json_typeof(value);
			if (node_type == JSON_TRUE)
			{
				ival = 1;
				strlcpy(string1+16, key, RABBITMQ_LEN-16);
				//printf("%s: %"d64"\n", string1, ival);
				metric_add_labels2(string1, &ival, DATATYPE_INT, carg, "name", queues_name, "vhost", queues_vhost);
			}
			else if (node_type == JSON_FALSE)
			{
				ival = 0;
				strlcpy(string1+16, key, RABBITMQ_LEN-16);
				//printf("%s: %"d64"\n", string1, ival);
				metric_add_labels2(string1, &ival, DATATYPE_INT, carg, "name", queues_name, "vhost", queues_vhost);
			}
			else if (node_type == JSON_REAL)
			{
				dval = json_real_value(value);
				strlcpy(string1+16, key, RABBITMQ_LEN-16);
				metric_add_labels2(string1, &dval, DATATYPE_DOUBLE, carg, "name", queues_name, "vhost", queues_vhost);
			}
			else if (node_type == JSON_INTEGER)
			{
				ival = json_integer_value(value);
				strlcpy(string1+16, key, RABBITMQ_LEN-16);
				metric_add_labels2(string1, &ival, DATATYPE_INT, carg, "name", queues_name, "vhost", queues_vhost);
			}
		}

		json_t *message_stats_json = json_object_get(queues_obj, "message_stats");
		if (message_stats_json)
		{
			const char *ms_key;
			json_t *ms_value;
			json_object_foreach(message_stats_json, ms_key, ms_value)
			{
				int node_type = json_typeof(ms_value);
				if (node_type == JSON_TRUE)
				{
					ival = 1;
					strlcpy(string2+30, ms_key, RABBITMQ_LEN-30);
					metric_add_labels2(string2, &ival, DATATYPE_INT, carg, "name", queues_name, "vhost", queues_vhost);
				}
				else if (node_type == JSON_FALSE)
				{
					ival = 0;
					strlcpy(string2+30, ms_key, RABBITMQ_LEN-30);
					metric_add_labels2(string2, &ival, DATATYPE_INT, carg, "name", queues_name, "vhost", queues_vhost);
				}
				else if (node_type == JSON_REAL)
				{
					dval = json_real_value(ms_value);
					strlcpy(string2+30, ms_key, RABBITMQ_LEN-30);
					metric_add_labels2(string2, &dval, DATATYPE_DOUBLE, carg, "name", queues_name, "vhost", queues_vhost);
				}
				else if (node_type == JSON_INTEGER)
				{
					ival = json_integer_value(ms_value);
					strlcpy(string2+30, ms_key, RABBITMQ_LEN-30);
					metric_add_labels2(string2, &ival, DATATYPE_INT, carg, "name", queues_name, "vhost", queues_vhost);
				}
			}
		}

		json_t *backing_queue_status_json = json_object_get(queues_obj, "backing_queue_status");
		if (backing_queue_status_json)
		{
			const char *ms_key;
			json_t *ms_value;
			json_object_foreach(backing_queue_status_json, ms_key, ms_value)
			{
				int node_type = json_typeof(ms_value);
				if (node_type == JSON_TRUE)
				{
					ival = 1;
					strlcpy(string3+37, ms_key, RABBITMQ_LEN-37);
					metric_add_labels2(string3, &ival, DATATYPE_INT, carg, "name", queues_name, "vhost", queues_vhost);
				}
				else if (node_type == JSON_FALSE)
				{
					ival = 0;
					strlcpy(string3+37, ms_key, RABBITMQ_LEN-37);
					metric_add_labels2(string3, &ival, DATATYPE_INT, carg, "name", queues_name, "vhost", queues_vhost);
				}
				else if (node_type == JSON_REAL)
				{
					dval = json_real_value(ms_value);
					strlcpy(string3+37, ms_key, RABBITMQ_LEN-37);
					metric_add_labels2(string3, &dval, DATATYPE_DOUBLE, carg, "name", queues_name, "vhost", queues_vhost);
				}
				else if (node_type == JSON_INTEGER)
				{
					ival = json_integer_value(ms_value);
					strlcpy(string3+37, ms_key, RABBITMQ_LEN-37);
					metric_add_labels2(string3, &ival, DATATYPE_INT, carg, "name", queues_name, "vhost", queues_vhost);
				}
			}
		}

		json_t *garbage_collection_json = json_object_get(queues_obj, "garbage_collection");
		if (garbage_collection_json)
		{
			const char *ms_key;
			json_t *ms_value;
			json_object_foreach(garbage_collection_json, ms_key, ms_value)
			{
				int node_type = json_typeof(ms_value);
				if (node_type == JSON_TRUE)
				{
					ival = 1;
					strlcpy(string4+35, ms_key, RABBITMQ_LEN-35);
					metric_add_labels2(string4, &ival, DATATYPE_INT, carg, "name", queues_name, "vhost", queues_vhost);
				}
				else if (node_type == JSON_FALSE)
				{
					ival = 0;
					strlcpy(string4+35, ms_key, RABBITMQ_LEN-35);
					metric_add_labels2(string4, &ival, DATATYPE_INT, carg, "name", queues_name, "vhost", queues_vhost);
				}
				else if (node_type == JSON_REAL)
				{
					dval = json_real_value(ms_value);
					strlcpy(string4+35, ms_key, RABBITMQ_LEN-35);
					metric_add_labels2(string4, &dval, DATATYPE_DOUBLE, carg, "name", queues_name, "vhost", queues_vhost);
				}
				else if (node_type == JSON_INTEGER)
				{
					ival = json_integer_value(ms_value);
					strlcpy(string4+35, ms_key, RABBITMQ_LEN-35);
					metric_add_labels2(string4, &ival, DATATYPE_INT, carg, "name", queues_name, "vhost", queues_vhost);
				}
			}
		}

		json_t *recoverable_slaves = json_object_get(queues_obj, "recoverable_slaves");
		uint64_t recoverable_slaves_size = json_array_size(recoverable_slaves);
		for (j=0; j<recoverable_slaves_size; j++)
		{
			json_t *recoverable_slaves_obj = json_array_get(recoverable_slaves, j);
			char *srv_name = (char*)json_string_value(recoverable_slaves_obj);
			if (!srv_name)
				continue;

			metric_add_labels3("rabbitmq_queues_recoverable_slaves_node", &j, DATATYPE_UINT, carg, "name", queues_name, "vhost", queues_vhost, "node", srv_name);
		}
		metric_add_labels2("rabbitmq_queues_recoverable_slaves_size", &recoverable_slaves_size, DATATYPE_UINT, carg, "name", queues_name, "vhost", queues_vhost);

		json_t *synchronised_slave_nodes = json_object_get(queues_obj, "synchronised_slave_nodes");
		uint64_t synchronised_slave_nodes_size = json_array_size(synchronised_slave_nodes);
		for (j=0; j<synchronised_slave_nodes_size; j++)
		{
			json_t *synchronised_slave_nodes_obj = json_array_get(synchronised_slave_nodes, j);
			char *srv_name = (char*)json_string_value(synchronised_slave_nodes_obj);
			if (!srv_name)
				continue;

			metric_add_labels3("rabbitmq_queues_synchronised_slave_nodes_node", &j, DATATYPE_UINT, carg, "name", queues_name, "vhost", queues_vhost, "node", srv_name);
		}
		metric_add_labels2("rabbitmq_queues_synchronised_slave_nodes_size", &synchronised_slave_nodes_size, DATATYPE_UINT, carg, "name", queues_name, "vhost", queues_vhost);

		json_t *slave_nodes = json_object_get(queues_obj, "slave_nodes");
		uint64_t slave_nodes_size = json_array_size(slave_nodes);
		for (j=0; j<slave_nodes_size; j++)
		{
			json_t *slave_nodes_obj = json_array_get(slave_nodes, j);
			char *srv_name = (char*)json_string_value(slave_nodes_obj);
			if (!srv_name)
				continue;

			metric_add_labels3("rabbitmq_queues_slave_nodes_node", &j, DATATYPE_UINT, carg, "name", queues_name, "vhost", queues_vhost, "node", srv_name);
		}
		metric_add_labels2("rabbitmq_queues_slave_nodes_size", &slave_nodes_size, DATATYPE_UINT, carg, "name", queues_name, "vhost", queues_vhost);
	}

	json_decref(root);
	carg->parser_status = 1;
}
void rabbitmq_vhosts_handler(char *metrics, size_t size, context_arg *carg)
{
	json_error_t error;
	json_t *root = json_loads(metrics, 0, &error);
	if (!root)
	{
		fprintf(stderr, "json error on line %d: %s\n", error.line, error.text);
		return;
	}

	char string1[RABBITMQ_LEN];
	char string2[RABBITMQ_LEN];
	strlcpy(string1, "rabbitmq_vhosts_", 17);
	strlcpy(string2, "rabbitmq_vhosts_message_stats_", 31);
	int64_t ival;
	double dval;

	size_t i;
	uint64_t j;
	size_t vhosts_size = json_array_size(root);
	for (i=0; i<vhosts_size; i++)
	{
		json_t *vhosts_obj = json_array_get(root, i);
		const char *key;
		json_t *value;

		json_t *vhosts_name_json = json_object_get(vhosts_obj, "name");
		char *vhosts_name = (char*)json_string_value(vhosts_name_json);
		if (strlen(vhosts_name) == 0)
			continue;

		json_t *cluster_state = json_object_get(vhosts_obj, "cluster_state");
		j = 0;
		json_object_foreach(cluster_state, key, value)
		{
			int node_type = json_typeof(value);
			if (node_type == JSON_STRING)
			{
				char *obj_state = (char*)json_string_value(value);
				if (!strncmp(obj_state, "running", 7))
					ival = 1;
				else
					ival = 0;

				metric_add_labels2("rabbitmq_vhosts_cluster_state", &ival, DATATYPE_INT, carg, "name", vhosts_name, "node", (char*)key);
			}
			++j;
		}
		metric_add_labels("rabbitmq_vhosts_cluster_size", &j, DATATYPE_UINT, carg, "name", vhosts_name);
		
		json_object_foreach(vhosts_obj, key, value)
		{
			int node_type = json_typeof(value);
			if (node_type == JSON_TRUE)
			{
				ival = 1;
				strlcpy(string1+16, key, RABBITMQ_LEN-16);
				metric_add_labels(string1, &ival, DATATYPE_INT, carg, "name", vhosts_name);
			}
			else if (node_type == JSON_FALSE)
			{
				ival = 0;
				strlcpy(string1+16, key, RABBITMQ_LEN-16);
				metric_add_labels(string1, &ival, DATATYPE_INT, carg, "name", vhosts_name);
			}
			else if (node_type == JSON_REAL)
			{
				dval = json_real_value(value);
				strlcpy(string1+16, key, RABBITMQ_LEN-16);
				metric_add_labels(string1, &dval, DATATYPE_DOUBLE, carg, "name", vhosts_name);
			}
			else if (node_type == JSON_INTEGER)
			{
				ival = json_integer_value(value);
				strlcpy(string1+16, key, RABBITMQ_LEN-16);
				metric_add_labels(string1, &ival, DATATYPE_INT, carg, "name", vhosts_name);
			}
		}

		json_t *message_stats_json = json_object_get(vhosts_obj, "message_stats");
		if (message_stats_json)
		{
			const char *ms_key;
			json_t *ms_value;
			json_object_foreach(message_stats_json, ms_key, ms_value)
			{
				int node_type = json_typeof(ms_value);
				if (node_type == JSON_TRUE)
				{
					ival = 1;
					strlcpy(string2+30, ms_key, RABBITMQ_LEN-30);
					metric_add_labels(string2, &ival, DATATYPE_INT, carg, "name", vhosts_name);
				}
				else if (node_type == JSON_FALSE)
				{
					ival = 0;
					strlcpy(string2+30, ms_key, RABBITMQ_LEN-30);
					metric_add_labels(string2, &ival, DATATYPE_INT, carg, "name", vhosts_name);
				}
				else if (node_type == JSON_REAL)
				{
					dval = json_real_value(ms_value);
					strlcpy(string2+30, ms_key, RABBITMQ_LEN-30);
					metric_add_labels(string2, &dval, DATATYPE_DOUBLE, carg, "name", vhosts_name);
				}
				else if (node_type == JSON_INTEGER)
				{
					ival = json_integer_value(ms_value);
					strlcpy(string2+30, ms_key, RABBITMQ_LEN-30);
					metric_add_labels(string2, &ival, DATATYPE_INT, carg, "name", vhosts_name);
				}
			}
		}
	}

	json_decref(root);
	carg->parser_status = 1;
}

string *rabbitmq_gen_url(host_aggregator_info *hi, char *addition, void *env, void *proxy_settings)
{
	return string_init_add(gen_http_query(0, hi->query, addition, hi->host, "alligator", hi->auth, 1, NULL, env, proxy_settings, NULL), 0, 0);
}

string* rabbitmq_overview_mesg(host_aggregator_info *hi, void *arg, void *env, void *proxy_settings) { return rabbitmq_gen_url(hi, "/api/overview", env, proxy_settings); }
string* rabbitmq_nodes_mesg(host_aggregator_info *hi, void *arg, void *env, void *proxy_settings) { return rabbitmq_gen_url(hi, "/api/nodes", env, proxy_settings); }
string* rabbitmq_exchanges_mesg(host_aggregator_info *hi, void *arg, void *env, void *proxy_settings) { return rabbitmq_gen_url(hi, "/api/exchanges", env, proxy_settings); }
string* rabbitmq_connections_mesg(host_aggregator_info *hi, void *arg, void *env, void *proxy_settings) { return rabbitmq_gen_url(hi, "/api/connections", env, proxy_settings); }
string* rabbitmq_queues_mesg(host_aggregator_info *hi, void *arg, void *env, void *proxy_settings) { return rabbitmq_gen_url(hi, "/api/queues", env, proxy_settings); }
string* rabbitmq_vhosts_mesg(host_aggregator_info *hi, void *arg, void *env, void *proxy_settings) { return rabbitmq_gen_url(hi, "/api/vhosts", env, proxy_settings); }

void rabbitmq_parser_push()
{
	aggregate_context *actx = calloc(1, sizeof(*actx));

	actx->key = strdup("rabbitmq");
	actx->handlers = 6;
	actx->handler = calloc(1, sizeof(*actx->handler)*actx->handlers);

	actx->handler[0].name = rabbitmq_overview_handler;
	actx->handler[0].validator = NULL;
	actx->handler[0].mesg_func = rabbitmq_overview_mesg;
	strlcpy(actx->handler[0].key,"rabbitmq_overview", 255);

	actx->handler[1].name = rabbitmq_nodes_handler;
	actx->handler[1].validator = NULL;
	actx->handler[1].mesg_func = rabbitmq_nodes_mesg;
	strlcpy(actx->handler[1].key,"rabbitmq_nodes", 255);

	actx->handler[2].name = rabbitmq_exchanges_handler;
	actx->handler[2].validator = NULL;
	actx->handler[2].mesg_func = rabbitmq_exchanges_mesg;
	strlcpy(actx->handler[2].key,"rabbitmq_exchanges", 255);

	actx->handler[3].name = rabbitmq_connections_handler;
	actx->handler[3].validator = NULL;
	actx->handler[3].mesg_func = rabbitmq_connections_mesg;
	strlcpy(actx->handler[3].key,"rabbitmq_connections", 255);

	actx->handler[4].name = rabbitmq_queues_handler;
	actx->handler[4].validator = NULL;
	actx->handler[4].mesg_func = rabbitmq_queues_mesg;
	strlcpy(actx->handler[4].key,"rabbitmq_queues", 255);

	actx->handler[5].name = rabbitmq_vhosts_handler;
	actx->handler[5].validator = NULL;
	actx->handler[5].mesg_func = rabbitmq_vhosts_mesg;
	strlcpy(actx->handler[5].key,"rabbitmq_vhosts", 255);

	alligator_ht_insert(ac->aggregate_ctx, &(actx->node), actx, tommy_strhash_u32(0, actx->key));
}

