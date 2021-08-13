#include <jansson.h>
#include <string.h>
#include <stdio.h>
#include <inttypes.h>
#include "common/selector.h"
#include "metric/namespace.h"
#include "events/context_arg.h"
#include "common/aggregator.h"
#include "main.h"
#define IPMI_METRIC_SIZE 256

typedef struct ipmi_data
{
	alligator_ht *event_log;
} ipmi_data;

typedef struct eventlog_node
{
	char key[256];
	char resource[256];
	char state[256];
	uint64_t val;

	tommy_node node;
} eventlog_node;

uint8_t ipmi_set_null_sep(char *name, size_t size)
{
	uint8_t i = size;
	while (i--)
	{
		if (isspace(name[i]))
			name[i] = 0;
		else
			break;
	}

	return i;
}

int eventlog_node_compare(const void* arg, const void* obj)
{
        char *s1 = (char*)arg;
        char *s2 = ((eventlog_node*)obj)->key;
        return strcmp(s1, s2);
}

void event_log_for(void *funcarg, void* arg)
{
	eventlog_node *eventnode = arg;
	context_arg *carg = funcarg;

	ipmi_data *idata = carg->data;
	
	metric_add_labels3("ipmi_eventlog_key", &eventnode->val, DATATYPE_UINT, carg, "key",  eventnode->key, "state", eventnode->state, "resource", eventnode->resource);
	alligator_ht_remove_existing(idata->event_log, &(eventnode->node));
	free(eventnode);
}

// return 0 if success
// or value for next newline
uint64_t ipmi_get_double(char *str, uint8_t *retind, double *ret)
{
	*retind = strcspn(str, "|\n\r");
	if ((str[0] == '\n') || (str[1] == '\r') || (str[0] == '\0'))
	{
		*ret = 0;
		return strcspn(str, "\n");
	}

	if (((str[0] == 'n') && (str[1] == 'a')) || ((str[1] == 'x')))
	{
		*ret = 0;
		return strcspn(str, "\n");
	}

	*ret = strtod(str, NULL);
	return 0;
}

void ipmi_sensor_handler(char *metrics, size_t size, context_arg *carg)
{
	uint8_t newind;
	double cur;
	double lower_non_recoverable;
	double lower_critical;
	double lower_non_critical;
	double upper_non_recoverable;
	double upper_critical;
	double upper_non_critical;
	char name[IPMI_METRIC_SIZE];
	char measure[IPMI_METRIC_SIZE];
	char state[IPMI_METRIC_SIZE];
	for (uint64_t i = 0; i < size; i++)
	{

		// name
		newind = strcspn(metrics+i, "|\n\r");
		if ((metrics[i] == '\n') || (metrics[i] == '\r') || (metrics[i] == '\0'))
		{
			i += strcspn(metrics+i, "\n");
			continue;
		}

		strlcpy(name, metrics+i, newind+1);
		ipmi_set_null_sep(name, newind);
		if (carg->log_level > 1)
			printf("name is '%s'\n", name);

		i += newind;
		i += strspn(metrics+i, " |\t");

		// current
		uint8_t newline = ipmi_get_double(metrics+i, &newind, &cur);
		if (newline)
		{
			i += newline;
			continue;
		}

		if (carg->log_level > 1)
			printf("\tcur is '%lf'\n", cur);
		i += newind;
		i += strspn(metrics+i, " |\t");

		// measure
		newind = strcspn(metrics+i, "|\n\r");
		if ((metrics[i] == '\n') || (metrics[i] == '\r') || (metrics[i] == '\0'))
		{
			i += strcspn(metrics+i, "\n");
			continue;
		}

		strlcpy(measure, metrics+i, newind+1);
		ipmi_set_null_sep(measure, newind);
		if (carg->log_level > 1)
			printf("\tmeasure is '%s'\n", measure);

		i += newind;
		i += strspn(metrics+i, " |\t");
		metric_add_labels2("ipmi_sensor_stat", &cur, DATATYPE_DOUBLE, carg, "name",  name, "measure", measure);

		// state
		newind = strcspn(metrics+i, "|\n\r");
		if ((metrics[i] == '\n') || (metrics[i] == '\r') || (metrics[i] == '\0'))
		{
			i += strcspn(metrics+i, "\n");
			continue;
		}

		strlcpy(state, metrics+i, newind+1);
		ipmi_set_null_sep(state, newind);
		if (carg->log_level > 1)
			printf("\tstate is '%s'\n", state);
		uint64_t state_int = 0;
		if ((state[0] == 'o') && (state[1] == 'k'))
			state_int = 1;
		metric_add_labels2("ipmi_sensor_status", &state_int, DATATYPE_UINT, carg, "name",  name, "measure", measure);

		i += newind;
		i += strspn(metrics+i, " |\t");

		//lower_non_recoverable = strtod(metrics+i, NULL);
		newline = ipmi_get_double(metrics+i, &newind, &lower_non_recoverable);
		if (!newline)
		{
			if (carg->log_level > 1)
				printf("\tlower_non_recoverable is '%lf'\n", lower_non_recoverable);
			metric_add_labels2("ipmi_sensor_lower_non_recoverable", &lower_non_recoverable, DATATYPE_DOUBLE, carg, "name",  name, "measure", measure);
		}
		i += newind;
		i += strspn(metrics+i, " |\t");

		//lower_critical = strtod(metrics+i, NULL);
		newline = ipmi_get_double(metrics+i, &newind, &lower_critical);
		if (!newline)
		{
			if (carg->log_level > 1)
				printf("\tlower_critical is '%lf'\n", lower_critical);
			metric_add_labels2("ipmi_sensor_lower_critical", &lower_critical, DATATYPE_DOUBLE, carg, "name",  name, "measure", measure);
		}
		i += newind;
		i += strspn(metrics+i, " |\t");

		// lower non_critical
		newline = ipmi_get_double(metrics+i, &newind, &lower_non_critical);
		if (!newline)
		{
			if (carg->log_level > 1)
				printf("\tlower_non_critical is '%lf'\n", lower_non_critical);
			metric_add_labels2("ipmi_sensor_lower_non_critical", &lower_non_critical, DATATYPE_DOUBLE, carg, "name",  name, "measure", measure);
		}
		i += newind;
		i += strspn(metrics+i, " |\t");

		//upper_non_recoverable = strtod(metrics+i, NULL);
		newline = ipmi_get_double(metrics+i, &newind, &upper_non_recoverable);
		if (!newline)
		{
			if (carg->log_level > 1)
				printf("\tupper_non_recoverable is '%lf'\n", upper_non_recoverable);
			metric_add_labels2("ipmi_sensor_non_recoverable", &upper_non_recoverable, DATATYPE_DOUBLE, carg, "name",  name, "measure", measure);
		}
		i += newind;
		i += strspn(metrics+i, " |\t");

		//upper_critical = strtod(metrics+i, NULL);
		newline = ipmi_get_double(metrics+i, &newind, &upper_critical);
		if (!newline)
		{
			if (carg->log_level > 1)
				printf("\tupper_critical is '%lf'\n", upper_critical);
			metric_add_labels2("ipmi_sensor_upper_critical", &upper_critical, DATATYPE_DOUBLE, carg, "name",  name, "measure", measure);
		}
		i += newind;
		i += strspn(metrics+i, " |\t");

		// upper non_critical
		newline = ipmi_get_double(metrics+i, &newind, &upper_non_critical);
		if (!newline)
		{
			if (carg->log_level > 1)
				printf("\tupper_non_critical is '%lf'\n", upper_non_critical);
			metric_add_labels2("ipmi_sensor_upper_non_critical", &upper_non_critical, DATATYPE_DOUBLE, carg, "name",  name, "measure", measure);
		}
		i += newind;
		i += strspn(metrics+i, " |\t");

		i += strcspn(metrics+i, "\n");
	}
}

void ipmi_elist_handler(char *metrics, size_t size, context_arg *carg)
{
	//7854 | 10/30/2019 | 07:33:53 | Memory | Uncorrectable ECC (@DIMM3B(CPU2)) | Asserted

	ipmi_data *idata = carg->data;
	if (!idata->event_log)
	{
		idata->event_log = calloc(1, sizeof(*idata->event_log));
		alligator_ht_init(idata->event_log);
	}
	
	uint8_t newind;
	uint64_t num;
	char key[IPMI_METRIC_SIZE];
	char state[IPMI_METRIC_SIZE];
	char resource[IPMI_METRIC_SIZE];
	for (uint64_t i = 0; i < size; i++)
	{

		// num
		newind = strcspn(metrics+i, "|\n\r");
		if ((metrics[i] == '\n') || (metrics[i] == '\r') || (metrics[i] == '\0'))
		{
			i += strcspn(metrics+i, "\n");
			continue;
		}

		num = strtoul(metrics+i, NULL, 16);
		if (carg->log_level > 1)
			printf("num is '%"u64"'\n", num);
		i += newind;
		i += strspn(metrics+i, " |\t");

		i += strcspn(metrics+i, " |\t");
		i += strspn(metrics+i, " |\t");
		i += strcspn(metrics+i, " |\t");
		i += strspn(metrics+i, " |\t");

		// resource
		newind = strcspn(metrics+i, "|\n\r");
		if ((metrics[i] == '\n') || (metrics[i] == '\r') || (metrics[i] == '\0'))
		{
			i += strcspn(metrics+i, "\n");
			continue;
		}

		strlcpy(resource, metrics+i, newind+1);
		ipmi_set_null_sep(resource, newind);
		if (carg->log_level > 1)
			printf("\tresource is '%s'\n", resource);

		i += newind;
		i += strspn(metrics+i, " |\t");

		// key
		newind = strcspn(metrics+i, "|\n\r");
		if ((metrics[i] == '\n') || (metrics[i] == '\r') || (metrics[i] == '\0'))
		{
			i += strcspn(metrics+i, "\n");
			continue;
		}

		strlcpy(key, metrics+i, newind+1);
		ipmi_set_null_sep(key, newind);
		if (carg->log_level > 1)
			printf("\tkey is '%s'\n", key);

		i += newind;
		i += strspn(metrics+i, " |\t");

		// state
		newind = strcspn(metrics+i, "|\n\r");
		if ((metrics[i] == '\n') || (metrics[i] == '\r') || (metrics[i] == '\0'))
		{
			i += strcspn(metrics+i, "\n");
			continue;
		}

		strlcpy(state, metrics+i, newind+1);
		ipmi_set_null_sep(state, newind);
		if (carg->log_level > 1)
			printf("\tstate is '%s'\n", state);

		i += newind;
		i += strspn(metrics+i, " |\t");

		uint32_t key_hash = tommy_strhash_u32(0, key);
		eventlog_node *eventnode = alligator_ht_search(idata->event_log, eventlog_node_compare, key, key_hash);
		if (!eventnode)
		{
			eventnode = calloc(1, sizeof(*eventnode));
			strlcpy(eventnode->key, key, 255);
			strlcpy(eventnode->resource, resource, 255);
			strlcpy(eventnode->state, state, 255);
			eventnode->val = 1;
			alligator_ht_insert(idata->event_log, &(eventnode->node), eventnode, tommy_strhash_u32(0, eventnode->key));
		}
		else
			++(eventnode->val);

		i += strcspn(metrics+i, "\n");
	}

	alligator_ht_foreach_arg(idata->event_log, event_log_for, carg);
	metric_add_auto("ipmi_eventlog_size", &num, DATATYPE_UINT, carg);
}

void ipmi_chassis_status_handler(char *metrics, size_t size, context_arg *carg)
{
	uint8_t newind;
	uint64_t val;
	char name[IPMI_METRIC_SIZE];
	char state[IPMI_METRIC_SIZE];
	strlcpy(name, "IPMI_", 6);
	for (uint64_t i = 0; i < size; i++)
	{
		// name
		newind = strcspn(metrics+i, ":\n\r");
		if ((metrics[i] == '\n') || (metrics[i] == '\r') || (metrics[i] == '\0'))
		{
			i += strcspn(metrics+i, "\n");
			continue;
		}

		strlcpy(name+5, metrics+i, newind+1);
		uint8_t newsize = ipmi_set_null_sep(name+5, newind);
		metric_name_normalizer(name+5, newsize);
		if (carg->log_level > 1)
			printf("name is '%s'\n", name);

		i += newind;
		i += strspn(metrics+i, " :\t");

		// state
		newind = strcspn(metrics+i, ":\n\r");
		if ((metrics[i] == '\n') || (metrics[i] == '\r') || (metrics[i] == '\0'))
		{
			i += strcspn(metrics+i, "\n");
			continue;
		}

		strlcpy(state, metrics+i, newind+1);
		ipmi_set_null_sep(state, newind);
		if (carg->log_level > 1)
			printf("\tstate is '%s'\n", state);
		if (!strcmp(state, "on") || !strcmp(state, "true"))
		{
			val = 1;
			metric_add_auto(name, &val, DATATYPE_UINT, carg);
		}
		else if (!strcmp(state, "inactive") || !strcmp(state, "false") || !strcmp(state, "none"))
		{
			val = 0;
			metric_add_auto(name, &val, DATATYPE_UINT, carg);
		}
		else
		{
			val = 1;
			metric_add_labels(name, &val, DATATYPE_UINT, carg, "state", state);
		}

		i += strcspn(metrics+i, "\n");
	}
}

void ipmi_sel_info_handler(char *metrics, size_t size, context_arg *carg)
{
	uint8_t newind;
	double dval;
	uint64_t val;
	char name[IPMI_METRIC_SIZE];
	strlcpy(name, "IPMI_", 6);
	for (uint64_t i = 0; i < size; i++)
	{
		// name
		newind = strcspn(metrics+i, ":\n\r");
		if ((metrics[i] == '\n') || (metrics[i] == '\r') || (metrics[i] == '\0'))
		{
			i += strcspn(metrics+i, "\n");
			continue;
		}

		if (!strncmp(metrics+i, "# of ", 5))
		{
			newind -= 5;
			i += 5;
		}
		else if (!strncmp(metrics+i, "# ", 2))
		{
			newind -= 2;
			i += 2;
		}

		strlcpy(name+5, metrics+i, newind+1);
		uint8_t newsize = ipmi_set_null_sep(name+5, newind);
		metric_name_normalizer(name+5, newsize);
		if (carg->log_level > 1)
			printf("name is '%s'\n", name);


		if (!strcmp(name, "IPMI_Last_Add_Time") || !strcmp(name, "IPMI_Last_Del_Time"))
		{
			i += strcspn(metrics+i, "\n");
			continue;
		}

		i += newind;
		i += strspn(metrics+i, " :\t");

		// state
		i += strcspn(metrics+i, ":\n\r");
		if ((metrics[i] == '\n') || (metrics[i] == '\r') || (metrics[i] == '\0'))
		{
			i += strcspn(metrics+i, "\n");
			continue;
		}

		if (!isdigit(metrics[i]))
		{
			i += strcspn(metrics+i, "\n");
			continue;
		}

		val = strtoull(metrics+i, NULL, 10);
		dval = strtod(metrics+i, NULL);
		if (carg->log_level > 1)
			printf("\tdata is '%"u64"/%lf'\n", val, dval);

		if (!strcmp(name, "IPMI_Version"))
		{
			metric_add_auto(name, &dval, DATATYPE_DOUBLE, carg);
		}
		else
		{
			metric_add_auto(name, &val, DATATYPE_UINT, carg);
		}

		i += strcspn(metrics+i, "\n");
	}
}

void ipmi_lan_print_handler(char *metrics, size_t size, context_arg *carg)
{
	uint8_t newind;
	uint64_t val = 1;
	char name[IPMI_METRIC_SIZE];
	char state[IPMI_METRIC_SIZE];

	alligator_ht *hash = alligator_ht_init(NULL);

	for (uint64_t i = 0; i < size; i++)
	{
		// name
		newind = strcspn(metrics+i, ":\n\r");
		if ((metrics[i] == '\n') || (metrics[i] == '\r') || (metrics[i] == '\0'))
		{
			i += strcspn(metrics+i, "\n");
			continue;
		}

		strlcpy(name, metrics+i, newind+1);
		ipmi_set_null_sep(name, newind);
		i += newind;
		i += strspn(metrics+i, " :\t");

		if (carg->log_level > 1)
			printf("name is '%s'\n", name);

		// state
		newind = strcspn(metrics+i, "\n\r");
		if ((metrics[i] == '\n') || (metrics[i] == '\r') || (metrics[i] == '\0'))
		{
			i += strcspn(metrics+i, "\n");
			continue;
		}

		strlcpy(state, metrics+i, newind+1);
		ipmi_set_null_sep(state, newind);
		if (carg->log_level > 1)
			printf("\tstate is '%s'\n", state);

		if (!strcmp(name, "IP Address Source"))
			labels_hash_insert_nocache(hash, "source", state);
		else if (!strcmp(name, "IP Address"))
			labels_hash_insert_nocache(hash, "ip", state);
		else if (!strcmp(name, "Subnet Mask"))
			labels_hash_insert_nocache(hash, "mask", state);
		else if (!strcmp(name, "MAC Address"))
			labels_hash_insert_nocache(hash, "mac", state);
		else if (!strcmp(name, "Default Gateway IP"))
			labels_hash_insert_nocache(hash, "default_gateway_ip", state);
		else if (!strcmp(name, "Default Gateway MAC"))
			labels_hash_insert_nocache(hash, "default_gateway_mac", state);
		else if (!strcmp(name, "Backup Gateway IP"))
			labels_hash_insert_nocache(hash, "backup_gateway_ip", state);
		else if (!strcmp(name, "Backup Gateway MAC"))
			labels_hash_insert_nocache(hash, "backup_gateway_mac", state);

		i += strcspn(metrics+i, "\n");
	}
	metric_add("IPMI_Lan", hash, &val, DATATYPE_UINT, ac->system_carg);
}

string* ipmi_sensor_mesg(host_aggregator_info *hi, void *arg, void *env, void *proxy_settings)
{
	return string_init_alloc("sensor list", 0);
}

string* ipmi_chassis_status_mesg(host_aggregator_info *hi, void *arg, void *env, void *proxy_settings)
{
	return string_init_alloc("chassis status", 0);
}

string* ipmi_sel_info_mesg(host_aggregator_info *hi, void *arg, void *env, void *proxy_settings)
{
	return string_init_alloc("sel info", 0);
}

string* ipmi_sel_elist_mesg(host_aggregator_info *hi, void *arg, void *env, void *proxy_settings)
{
	return string_init_alloc("sel elist", 0);
}

string* ipmi_lan_print_mesg(host_aggregator_info *hi, void *arg, void *env, void *proxy_settings)
{
	return string_init_alloc("lan print", 0);
}

void ipmi_parser_push()
{
	aggregate_context *actx = calloc(1, sizeof(*actx));

	actx->key = strdup("ipmi");
	actx->data = calloc(1, sizeof(ipmi_data));
	actx->handlers = 5;
	actx->handler = calloc(1, sizeof(*actx->handler)*actx->handlers);

	actx->handler[0].name = ipmi_sensor_handler;
	actx->handler[0].validator = NULL;
	actx->handler[0].mesg_func = ipmi_sensor_mesg;
	strlcpy(actx->handler[0].key,"ipmi_sensor", 255);

	actx->handler[1].name = ipmi_elist_handler;
	actx->handler[1].validator = NULL;
	actx->handler[1].mesg_func = ipmi_sel_elist_mesg;
	strlcpy(actx->handler[1].key,"ipmi_elist", 255);

	actx->handler[2].name = ipmi_chassis_status_handler;
	actx->handler[2].validator = NULL;
	actx->handler[2].mesg_func = ipmi_chassis_status_mesg;
	strlcpy(actx->handler[2].key,"ipmi_chassis", 255);

	actx->handler[3].name = ipmi_sel_info_handler;
	actx->handler[3].validator = NULL;
	actx->handler[3].mesg_func = ipmi_sel_info_mesg;
	strlcpy(actx->handler[3].key,"ipmi_sel_info", 255);

	actx->handler[4].name = ipmi_lan_print_handler;
	actx->handler[4].validator = NULL;
	actx->handler[4].mesg_func = ipmi_lan_print_mesg;
	strlcpy(actx->handler[4].key,"ipmi_lan_print", 255);

	alligator_ht_insert(ac->aggregate_ctx, &(actx->node), actx, tommy_strhash_u32(0, actx->key));
}
