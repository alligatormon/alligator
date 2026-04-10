#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <jansson.h>
#include "common/json_query.h"
#include "common/selector.h"
#include "metric/namespace.h"
#include "metric/labels.h"
#include "events/context_arg.h"
#include "common/aggregator.h"
#include "common/http.h"
#include "common/logs.h"
#include "common/validator.h"
#include "common/reject.h"
#include "cluster/pass.h"
#include "parsers/metric_types.h"
#include "parsers/http_proto.h"
#include "api/api.h"
#include "main.h"

#define DT_TOKEN_MAX 255
#define DT_VAL_MAX 1023
#define DT_LINE_BUF 65535

static int dt_space(char c)
{
	return c == ' ' || c == '\t' || c == '\r';
}

static void dt_rtrim(char *line, size_t *len)
{
	while (*len > 0 && (line[*len - 1] == '\n' || line[*len - 1] == '\r' || dt_space(line[*len - 1])))
		(*len)--;
	line[*len] = '\0';
}

static int dt_quoted_value(const char *s, size_t n, size_t *i, char *out, size_t outsz)
{
	if (*i >= n || s[*i] != '"')
		return 0;
	(*i)++;
	size_t o = 0;
	while (*i < n && o + 1 < outsz)
	{
		unsigned char c = (unsigned char)s[*i];
		if (c == '\\' && *i + 1 < n)
		{
			(*i)++;
			out[o++] = s[(*i)++];
			continue;
		}
		if (c == '"')
		{
			(*i)++;
			out[o] = '\0';
			return 1;
		}
		out[o++] = (char)c;
		(*i)++;
	}
	return 0;
}


// https://docs.dynatrace.com/docs/ingest-from/extend-dynatrace/extend-metrics/reference/metric-ingestion-protocol
static uint8_t dynatrace_ingest_parse_line(char *line, size_t len, context_arg *carg)
{
	alligator_ht *lbl = NULL;
	size_t i = 0;

	while (i < len && dt_space(line[i]))
		i++;
	if (!len || i >= len || line[i] == '#')
		return 0;

	size_t key_start = i;
	if (!(isalpha((unsigned char)line[i]) || line[i] == '_' || line[i] == ':'))
		return 0;
	i++;

	while (i < len)
	{
		unsigned char c = (unsigned char)line[i];
		if (isalnum(c) || c == '_' || c == '.' || c == ':' || c == '-')
			i++;
		else
			break;
	}
	size_t key_len = i - key_start;
	if (!key_len || key_len >= DT_TOKEN_MAX)
		return 0;

	char metric_name[DT_TOKEN_MAX + 1];
	memcpy(metric_name, line + key_start, key_len);
	metric_name[key_len] = '\0';

	if (!metric_name_validator_promstatsd(metric_name, key_len))
		return 0;

	while (i < len && dt_space(line[i]))
		i++;

	while (i < len && line[i] == ',')
	{
		i++;
		size_t dn_start = i;
		while (i < len && line[i] != '=')
		{
			if (dt_space(line[i]))
			{
				labels_hash_free(lbl);
				return 0;
			}
			i++;
		}
		if (i >= len || line[i] != '=')
		{
			labels_hash_free(lbl);
			return 0;
		}
		size_t dn_len = i - dn_start;
		i++;
		if (!dn_len || dn_len >= DT_TOKEN_MAX)
		{
			labels_hash_free(lbl);
			return 0;
		}
		char dim_name[DT_TOKEN_MAX + 1];
		memcpy(dim_name, line + dn_start, dn_len);
		dim_name[dn_len] = '\0';

		char dim_val[DT_VAL_MAX + 1];
		if (i < len && line[i] == '"')
		{
			if (!dt_quoted_value(line, len, &i, dim_val, sizeof(dim_val)))
			{
				labels_hash_free(lbl);
				return 0;
			}
		}
		else
		{
			size_t v0 = i;
			while (i < len && line[i] != ',' && !dt_space(line[i]))
				i++;
			size_t vl = i - v0;
			if (!vl || vl >= sizeof(dim_val))
			{
				labels_hash_free(lbl);
				return 0;
			}
			memcpy(dim_val, line + v0, vl);
			dim_val[vl] = '\0';
		}

		if (carg && reject_metric(carg->reject, dim_name, dim_val))
		{
			labels_hash_free(lbl);
			return 0;
		}
		metric_label_value_validator_normalizer(dim_val, strlen(dim_val));
		if (!metric_label_validator(dim_name, dn_len))
		{
			labels_hash_free(lbl);
			return 0;
		}
		tag_normalizer_dynatrace(dim_name, dn_len);
		if (!lbl)
			lbl = alligator_ht_init(NULL);
		labels_hash_insert_nocache(lbl, dim_name, dim_val);
	}

	while (i < len && dt_space(line[i]))
		i++;
	if (i >= len)
	{
		labels_hash_free(lbl);
		return 0;
	}

	char *end_num = NULL;
	double value = strtod(line + i, &end_num);
	if (end_num == line + i)
	{
		labels_hash_free(lbl);
		return 0;
	}
	i = (size_t)(end_num - line);
	while (i < len && dt_space(line[i]))
		i++;
	if (i < len && (line[i] == '-' || isdigit((unsigned char)line[i])))
	{
		char *end_ts = NULL;
		(void)strtoll(line + i, &end_ts, 10);
		if (end_ts && end_ts > line + i)
			i = (size_t)(end_ts - line);
		while (i < len && dt_space(line[i]))
			i++;
	}
	if (i < len && line[i] != '\0')
		carglog(carg, L_DEBUG, "dynatrace_metrics_ingest: ignored trailing: '%s'\n", line + i);

	metric_name_normalizer(metric_name, key_len);

	if (cluster_pass(carg, metric_name, lbl, &value, DATATYPE_DOUBLE, METRIC_TYPE_UNTYPED))
	{
		labels_hash_free(lbl);
		return 1;
	}
	metric_add(metric_name, lbl, &value, DATATYPE_DOUBLE, carg);
	return 1;
}

string *dynatrace_metrics_ingest_mesg(host_aggregator_info *hi, void *arg, void *env, void *proxy_settings)
{
	(void)arg;
	if ((hi->proto == APROTO_HTTP) || (hi->proto == APROTO_HTTPS))
		return string_init_add_auto(gen_http_query(0, hi->query, NULL, hi->host, "alligator", hi->auth, "1.1", env, proxy_settings, NULL));
	return NULL;
}

void dynatrace_metrics_ingest_handler(string *response, http_reply_data *http_data, const char *configbody, context_arg *carg)
{
	const char *body = http_data ? http_data->body : configbody;
	size_t body_size = http_data ? http_data->body_size : (configbody ? strlen(configbody) : 0);
	uint8_t method = http_data ? http_data->method : HTTP_METHOD_PUT;

	char line[DT_LINE_BUF];
	char jsonbuf[192];
	char temp_resp[512];
	uint64_t lines_ok = 0;
	uint64_t lines_invalid = 0;
	uint64_t n_nonempty = 0;
	int64_t cnt = 0;

	context_arg *mc = carg ? carg : (ac ? ac->system_carg : NULL);

	if (!response)
		return;

	if (method != HTTP_METHOD_POST)
	{
		snprintf(temp_resp, sizeof(temp_resp), "HTTP/1.1 405 Method Not Allowed\r\nServer: alligator\r\nContent-Type: application/json\r\nConnection: close\r\n\r\n{\"error\":\"use POST\"}\n");
		string_cat(response, temp_resp, strlen(temp_resp));
		return;
	}

	if (!mc)
	{
		snprintf(temp_resp, sizeof(temp_resp), "HTTP/1.1 503 Service Unavailable\r\nServer: alligator\r\nContent-Type: application/json\r\nConnection: close\r\n\r\n{\"error\":\"no metric context\"}\n");
		string_cat(response, temp_resp, strlen(temp_resp));
		return;
	}

	mc->push_accepted_lines = 0;
	mc->parser_status = 0;

	if (!body || !body_size)
	{
		snprintf(jsonbuf, sizeof(jsonbuf), "{\"linesOk\":0,\"linesInvalid\":0}\n");
		size_t jl = strlen(jsonbuf);
		snprintf(temp_resp, sizeof(temp_resp), "HTTP/1.1 202 Accepted\r\nServer: alligator\r\nContent-Type: application/json\r\nConnection: close\r\nContent-Length: %zu\r\n\r\n%s", jl, jsonbuf);
		carglog(mc, L_INFO, "dynatrace_metrics_ingest: no lines\n");
		string_cat(response, temp_resp, strlen(temp_resp));
		return;
	}

	while (cnt < (int64_t)body_size)
	{
		size_t left = body_size - (size_t)cnt;
		const char *nl = memchr(body + cnt, '\n', left);
		size_t raw_len = nl ? (size_t)(nl - (body + cnt)) : left;
		size_t ksz = raw_len;
		if (ksz >= DT_LINE_BUF)
			ksz = DT_LINE_BUF - 1;
		memcpy(line, body + cnt, ksz);
		line[ksz] = '\0';
		cnt += (int64_t)(nl ? (raw_len + 1) : raw_len);

		size_t linelen = ksz;
		dt_rtrim(line, &linelen);
		if (!linelen || line[0] == '#')
			continue;

		n_nonempty++;
		uint8_t rc = dynatrace_ingest_parse_line(line, linelen, mc);
		if (rc)
		{
			lines_ok++;
			mc->push_accepted_lines++;
			mc->parser_status = 1;
		}
		else
		{
			lines_invalid++;
			mc->parser_status = 0;
		}
	}

	if (n_nonempty == 0)
	{
		snprintf(jsonbuf, sizeof(jsonbuf), "{\"linesOk\":0,\"linesInvalid\":0}\n");
		carglog(mc, L_ERROR, "dynatrace_metrics_ingest: no lines\n");
	}
	else
	{
		snprintf(jsonbuf, sizeof(jsonbuf), "{\"linesOk\":%"u64",\"linesInvalid\":%"u64"}\n", lines_ok, lines_invalid);
		carglog(mc, L_INFO, "dynatrace_metrics_ingest: linesOk=%"u64", linesInvalid=%"u64"\n", lines_ok, lines_invalid);
	}

	uint16_t code = lines_invalid ? 400 : 202;
	const char *status = lines_invalid ? "Bad Request" : "Accepted";
	size_t jl = strlen(jsonbuf);
	snprintf(temp_resp, sizeof(temp_resp), 	"HTTP/1.1 %"u16" %s\r\nServer: alligator\r\nContent-Type: application/json\r\nConnection: close\r\nContent-Length: %zu\r\n\r\n%s", code, status, jl, jsonbuf);
	string_cat(response, temp_resp, strlen(temp_resp));

	carglog(mc, L_INFO, "dynatrace_metrics_ingest: linesOk=%"u64", linesInvalid=%"u64"\n", lines_ok, lines_invalid);
}

// https://docs.dynatrace.com/docs/discover-dynatrace/references/dynatrace-api/environment-api/metric-v2/post-ingest-metrics
void dynatrace_response_catch(char *metrics, size_t size, context_arg *carg)
{
	if (!metrics || !size)
	{
		carglog(carg, L_ERROR, "dynatrace_response_catch: empty response body\n");
		return;
	}

	json_error_t jerr;
	json_t *root = json_loadb(metrics, size, 0, &jerr);
	if (!root)
	{
		carglog(carg, L_ERROR, "dynatrace_response_catch: json error on line %d: %s\n", jerr.line, jerr.text);
		return;
	}

	json_t *jlinesOk = json_object_get(root, "linesOk");
	json_t *jlinesInvalid = json_object_get(root, "linesInvalid");
	if (jlinesOk || jlinesInvalid)
	{
		json_int_t linesOk = json_is_integer(jlinesOk) ? json_integer_value(jlinesOk) : 0;
		json_int_t linesInvalid = json_is_integer(jlinesInvalid) ? json_integer_value(jlinesInvalid) : 0;

		carglog(carg, L_INFO, "dynatrace metrics ingest: linesOk=%" JSON_INTEGER_FORMAT ", linesInvalid=%" JSON_INTEGER_FORMAT "\n", linesOk, linesInvalid);

		json_t *jmetricErr = json_object_get(root, "error");
		if (jmetricErr && json_is_object(jmetricErr))
		{
			const char *emsg = json_string_value(json_object_get(jmetricErr, "message"));
			if (emsg)
				carglog(carg, L_ERROR, "dynatrace metrics ingest: %s\n", emsg);

			json_t *invLines = json_object_get(jmetricErr, "invalidLines");
			if (invLines && json_is_array(invLines))
			{
				size_t n = json_array_size(invLines);
				for (size_t i = 0; i < n; i++)
				{
					json_t *line = json_array_get(invLines, i);
					if (!json_is_object(line))
						continue;
					json_t *jl = json_object_get(line, "line");
					const char *lerr = json_string_value(json_object_get(line, "error"));
					json_int_t ln = json_is_integer(jl) ? json_integer_value(jl) : -1;
					carglog(carg, L_ERROR, "dynatrace metrics ingest: line %" JSON_INTEGER_FORMAT ": %s\n", ln, lerr ? lerr : "(no detail)");
				}
			}
		}

		json_t *jwarn = json_object_get(root, "warnings");
		if (jwarn && json_is_object(jwarn))
		{
			const char *wmsg = json_string_value(json_object_get(jwarn, "message"));
			if (wmsg)
				carglog(carg, L_WARN, "dynatrace metrics ingest warning: %s\n", wmsg);
		}

		carg->parser_status = (linesInvalid == 0) ? 1 : 0;
		json_decref(root);
		return;
	}

	json_t *jouter = json_object_get(root, "error");
	if (json_is_object(jouter))
	{
		const char *msg = json_string_value(json_object_get(jouter, "message"));
		json_t *jcode = json_object_get(jouter, "code");
		json_int_t code = json_is_integer(jcode) ? json_integer_value(jcode) : 0;
		carglog(carg, L_ERROR, "dynatrace metrics ingest failed: code=%" JSON_INTEGER_FORMAT " %s\n",
			code, msg ? msg : "");
		json_decref(root);
		return;
	}
	if (json_is_string(jouter))
	{
		carglog(carg, L_ERROR, "dynatrace metrics ingest failed: %s\n", json_string_value(jouter));
		json_decref(root);
		return;
	}

	carglog(carg, L_ERROR, "dynatrace_response_catch: unexpected response (%zu bytes)\n", size);
	json_decref(root);
}
