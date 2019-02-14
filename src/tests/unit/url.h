#include "common/url.h"

#define TEST_URL_1 "tcp://example.com:25"
#define TEST_URL_HOST_1 "example.com"
#define TEST_URL_PROTO_1 APROTO_TCP
#define TEST_URL_PORT_1 "25"
void test_parse_url_1()
{
	host_aggregator_info *hi = parse_url (TEST_URL_1, strlen(TEST_URL_1));
	cut_assert_not_null(hi);
	cut_assert_equal_int(hi->proto, TEST_URL_PROTO_1);
	cut_assert_equal_string(hi->port, TEST_URL_PORT_1);
	cut_assert_equal_string(hi->host, TEST_URL_HOST_1);
}

#define TEST_URL_2 "udp://example.org:53"
#define TEST_URL_HOST_2 "example.org"
#define TEST_URL_PROTO_2 APROTO_UDP
#define TEST_URL_PORT_2 "53"
void test_parse_url_2()
{
	host_aggregator_info *hi = parse_url (TEST_URL_2, strlen(TEST_URL_2));
	cut_assert_not_null(hi);
	cut_assert_equal_int(hi->proto, TEST_URL_PROTO_2);
	cut_assert_equal_string(hi->port, TEST_URL_PORT_2);
	cut_assert_equal_string(hi->host, TEST_URL_HOST_2);
}

#define TEST_URL_3 "tcp://example.com"
void test_parse_url_3()
{
	host_aggregator_info *hi = parse_url (TEST_URL_3, strlen(TEST_URL_3));
	cut_assert_null(hi);
}
#define TEST_URL_4 "udp://example.org"
void test_parse_url_4()
{
	host_aggregator_info *hi = parse_url (TEST_URL_4, strlen(TEST_URL_4));
	cut_assert_null(hi);
}

#define TEST_URL_5 "file:///var/log/messages"
#define TEST_URL_HOST_5 "/var/log/messages"
#define TEST_URL_PROTO_5 APROTO_FILE
#define TEST_URL_PORT_5 "0"
void test_parse_url_5()
{
	host_aggregator_info *hi = parse_url (TEST_URL_5, strlen(TEST_URL_5));
	cut_assert_not_null(hi);
	cut_assert_equal_int(hi->proto, TEST_URL_PROTO_5);
	cut_assert_equal_string(hi->port, TEST_URL_PORT_5);
	cut_assert_equal_string(hi->host, TEST_URL_HOST_5);
}

#define TEST_URL_6 "file:///var/log/maillog"
#define TEST_URL_HOST_6 "/var/log/maillog"
#define TEST_URL_PROTO_6 APROTO_FILE
#define TEST_URL_PORT_6 "0"
void test_parse_url_6()
{
	host_aggregator_info *hi = parse_url (TEST_URL_6, strlen(TEST_URL_6));
	cut_assert_not_null(hi);
	cut_assert_equal_int(hi->proto, TEST_URL_PROTO_6);
	cut_assert_equal_string(hi->port, TEST_URL_PORT_6);
	cut_assert_equal_string(hi->host, TEST_URL_HOST_6);
}

#define TEST_URL_7 "http://example.com"
#define TEST_URL_HOST_7 "example.com"
#define TEST_URL_PROTO_7 APROTO_HTTP
#define TEST_URL_PORT_7 "80"
#define TEST_URL_QUERY_7 "/"
//void test_parse_url_7()
//{
//	host_aggregator_info *hi = parse_url (TEST_URL_7, strlen(TEST_URL_7));
//	cut_assert_not_null(hi);
//	cut_assert_equal_int(hi->proto, TEST_URL_PROTO_7);
//	cut_assert_equal_string(hi->port, TEST_URL_PORT_7);
//	cut_assert_equal_string(hi->host, TEST_URL_HOST_7);
//	cut_assert_equal_string(hi->query, TEST_URL_QUERY_7);
//}

#define TEST_URL_8 "http://example.com/"
#define TEST_URL_HOST_8 "example.com"
#define TEST_URL_PROTO_8 APROTO_HTTP
#define TEST_URL_PORT_8 "80"
#define TEST_URL_QUERY_8 "/"
void test_parse_url_8()
{
	host_aggregator_info *hi = parse_url (TEST_URL_8, strlen(TEST_URL_8));
	cut_assert_not_null(hi);
	cut_assert_equal_int(hi->proto, TEST_URL_PROTO_8);
	cut_assert_equal_string(hi->port, TEST_URL_PORT_8);
	cut_assert_equal_string(hi->host, TEST_URL_HOST_8);
}

#define TEST_URL_9 "http://example.com?a=1"
#define TEST_URL_HOST_9 "example.com"
#define TEST_URL_PROTO_9 APROTO_HTTP
#define TEST_URL_PORT_9 "80"
#define TEST_URL_QUERY_9 "?a=1"

#define TEST_URL_10 "http://example.com?arg_1=value_1"
#define TEST_URL_HOST_10 "example.com"
#define TEST_URL_PROTO_10 APROTO_HTTP
#define TEST_URL_PORT_10 "80"
#define TEST_URL_QUERY_10 "?arg_1=value_1"

#define TEST_URL_11 "http://example.com/?a=1"
#define TEST_URL_HOST_11 "example.com"
#define TEST_URL_PROTO_11 APROTO_HTTP
#define TEST_URL_PORT_11 "80"
#define TEST_URL_QUERY_11 "/?a=1"

#define TEST_URL_12 "http://example.com/?arg_1=value_1"
#define TEST_URL_HOST_12 "example.com"
#define TEST_URL_PROTO_12 APROTO_HTTP
#define TEST_URL_PORT_12 "80"
#define TEST_URL_QUERY_12 "/?arg_1=value_1"

#define TEST_URL_13 "http://example.com/api?a=1"
#define TEST_URL_HOST_13 "example.com"
#define TEST_URL_PROTO_13 APROTO_HTTP
#define TEST_URL_PORT_13 "80"
#define TEST_URL_QUERY_13 "/api?a=1"

#define TEST_URL_14 "http://example.com/api?arg_1=value_1"
#define TEST_URL_HOST_14 "example.com"
#define TEST_URL_PROTO_14 APROTO_HTTP
#define TEST_URL_PORT_14 "80"
#define TEST_URL_QUERY_14 "/api?arg_1=value_1"

#define TEST_URL_15 "http://example.com/api/?a=1"
#define TEST_URL_HOST_15 "example.com"
#define TEST_URL_PROTO_15 APROTO_HTTP
#define TEST_URL_PORT_15 "80"
#define TEST_URL_QUERY_15 "/api/?a=1"

#define TEST_URL_16 "http://example.com/api/?arg_1=value_1"
#define TEST_URL_HOST_16 "example.com"
#define TEST_URL_PROTO_16 APROTO_HTTP
#define TEST_URL_PORT_16 "80"
#define TEST_URL_QUERY_16 "/api/?arg_1=value_1"

#define TEST_URL_17 "http://example.com/static//assets?a=1"
#define TEST_URL_HOST_17 "example.com"
#define TEST_URL_PROTO_17 APROTO_HTTP
#define TEST_URL_PORT_17 "80"
#define TEST_URL_QUERY_17 "/static//assets?a=1"

#define TEST_URL_18 "http://example.com/static//assets?arg_1=value_1"
#define TEST_URL_HOST_18 "example.com"
#define TEST_URL_PROTO_18 APROTO_HTTP
#define TEST_URL_PORT_18 "80"
#define TEST_URL_QUERY_18 "/static//assets?arg_1=value_1"

#define TEST_URL_19 "http://example.com:23424/static//assets?a=1"
#define TEST_URL_HOST_19 "example.com"
#define TEST_URL_PROTO_19 APROTO_HTTP
#define TEST_URL_PORT_19 "23424"
#define TEST_URL_QUERY_19 "/static//assets?a=1"

#define TEST_URL_20 "http://example.com:23245/static//assets?arg_1=value_1"
#define TEST_URL_HOST_20 "example.com"
#define TEST_URL_PROTO_20 APROTO_HTTP
#define TEST_URL_PORT_20 "23245"
#define TEST_URL_QUERY_20 "/static//assets?arg_1=value_1"

#define TEST_URL_21 "https://example.com?a=1"
#define TEST_URL_HOST_21 "example.com"
#define TEST_URL_PROTO_21 APROTO_HTTPS
#define TEST_URL_PORT_21 "443"
#define TEST_URL_QUERY_21 "?a=1"

#define TEST_URL_22 "https://example.com?arg_1=value_1"
#define TEST_URL_HOST_22 "example.com"
#define TEST_URL_PROTO_22 APROTO_HTTPS
#define TEST_URL_PORT_22 "443"
#define TEST_URL_QUERY_22 "?arg_1=value_1"

#define TEST_URL_23 "https://example.com/?a=1"
#define TEST_URL_HOST_23 "example.com"
#define TEST_URL_PROTO_23 APROTO_HTTPS
#define TEST_URL_PORT_23 "443"
#define TEST_URL_QUERY_23 "/?a=1"

#define TEST_URL_24 "https://example.com/?arg_1=value_1"
#define TEST_URL_HOST_24 "example.com"
#define TEST_URL_PROTO_24 APROTO_HTTPS
#define TEST_URL_PORT_24 "443"
#define TEST_URL_QUERY_24 "/?arg_1=value_1"

#define TEST_URL_25 "https://example.com/api?a=1"
#define TEST_URL_HOST_25 "example.com"
#define TEST_URL_PROTO_25 APROTO_HTTPS
#define TEST_URL_PORT_25 "443"
#define TEST_URL_QUERY_25 "/api?a=1"

#define TEST_URL_26 "https://example.com/api?arg_1=value_1"
#define TEST_URL_HOST_26 "example.com"
#define TEST_URL_PROTO_26 APROTO_HTTPS
#define TEST_URL_PORT_26 "443"
#define TEST_URL_QUERY_26 "/api?arg_1=value_1"

#define TEST_URL_27 "https://example.com/api/?a=1"
#define TEST_URL_HOST_27 "example.com"
#define TEST_URL_PROTO_27 APROTO_HTTPS
#define TEST_URL_PORT_27 "443"
#define TEST_URL_QUERY_27 "/api/?a=1"

#define TEST_URL_28 "https://example.com/api/?arg_1=value_1"
#define TEST_URL_HOST_28 "example.com"
#define TEST_URL_PROTO_28 APROTO_HTTPS
#define TEST_URL_PORT_28 "443"
#define TEST_URL_QUERY_28 "/api/?arg_1=value_1"

#define TEST_URL_29 "https://example.com/static//assets?a=1"
#define TEST_URL_HOST_29 "example.com"
#define TEST_URL_PROTO_29 APROTO_HTTPS
#define TEST_URL_PORT_29 "443"
#define TEST_URL_QUERY_29 "/static//assets?a=1"

#define TEST_URL_30 "https://example.com/static//assets?arg_1=value_1"
#define TEST_URL_HOST_30 "example.com"
#define TEST_URL_PROTO_30 APROTO_HTTPS
#define TEST_URL_PORT_30 "443"
#define TEST_URL_QUERY_30 "/static//assets?arg_1=value_1"

#define TEST_URL_31 "https://example.com:23424/static//assets?a=1"
#define TEST_URL_HOST_31 "example.com"
#define TEST_URL_PROTO_31 APROTO_HTTPS
#define TEST_URL_PORT_31 "23424"
#define TEST_URL_QUERY_31 "/static//assets?a=1"

#define TEST_URL_32 "https://example.com:23245/static//assets?arg_1=value_1"
#define TEST_URL_HOST_32 "example.com"
#define TEST_URL_PROTO_32 APROTO_HTTPS
#define TEST_URL_PORT_32 "23245"
#define TEST_URL_QUERY_32 "/static//assets?arg_1=value_1"

#define TEST_URL_33 "http://usdfewin:dcrvtvajf@example.com/api?arg_1=value_1"
#define TEST_URL_HOST_33 "example.com"
#define TEST_URL_PROTO_33 APROTO_HTTP
#define TEST_URL_PORT_33 "80"
#define TEST_URL_QUERY_33 "/api?arg_1=value_1"
#define TEST_URL_AUTH_33 "dXNkZmV3aW46ZGNydnR2YWpm"

#define TEST_URL_34 "http://usdfewin:dcrvtvajf@example.com/api/?a=1"
#define TEST_URL_HOST_34 "example.com"
#define TEST_URL_PROTO_34 APROTO_HTTP
#define TEST_URL_PORT_34 "80"
#define TEST_URL_QUERY_34 "/api/?a=1"
#define TEST_URL_AUTH_34 "dXNkZmV3aW46ZGNydnR2YWpm"

#define TEST_URL_35 "http://usdfewin:dcrvtvajf@example.com/api/?arg_1=value_1"
#define TEST_URL_HOST_35 "example.com"
#define TEST_URL_PROTO_35 APROTO_HTTP
#define TEST_URL_PORT_35 "80"
#define TEST_URL_QUERY_35 "/api/?arg_1=value_1"
#define TEST_URL_AUTH_35 "dXNkZmV3aW46ZGNydnR2YWpm"

#define TEST_URL_36 "http://usdfewin:dcrvtvajf@example.com/static//assets?a=1"
#define TEST_URL_HOST_36 "example.com"
#define TEST_URL_PROTO_36 APROTO_HTTP
#define TEST_URL_PORT_36 "80"
#define TEST_URL_QUERY_36 "/static//assets?a=1"
#define TEST_URL_AUTH_36 "dXNkZmV3aW46ZGNydnR2YWpm"

#define TEST_URL_37 "http://usdfewin:dcrvtvajf@example.com/static//assets?arg_1=value_1"
#define TEST_URL_HOST_37 "example.com"
#define TEST_URL_PROTO_37 APROTO_HTTP
#define TEST_URL_PORT_37 "80"
#define TEST_URL_QUERY_37 "/static//assets?arg_1=value_1"
#define TEST_URL_AUTH_37 "dXNkZmV3aW46ZGNydnR2YWpm"

#define TEST_URL_38 "http://usdfewin:dcrvtvajf@example.com:23424/static//assets?a=1"
#define TEST_URL_HOST_38 "example.com"
#define TEST_URL_PROTO_38 APROTO_HTTP
#define TEST_URL_PORT_38 "80"
#define TEST_URL_QUERY_38 "/static//assets?a=1"
#define TEST_URL_AUTH_38 "dXNkZmV3aW46ZGNydnR2YWpm"

#define TEST_URL_39 "http://usdfewin:dcrvtvajf@example.com:23245/static//assets?arg_1=value_1"
#define TEST_URL_HOST_39 "example.com"
#define TEST_URL_PROTO_39 APROTO_HTTP
#define TEST_URL_PORT_39 "80"
#define TEST_URL_QUERY_39 "/static//assets?arg_1=value_1"
#define TEST_URL_AUTH_39 "dXNkZmV3aW46ZGNydnR2YWpm"

#define TEST_URL_40 "https://crfvcgtivn:fcrtvtv@example.com?a=1"
#define TEST_URL_HOST_40 "example.com"
#define TEST_URL_PROTO_40 APROTO_HTTPS
#define TEST_URL_PORT_40 "443"
#define TEST_URL_QUERY_40 "?a=1"
#define TEST_URL_AUTH_40 "Y3JmdmNndGl2bjpmY3J0dnR2"

#define TEST_URL_41 "https://crfvcgtivn:fcrtvtv@example.com?arg_1=value_1"
#define TEST_URL_HOST_41 "example.com"
#define TEST_URL_PROTO_41 APROTO_HTTPS
#define TEST_URL_PORT_41 "443"
#define TEST_URL_QUERY_41 "?arg_1=value_1"
#define TEST_URL_AUTH_41 "Y3JmdmNndGl2bjpmY3J0dnR2"

#define TEST_URL_42 "https://crfvcgtivn:fcrtvtv@example.com/?a=1"
#define TEST_URL_HOST_42 "example.com"
#define TEST_URL_PROTO_42 APROTO_HTTPS
#define TEST_URL_PORT_42 "443"
#define TEST_URL_QUERY_42 "/?a=1"
#define TEST_URL_AUTH_42 "Y3JmdmNndGl2bjpmY3J0dnR2"

#define TEST_URL_43 "https://crfvcgtivn:fcrtvtv@example.com:64012/?a=1"
#define TEST_URL_HOST_43 "example.com"
#define TEST_URL_PROTO_43 APROTO_HTTPS
#define TEST_URL_PORT_43 "64012"
#define TEST_URL_QUERY_43 "/?a=1"
#define TEST_URL_AUTH_43 "Y3JmdmNndGl2bjpmY3J0dnR2"


