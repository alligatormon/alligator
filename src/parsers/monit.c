#include <stdio.h>
#include <string.h>
#include "common/selector.h"
#include "metric/namespace.h"
#include "events/context_arg.h"
#include "common/http.h"
#include "main.h"

#define MONIT_NAME_SIZE 10000
#define MONIT_SLIM_SIZE 1000

void monit_handler(char *metrics, size_t size, context_arg *carg)
{
	char *tmp2;
	char *tmp = strstr(metrics, "</version>");
	if (!tmp)
		return;

	char name[MONIT_NAME_SIZE];
	char value[MONIT_NAME_SIZE];
	size_t name_size = MONIT_NAME_SIZE;
	size_t value_size = MONIT_NAME_SIZE;
	size_t end;
	if (!get_xml_node(tmp, (size - (tmp - metrics)), name, &name_size, value, &value_size, &end))
		return;

	tmp = strstr(tmp, "<service");
	if (!tmp)
		return;

	while (tmp++ - metrics < size )
	{
		tmp = strstr(tmp, "<name");
		if (!tmp)
			return;
		tmp2 = strstr(tmp, "</service>");
		if (!tmp2)
			return;

		if (!get_xml_node(tmp, (size - (tmp - metrics)), name, &name_size, value, &value_size, &end))
			return;
		tmp += end;

		char mname[MONIT_NAME_SIZE];
		strcpy(mname, "monit_");
		while (tmp++ < tmp2)
		{
			char node_name[MONIT_NAME_SIZE];
			char node_value[MONIT_NAME_SIZE];
			size_t node_name_size;
			size_t node_value_size;
			if (!get_xml_node(tmp, (size - (tmp - metrics)), node_name, &node_name_size, node_value, &node_value_size, &end))
				return;

			if (!strncmp(node_name, "service", 7))
				break;
			if (node_name_size == 4 && !strncmp(node_name, "port", 4))
			{

				// <hostname>localhost</hostname><portnumber>80</portnumber><request><![CDATA[/api/ping]]></request><protocol>HTTP</protocol><type>TCP</type><responsetime>0.001968</responsetime>
				char *porttmp;

				char hostname_name[MONIT_SLIM_SIZE];
				char hostname_value[MONIT_SLIM_SIZE];
				char portnumber_name[MONIT_SLIM_SIZE];
				char portnumber_value[MONIT_SLIM_SIZE];
				char rawrequest_name[MONIT_SLIM_SIZE];
				char request[MONIT_SLIM_SIZE];
				char rawrequest_value[MONIT_SLIM_SIZE];
				char protocol_name[MONIT_SLIM_SIZE];
				char protocol_value[MONIT_SLIM_SIZE];
				char type_name[MONIT_SLIM_SIZE];
				char type_value[MONIT_SLIM_SIZE];
				char responsetime_name[MONIT_SLIM_SIZE];
				char responsetime_value[MONIT_SLIM_SIZE];
				size_t portsize_1;
				size_t portsize_2;
				size_t portsize_3;

				porttmp = strstr(tmp, "<hostname>");
				if (!porttmp)
					return;
				if (!get_xml_node(porttmp, (size - (porttmp - metrics)), hostname_name, &portsize_1, hostname_value, &portsize_2, &portsize_3))
					return;

				porttmp = strstr(tmp, "<portnumber>");
				if (!porttmp)
					return;
				if (!get_xml_node(porttmp, (size - (porttmp - metrics)), portnumber_name, &portsize_1, portnumber_value, &portsize_2, &portsize_3))
					return;

				porttmp = strstr(tmp, "<request>");
				if (!porttmp)
					return;
				if (!get_xml_node(porttmp, (size - (porttmp - metrics)), rawrequest_name, &portsize_1, rawrequest_value, &portsize_2, &portsize_3))
					return;
				strlcpy(request, rawrequest_value+9, strcspn(rawrequest_value+9, "]")+1);

				porttmp = strstr(tmp, "<protocol>");
				if (!porttmp)
					return;
				if (!get_xml_node(porttmp, (size - (porttmp - metrics)), protocol_name, &portsize_1, protocol_value, &portsize_2, &portsize_3))
					return;

				porttmp = strstr(tmp, "<type>");
				if (!porttmp)
					return;
				if (!get_xml_node(porttmp, (size - (porttmp - metrics)), type_name, &portsize_1, type_value, &portsize_2, &portsize_3))
					return;

				porttmp = strstr(tmp, "<responsetime>");
				if (!porttmp)
					return;
				if (!get_xml_node(porttmp, (size - (porttmp - metrics)), responsetime_name, &portsize_1, responsetime_value, &portsize_2, &portsize_3))
					return;

				double mval = atof(responsetime_value);
				metric_add_labels6("monit_service_responsetime", &mval, DATATYPE_DOUBLE, carg, "name", value, hostname_name, hostname_value, portnumber_name, portnumber_value, rawrequest_name, request, protocol_name, protocol_value, type_name, type_value);

				continue;
			}

			else if (node_name_size == 7 && !strncmp(node_name, "program", 7))
			{
				//<program> <started>1561892520</started> <status>0</status> <output><![CDATA[on]]></output></program>
				char started_name[MONIT_SLIM_SIZE];
				char started_value[MONIT_SLIM_SIZE];
				size_t portsize_1;
				size_t portsize_2;
				size_t portsize_3;
				
				char *programtmp = strstr(tmp, "<started>");
				if (!programtmp)
					return;
				if (!get_xml_node(programtmp, (size - (programtmp - metrics)), started_name, &portsize_1, started_value, &portsize_2, &portsize_3))
					return;

				uint64_t mval = atof(started_value);
				metric_add_labels("monit_program_start", &mval, DATATYPE_UINT, carg, "name", value);
			}
			else if (node_name_size == 10 && !strncmp(node_name, "timestamps", 10))
			{
				//<timestamps> <access>1561892247</access> <change>1561892315</change> <modify>1561892247</modify> </timestamps>
				char timestamps_name[MONIT_SLIM_SIZE];
				char timestamps_value[MONIT_SLIM_SIZE];
				size_t timestamps_1;
				size_t timestamps_2;
				size_t timestamps_3;
				uint64_t access, change, modify;
				
				char *timestampstmp;
				timestampstmp = strstr(tmp, "<access>");
				if (!timestampstmp)
					return;
				if (!get_xml_node(timestampstmp, (size - (timestampstmp - metrics)), timestamps_name, &timestamps_1, timestamps_value, &timestamps_2, &timestamps_3))
					return;
				access = atoll(timestamps_value);


				timestampstmp = strstr(tmp, "<change>");
				if (!timestampstmp)
					return;
				if (!get_xml_node(timestampstmp, (size - (timestampstmp - metrics)), timestamps_name, &timestamps_1, timestamps_value, &timestamps_2, &timestamps_3))
					return;
				change = atoll(timestamps_value);

				timestampstmp = strstr(tmp, "<modify>");
				if (!timestampstmp)
					return;
				if (!get_xml_node(timestampstmp, (size - (timestampstmp - metrics)), timestamps_name, &timestamps_1, timestamps_value, &timestamps_2, &timestamps_3))
					return;
				modify = atoll(timestamps_value);

				metric_add_labels("monit_timestamps_access", &access, DATATYPE_UINT, carg, "name", value);
				metric_add_labels("monit_timestamps_change", &change, DATATYPE_UINT, carg, "name", value);
				metric_add_labels("monit_timestamps_modify", &modify, DATATYPE_UINT, carg, "name", value);
			}

			strlcpy(mname+6, node_name, node_name_size+1);

			int rc = metric_value_validator(node_value, node_value_size);
			if (rc == DATATYPE_INT)
			{
				int64_t mval = atoll(node_value);
				metric_add_labels(mname, &mval, rc, carg, name, value);
			}
			else if (rc == DATATYPE_UINT)
			{
				uint64_t mval = atoll(node_value);
				metric_add_labels(mname, &mval, rc, carg, name, value);
			}
			else if (rc == DATATYPE_DOUBLE)
			{
				double mval = atof(node_value);
				metric_add_labels(mname, &mval, rc, carg, name, value);
			}

			tmp += end;
		}
	}
}

string* monit_mesg(host_aggregator_info *hi, void *arg, void *env, void *proxy_settings)
{
	return string_init_add(gen_http_query(0, hi->query, "/_status?format=xml&level=full", hi->host, "alligator", hi->auth, 1, NULL, env, proxy_settings), 0, 0);
}

void monit_parser_push()
{
	aggregate_context *actx = calloc(1, sizeof(*actx));

	actx->key = strdup("monit");
	actx->handlers = 1;
	actx->handler = malloc(sizeof(*actx->handler)*actx->handlers);

	actx->handler[0].name = monit_handler;
	actx->handler[0].validator = NULL;
	actx->handler[0].mesg_func = monit_mesg;
	strlcpy(actx->handler[0].key,"monit", 255);

	tommy_hashdyn_insert(ac->aggregate_ctx, &(actx->node), actx, tommy_strhash_u32(0, actx->key));
}
