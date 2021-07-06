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
	//printf("check promql: %s\n", QUERY_STR_TEST_1);
	metric_query_context *mqc = promql_parser(NULL, QUERY_STR_TEST_1, strlen(QUERY_STR_TEST_1));
	cut_assert_equal_string(QUERY_EXPECT_TEST_1, mqc->name);
	cut_assert_equal_int(0, mqc->groupkey->l);
	cut_assert_equal_int(QUERY_FUNC_TEST_1, mqc->func);
	string_null(mqc->groupkey);

	//printf("check promql: %s\n", QUERY_STR_TEST_2);
	mqc = promql_parser(NULL, QUERY_STR_TEST_2, strlen(QUERY_STR_TEST_2));
	cut_assert_equal_string(QUERY_EXPECT_TEST_2, mqc->name);
	cut_assert_equal_int(0, mqc->groupkey->l);
	cut_assert_equal_int(QUERY_FUNC_TEST_2, mqc->func);
	string_null(mqc->groupkey);

	//printf("check promql: %s\n", QUERY_STR_TEST_3);
	mqc = promql_parser(NULL, QUERY_STR_TEST_3, strlen(QUERY_STR_TEST_3));
	cut_assert_equal_string(QUERY_EXPECT_TEST_3, mqc->name);
	cut_assert_equal_int(0, mqc->groupkey->l);
	cut_assert_equal_int(QUERY_FUNC_TEST_3, mqc->func);
	string_null(mqc->groupkey);

	//printf("check promql: %s\n", QUERY_STR_TEST_4);
	mqc = promql_parser(NULL, QUERY_STR_TEST_4, strlen(QUERY_STR_TEST_4));
	cut_assert_equal_string(QUERY_EXPECT_TEST_4, mqc->name);
	cut_assert_equal_int(0, mqc->groupkey->l);
	cut_assert_equal_int(QUERY_FUNC_TEST_4, mqc->func);
	string_null(mqc->groupkey);

	//printf("check promql: %s\n", QUERY_STR_TEST_5);
	mqc = promql_parser(NULL, QUERY_STR_TEST_5, strlen(QUERY_STR_TEST_5));
	cut_assert_equal_string(QUERY_EXPECT_TEST_5, mqc->name);
	cut_assert_equal_int(9, mqc->groupkey->l);
	cut_assert_equal_string("src_port", mqc->groupkey->s);
	cut_assert_equal_int(QUERY_FUNC_TEST_5, mqc->func);
	string_null(mqc->groupkey);

	//printf("check promql: %s\n", QUERY_STR_TEST_6);
	mqc = promql_parser(NULL, QUERY_STR_TEST_6, strlen(QUERY_STR_TEST_6));
	cut_assert_equal_string(QUERY_EXPECT_TEST_6, mqc->name);
	cut_assert_equal_int(9, mqc->groupkey->l);
	cut_assert_equal_string("src_port", mqc->groupkey->s);
	cut_assert_equal_int(QUERY_FUNC_TEST_6, mqc->func);
	string_null(mqc->groupkey);

	//printf("check promql: %s\n", QUERY_STR_TEST_7);
	mqc = promql_parser(NULL, QUERY_STR_TEST_7, strlen(QUERY_STR_TEST_7));
	cut_assert_equal_string(QUERY_EXPECT_TEST_7, mqc->name);
	cut_assert_equal_int(0, mqc->groupkey->l);
	cut_assert_equal_int(QUERY_FUNC_TEST_7, mqc->func);
	string_null(mqc->groupkey);

	mqc = promql_parser(NULL, QUERY_STR_TEST_8, strlen(QUERY_STR_TEST_8));
	cut_assert_equal_string(QUERY_EXPECT_TEST_8, mqc->name);
	cut_assert_equal_int(6, mqc->groupkey->l);
	cut_assert_equal_string("proto", mqc->groupkey->s);
	cut_assert_equal_int(QUERY_FUNC_TEST_8, mqc->func);
	string_null(mqc->groupkey);

	mqc = promql_parser(NULL, QUERY_STR_TEST_9, strlen(QUERY_STR_TEST_9));
	cut_assert_equal_string(QUERY_EXPECT_TEST_9, mqc->name);
	cut_assert_equal_int(6, mqc->groupkey->l);
	cut_assert_equal_string("proto", mqc->groupkey->s);
	cut_assert_equal_int(QUERY_FUNC_TEST_9, mqc->func);
	string_null(mqc->groupkey);
}
