#include "common/json_query.h"

/* json_query: label extraction + nested metrics */
void test_json_query_labels_and_int(void)
{
	char *json = "[{\"ts\":\"t1\",\"rows\":[{\"id\":\"a\",\"count\":7}]}]";
	char *pquery_str = ".[ts].rows.[id]";
	char *pquery[] = { pquery_str };
	context_arg *carg = calloc(1, sizeof(*carg));
	carg->key = "json_query_ut";
	carg->pquery = pquery;
	carg->pquery_size = 1;

	assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 1,
	    json_query(json, NULL, "json", carg, carg->pquery, carg->pquery_size));

	metric_test_run(CMP_EQUAL, "json_rows_count", "json_rows_count", 7);
	free(carg);
}

/* jq-style pipe: same idea as system test .[timestamp].data | .[type] */
void test_json_query_pipe_stages(void)
{
	char *json = "[{\"timestamp\":\"12-04-92\",\"data\":[{\"type\":\"articles\",\"id\":\"1\"}]}]";
	char *pquery_str = ".[timestamp].data | .[type]";
	char *pquery[] = { pquery_str };
	context_arg *carg = calloc(1, sizeof(*carg));
	carg->key = "json_query_ut";
	carg->pquery = pquery;
	carg->pquery_size = 1;

	assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 1,
	    json_query(json, NULL, "json", carg, carg->pquery, carg->pquery_size));

	/* string leaf -> uint gauge 1 per json_parse_string */
	metric_test_run(CMP_EQUAL, "json_data_id", "json_data_id", 1);
	metric_test_run(CMP_EQUAL, "json_data_type", "json_data_type", 1);
	free(carg);
}

/* [field:label] renames Prometheus label; metric path unchanged */
void test_json_query_label_alias(void)
{
	char *json = "[{\"items\":[{\"uid\":\"z9\",\"score\":9}]}]";
	char *pquery_str = ".items.[uid:entity_id]";
	char *pquery[] = { pquery_str };
	context_arg *carg = calloc(1, sizeof(*carg));
	carg->key = "json_query_ut";
	carg->pquery = pquery;
	carg->pquery_size = 1;

	assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 1,
	    json_query(json, NULL, "json", carg, carg->pquery, carg->pquery_size));

	metric_test_run(CMP_EQUAL, "json_items_score", "json_items_score", 9);
	free(carg);
}

/* comma branches: two sibling arrays under same timestamp label stage */
void test_json_query_comma_branches(void)
{
	char *json = "[{\"ts\":\"t1\",\"items\":[{\"id\":\"1\",\"n\":1}],\"meta\":[{\"tag\":\"x\",\"m\":2}]}]";
	char *pquery_str = ".[ts] | .items, .meta | .[id tag]";
	char *pquery[] = { pquery_str };
	context_arg *carg = calloc(1, sizeof(*carg));
	carg->key = "json_query_ut";
	carg->pquery = pquery;
	carg->pquery_size = 1;

	assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 1,
	    json_query(json, NULL, "json", carg, carg->pquery, carg->pquery_size));

	metric_test_run(CMP_EQUAL, "json_items_n", "json_items_n", 1);
	metric_test_run(CMP_EQUAL, "json_meta_m", "json_meta_m", 2);
	free(carg);
}

/* two pquery entries merge label rules for the same paths */
void test_json_query_merge_two_pquery(void)
{
	char *json = "[{\"ts\":\"t1\",\"items\":[{\"id\":\"a\",\"count\":3,\"name\":\"n1\"}]}]";
	char *q0 = ".[ts].items.[id]";
	char *q1 = ".[ts].items.[name]";
	char *pquery[] = { q0, q1 };
	context_arg *carg = calloc(1, sizeof(*carg));
	carg->key = "json_query_ut";
	carg->pquery = pquery;
	carg->pquery_size = 2;

	assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 1,
	    json_query(json, NULL, "json", carg, carg->pquery, carg->pquery_size));

	metric_test_run(CMP_EQUAL, "json_items_count", "json_items_count", 3);
	free(carg);
}
