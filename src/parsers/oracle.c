#include <stdio.h>
#include <stdlib.h>
#include <libpq-fe.h>
#include <sys/time.h>
#include <sys/types.h>
#include <unistd.h>
#include <string.h>
#include <query/query.h>
#include "events/context_arg.h"
#include <main.h>
#define ORACLE_PROMPT "SQL> SQL> SQL> SQL> SQL> SQL> "

void oracle_get_databases(context_arg *carg)
{
}

void oracle_query_run(char *metrics, size_t size, context_arg *carg)
{
	if (carg->log_level > 0)
		puts("===== oracle_query_run ======");

	char *tmp = strstr(metrics, ORACLE_PROMPT);
	if (!tmp)
		return;
	uint64_t cur = tmp - metrics + strlen(ORACLE_PROMPT);

	//if (strcmp(carg->key, "col_stats"))
	//	return;

	query_ds *qds = query_get(carg->name);
	query_node *qn = query_get_node(qds, carg->key);
	//printf("found queries for datasource: %s: %p/%p %s/%s\n", carg->name, qds, qn, carg->name, carg->key);

	// max columns per table 1000: https://docs.oracle.com/en/database/oracle/oracle-database/19/refrn/logical-database-limits.html
	char colname[1000][31];

	uint64_t end = strcspn(metrics + cur, "\n") + cur;
	//printf("cur: %"PRIu64", end: %"PRIu64", metrics: '%s'\n", cur, end, metrics);
	char colstring[end + 1];
	strlcpy(colstring, metrics + cur, end + 1);

	uint64_t i;
	for (i = 0; cur < end; cur++, i++)
	{
		uint64_t endfield;
		uint64_t copysize;

		cur += strspn(metrics + cur, "\t |");
		endfield = strcspn(metrics + cur, "|\n");
		copysize = strcspn(metrics + cur, "\t |\n");
		strlcpy(colname[i], metrics + cur, copysize + 1);

		if (carg->log_level > 0)
			printf("(%s) found column name: '%s'[%"u64"]\n", colstring, colname[i], i);

		cur += endfield;
	}
	if (!i)
		return;

	// skip terminate string
	cur += strspn(metrics + cur, "\n");
	cur += strcspn(metrics + cur, "\n");
	cur += strspn(metrics + cur, "\n");

	char *tssize = strstr(metrics + cur, "\n\n");
	if (!tssize)
		return;
	size_t ssize = tssize ? (tssize - metrics) : 0;
	for (; cur < ssize; cur++)
	{
		end = strcspn(metrics + cur, "\n");
		uint64_t endcur = end + cur;

		uint64_t ccur = cur;

		alligator_ht *hash = alligator_ht_init(NULL);

		if (carg->ns)
			labels_hash_insert_nocache(hash, "dbname", carg->ns);

		for (uint64_t i = 0; ccur < endcur; ccur++, i++)
		{
			uint64_t endfield;
			uint64_t copysize;
			char colvalue[1024];

			ccur += strspn(metrics + ccur, "\t ");
			endfield = strcspn(metrics + ccur, "|");
			copysize = strcspn(metrics + ccur, "\t|\n");
			copysize = copysize > 1024 ? 1023 : copysize;
			strlcpy(colvalue, metrics + ccur, copysize + 1);
			trim(colvalue);

			if (strstr(colvalue, "---")) {
				break;
			}
			if (carg->log_level > 1)
				printf("column name: '%s'[%"u64"], column value: '%s'\n", colname[i], i, colvalue);

			char *clname = colname[i];
			if (!*clname)
				continue;
			if (metric_value_validator(colvalue, copysize) == DATATYPE_DOUBLE)
			{
				double dres = strtod(colvalue, NULL);
				query_field *qf = query_field_get(qn->qf_hash, clname);
				if (qf)
				{
					if (carg->log_level > 2)
						printf("\tvalue '%s'\n", colvalue);
					qf->d = dres;

					qf->type = DATATYPE_DOUBLE;
				}
				else
				{
					if (carg->log_level > 2)
						printf("\tfield '%s': '%s'\n", clname, colvalue);
					labels_hash_insert_nocache(hash, clname, colvalue);
				}
			}
			else
			{
				query_field *qf = query_field_get(qn->qf_hash, clname);
				if (qf)
				{
					if (carg->log_level > 2)
						printf("\tvalue '%s'\n", colvalue);
					qf->i = strtoll(colvalue, NULL, 10);
					qf->type = DATATYPE_INT;
				}
				else
				{
					if (carg->log_level > 2)
						printf("\tfield '%s': '%s'\n", clname, colvalue);
					labels_hash_insert_nocache(hash, clname, colvalue);
				}
			}

			ccur += endfield;
		}

		qn->labels = hash;
		qn->carg = carg;
		query_set_values(qn);
		labels_hash_free(hash);

		cur += end;
	}

	if (carg->log_level > 0)
		puts("----- oracle_query_run ------");
	carg->parser_status = 1;
}

void oracle_queries_foreach(void *funcarg, void* arg)
{
	context_arg *carg = (context_arg*)funcarg;
	query_node *qn = arg;

	string *str = string_init(1024);
	string_cat(str, carg->user, strlen(carg->user));
	string_cat(str, "/", 1);
	string_cat(str, carg->password, strlen(carg->password));
	string_cat(str, "@$ORACLE_SID", strlen("@$ORACLE_SID"));
	string_cat(str, " <<EOF\n", strlen(" <<EOF\n"));

	string_cat(str, "SET HEADING ON\n", strlen("SET HEADING ON\n"));
	string_cat(str, "SET WRAP OFF\n", strlen("SET WRAP OFF\n"));
	string_cat(str, "SET LINESIZE 32767\n", strlen("SET LINESIZE 32767\n"));
	string_cat(str, "SET COLSEP |\n", strlen("SET COLSEP |\n"));
	string_cat(str, "SET PAGESIZE 0 EMBEDDED ON\n", strlen("SET PAGESIZE 0 EMBEDDED ON\n"));

	string_cat(str, qn->expr, strlen(qn->expr));
	string_cat(str, ";\nquit\nEOF\n", strlen(";\nquit\nEOF\n"));

	if (carg->log_level > 1)
	{
		puts("+-+-+-+-+-+-+-+");
		printf("run datasource '%s', make '%s': '%s'\n", qn->datasource, qn->make, qn->expr);
	}

	aggregator_oneshot(carg, carg->url, strlen(carg->url), str->s, str->l, oracle_query_run, "oracle_query", NULL, strdup(qn->make), 0, NULL, NULL, 0, NULL);
	free(str);
	carg->parser_status = 1;
}

void oracle_sql_generator(char *metrics, size_t size, context_arg *carg)
{
	if (carg->name)
	{
		query_ds *qds = query_get(carg->name);
		if (carg->log_level > 1)
			printf("found queries for datasource: %s: %p\n", carg->name, qds);
		if (qds)
		{
			alligator_ht_foreach_arg(qds->hash, oracle_queries_foreach, carg);
		}
	}

	if (!carg->data_lock)
		oracle_get_databases(carg);
	carg->parser_status = 1;
}

string* oracle_mesg(host_aggregator_info *hi, void *arg, void *env, void *proxy_settings)
{
	return string_init_alloc("-V", 0);
}

void oracle_parser_push()
{
	aggregate_context *actx = calloc(1, sizeof(*actx));

	actx->key = strdup("oracle");
	actx->handlers = 1;
	actx->handler = calloc(1, sizeof(*actx->handler)*actx->handlers);

	actx->handler[0].name = oracle_sql_generator;
	actx->handler[0].validator = NULL;
	actx->handler[0].mesg_func = oracle_mesg;
	strlcpy(actx->handler[0].key,"oracle_sql_generator", 255);

	alligator_ht_insert(ac->aggregate_ctx, &(actx->node), actx, tommy_strhash_u32(0, actx->key));
}

string* oracle_query_mesg(host_aggregator_info *hi, void *arg, void *env, void *proxy_settings)
{
	return string_init_alloc("", 0);
}

void oracle_query_parser_push()
{
	aggregate_context *actx = calloc(1, sizeof(*actx));

	actx->key = strdup("oracle_query");
	actx->handlers = 1;
	actx->handler = calloc(1, sizeof(*actx->handler)*actx->handlers);

	actx->handler[0].name = oracle_query_run;
	actx->handler[0].validator = NULL;
	actx->handler[0].mesg_func = oracle_query_mesg;
	strlcpy(actx->handler[0].key,"oracle_query", 255);

	alligator_ht_insert(ac->aggregate_ctx, &(actx->node), actx, tommy_strhash_u32(0, actx->key));
}
