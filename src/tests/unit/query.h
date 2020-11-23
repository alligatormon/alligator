#include "query/query.h"
#include "query/promql.h"
#include "common/selector.h"

#define QUERY_STR_TEST_1 " sum      (socket_stat {state=\"listen\", proto=\"tcp\", src=\"0.0.0.0\", process=\"alligator\", src_port=\"1111\", dst=\"0.0.0.0\"})"
#define QUERY_EXPECT_TEST_1 "socket_stat"
#define QUERY_FUNC_TEST_1 QUERY_FUNC_SUM

#define QUERY_STR_TEST_2 "sum(socket_stat {state=\"listen\", proto=\"tcp\", src=\"0.0.0.0\", process=\"alligator\", src_port=\"1111\", dst=\"0.0.0.0\"})"
#define QUERY_EXPECT_TEST_2 "socket_stat"
#define QUERY_FUNC_TEST_2 QUERY_FUNC_SUM

#define QUERY_STR_TEST_3 "count( 	  metric_name	 {  state=\"listen\", proto=\"tcp\", src=\"0.0.0.0\", process=\"alligator\", src_port=\"1111\", dst=\"0.0.0.0\"})"
#define QUERY_EXPECT_TEST_3 "metric_name"
#define QUERY_FUNC_TEST_3 QUERY_FUNC_COUNT

#define QUERY_STR_TEST_4 " 	 count 	 ( 	  metric_name	 {  state=\"listen\"  	,	  proto=\"tcp\",	  src=\"0.0.0.0\", 	 process=\"alligator\",	  src_port=\"1111\",	  dst=\"0.0.0.0\"  	  }   		)   "
#define QUERY_EXPECT_TEST_4 "metric_name"
#define QUERY_FUNC_TEST_4 QUERY_FUNC_COUNT

#define QUERY_STR_TEST_5 "sum by (src_port) (socket_stat)"
#define QUERY_EXPECT_TEST_5 "socket_stat"
#define QUERY_FUNC_TEST_5 QUERY_FUNC_SUM

#define QUERY_STR_TEST_6 "avg by (src_port) (socket_stat)"
#define QUERY_EXPECT_TEST_6 "socket_stat"
#define QUERY_FUNC_TEST_6 QUERY_FUNC_AVG

#define QUERY_STR_TEST_7 "present(socket_stat)"
#define QUERY_EXPECT_TEST_7 "socket_stat"
#define QUERY_FUNC_TEST_7 QUERY_FUNC_PRESENT

#define QUERY_STR_TEST_8 "sum (network_stat) by (proto)"
#define QUERY_EXPECT_TEST_8 "network_stat"
#define QUERY_FUNC_TEST_8 QUERY_FUNC_SUM

#define QUERY_STR_TEST_9 "count (network_stat) by (proto)"
#define QUERY_EXPECT_TEST_9 "network_stat"
#define QUERY_FUNC_TEST_9 QUERY_FUNC_COUNT


void test_query()
{
	char *name = malloc(255);
	string *groupkey = string_new();
	int func;

	//printf("check promql: %s\n", QUERY_STR_TEST_1);
	promql_parser(NULL, QUERY_STR_TEST_1, strlen(QUERY_STR_TEST_1), name, &func, groupkey);
	cut_assert_equal_string(QUERY_EXPECT_TEST_1, name);
	cut_assert_equal_int(0, groupkey->l);
	cut_assert_equal_int(QUERY_FUNC_TEST_1, func);
	string_null(groupkey);

	//printf("check promql: %s\n", QUERY_STR_TEST_2);
	promql_parser(NULL, QUERY_STR_TEST_2, strlen(QUERY_STR_TEST_2), name, &func, groupkey);
	cut_assert_equal_string(QUERY_EXPECT_TEST_2, name);
	cut_assert_equal_int(0, groupkey->l);
	cut_assert_equal_int(QUERY_FUNC_TEST_2, func);
	string_null(groupkey);

	//printf("check promql: %s\n", QUERY_STR_TEST_3);
	promql_parser(NULL, QUERY_STR_TEST_3, strlen(QUERY_STR_TEST_3), name, &func, groupkey);
	cut_assert_equal_string(QUERY_EXPECT_TEST_3, name);
	cut_assert_equal_int(0, groupkey->l);
	cut_assert_equal_int(QUERY_FUNC_TEST_3, func);
	string_null(groupkey);

	//printf("check promql: %s\n", QUERY_STR_TEST_4);
	promql_parser(NULL, QUERY_STR_TEST_4, strlen(QUERY_STR_TEST_4), name, &func, groupkey);
	cut_assert_equal_string(QUERY_EXPECT_TEST_4, name);
	cut_assert_equal_int(0, groupkey->l);
	cut_assert_equal_int(QUERY_FUNC_TEST_4, func);
	string_null(groupkey);

	//printf("check promql: %s\n", QUERY_STR_TEST_5);
	promql_parser(NULL, QUERY_STR_TEST_5, strlen(QUERY_STR_TEST_5), name, &func, groupkey);
	cut_assert_equal_string(QUERY_EXPECT_TEST_5, name);
	cut_assert_equal_int(9, groupkey->l);
	cut_assert_equal_string("src_port", groupkey->s);
	cut_assert_equal_int(QUERY_FUNC_TEST_5, func);
	string_null(groupkey);

	//printf("check promql: %s\n", QUERY_STR_TEST_6);
	promql_parser(NULL, QUERY_STR_TEST_6, strlen(QUERY_STR_TEST_6), name, &func, groupkey);
	cut_assert_equal_string(QUERY_EXPECT_TEST_6, name);
	cut_assert_equal_int(9, groupkey->l);
	cut_assert_equal_string("src_port", groupkey->s);
	cut_assert_equal_int(QUERY_FUNC_TEST_6, func);
	string_null(groupkey);

	//printf("check promql: %s\n", QUERY_STR_TEST_7);
	promql_parser(NULL, QUERY_STR_TEST_7, strlen(QUERY_STR_TEST_7), name, &func, groupkey);
	cut_assert_equal_string(QUERY_EXPECT_TEST_7, name);
	cut_assert_equal_int(0, groupkey->l);
	cut_assert_equal_int(QUERY_FUNC_TEST_7, func);
	string_null(groupkey);

	promql_parser(NULL, QUERY_STR_TEST_8, strlen(QUERY_STR_TEST_8), name, &func, groupkey);
	cut_assert_equal_string(QUERY_EXPECT_TEST_8, name);
	cut_assert_equal_int(6, groupkey->l);
	cut_assert_equal_string("proto", groupkey->s);
	cut_assert_equal_int(QUERY_FUNC_TEST_8, func);
	string_null(groupkey);

	promql_parser(NULL, QUERY_STR_TEST_9, strlen(QUERY_STR_TEST_9), name, &func, groupkey);
	cut_assert_equal_string(QUERY_EXPECT_TEST_9, name);
	cut_assert_equal_int(6, groupkey->l);
	cut_assert_equal_string("proto", groupkey->s);
	cut_assert_equal_int(QUERY_FUNC_TEST_9, func);
	string_null(groupkey);

	string_free(groupkey);
	free(name);
}
