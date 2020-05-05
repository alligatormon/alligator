#include "common/url.h"

void test_parse_url()
{
	int test_url_size = 26;
	char **test_url = calloc(test_url_size, sizeof(void*));
	char **test_url_host = calloc(test_url_size, sizeof(void*));
	uint8_t *test_url_proto = calloc(test_url_size, sizeof(void*));
	uint8_t *test_url_transport = calloc(test_url_size, sizeof(void*));
	char **test_url_port = calloc(test_url_size, sizeof(void*));
	char **test_url_query = calloc(test_url_size, sizeof(void*));
	char **test_url_auth = calloc(test_url_size, sizeof(void*));
	char **test_url_user = calloc(test_url_size, sizeof(void*));
	char **test_url_pass = calloc(test_url_size, sizeof(void*));
	char **test_url_host_header = calloc(test_url_size, sizeof(void*));
	test_url[0] = "https://crfvcgtivn:fcrtvtv@example.com/?query=a:{a-1}&pretty";
	test_url_host[0] = "example.com";
	test_url_proto[0] = APROTO_HTTPS;
	test_url_transport[0] = APROTO_TLS;
	test_url_port[0] = "443";
	test_url_query[0] = "/?query=a:{a-1}&pretty";
	test_url_auth[0] = "Y3JmdmNndGl2bjpmY3J0dnR2";

	test_url[1] = "https://crfvcgtivn:fcrtvtv@example.com:64012/?a=1";
	test_url_host[1] = "example.com";
	test_url_proto[1] = APROTO_HTTPS;
	test_url_transport[1] = APROTO_TLS;
	test_url_port[1] = "64012";
	test_url_query[1] = "/?a=1";
	test_url_auth[1] = "Y3JmdmNndGl2bjpmY3J0dnR2";

	test_url[2] = "https://crfvcgtivn:fcrtvtv@example.com/?a=1";
	test_url_host[2] = "example.com";
	test_url_proto[2] = APROTO_HTTPS;
	test_url_transport[2] = APROTO_TLS;
	test_url_port[2] = "443";
	test_url_query[2] = "/?a=1";
	test_url_auth[2] = "Y3JmdmNndGl2bjpmY3J0dnR2";

	test_url[3] = "http://usdfewin:dcrvtvajf@example.com:23245/static//assets?arg_1=value_1";
	test_url_host[3] = "example.com";
	test_url_proto[3] = APROTO_HTTP;
	test_url_transport[3] = APROTO_TCP;
	test_url_port[3] = "23245";
	test_url_query[3] = "/static//assets?arg_1=value_1";
	test_url_auth[3] = "dXNkZmV3aW46ZGNydnR2YWpm";

	test_url[4] = "https://crfvcgtivn:fcrtvtv@example.com?a=1";
	test_url_host[4] = "example.com";
	test_url_proto[4] = APROTO_HTTPS;
	test_url_transport[4] = APROTO_TLS;
	test_url_port[4] = "443";
	test_url_query[4] = "?a=1";
	test_url_auth[4] = "Y3JmdmNndGl2bjpmY3J0dnR2";

	test_url[5] = "https://crfvcgtivn:fcrtvtv@example.com?arg_1=value_1";
	test_url_host[5] = "example.com";
	test_url_proto[5] = APROTO_HTTPS;
	test_url_transport[5] = APROTO_TLS;
	test_url_port[5] = "443";
	test_url_query[5] = "?arg_1=value_1";
	test_url_auth[5] = "Y3JmdmNndGl2bjpmY3J0dnR2";

	test_url[6] = "http://usdfewin:dcrvtvajf@Llanfairpwllgwyngyllgogerychwyrndrobwllllantysiliogogogoch.com:23424/static//assets?a=1";
	test_url_host[6] = "Llanfairpwllgwyngyllgogerychwyrndrobwllllantysiliogogogoch.com";
	test_url_proto[6] = APROTO_HTTP;
	test_url_transport[6] = APROTO_TCP;
	test_url_port[6] = "23424";
	test_url_query[6] = "/static//assets?a=1";
	test_url_auth[6] = "dXNkZmV3aW46ZGNydnR2YWpm";

	test_url[7] = "http://usdfewin:dcrvtvajf@example.com/static//assets?arg_1=value_1";
	test_url_host[7] = "example.com";
	test_url_proto[7] = APROTO_HTTP;
	test_url_transport[7] = APROTO_TCP;
	test_url_port[7] = "80";
	test_url_query[7] = "/static//assets?arg_1=value_1";
	test_url_auth[7] = "dXNkZmV3aW46ZGNydnR2YWpm";

	test_url[8] = "http://usdfewin:dcrvtvajf@example.com/static//assets?a=1";
	test_url_host[8] = "example.com";
	test_url_proto[8] = APROTO_HTTP;
	test_url_transport[8] = APROTO_TCP;
	test_url_port[8] = "80";
	test_url_query[8] = "/static//assets?a=1";
	test_url_auth[8] = "dXNkZmV3aW46ZGNydnR2YWpm";

	test_url[9] = "http://example.com/";
	test_url_host[9] = "example.com";
	test_url_proto[9] = APROTO_HTTP;
	test_url_transport[9] = APROTO_TCP;
	test_url_port[9] = "80";
	test_url_query[9] = "/";

	test_url[10] = "http://usdfewin:dcrvtvajf@example.com/static//assets?arg_1=value_1";
	test_url_host[10] = "example.com";
	test_url_proto[10] = APROTO_HTTP;
	test_url_transport[10] = APROTO_TCP;
	test_url_port[10] = "80";
	test_url_query[10] = "/static//assets?arg_1=value_1";

	test_url[11] = "http://usdfewin:dcrvtvajf@example.com/api?arg_1=value_1";
	test_url_host[11] = "example.com";
	test_url_proto[11] = APROTO_HTTP;
	test_url_transport[11] = APROTO_TCP;
	test_url_port[11] = "80";
	test_url_query[11] = "/api?arg_1=value_1";

	test_url[12] = "http://usdfewin:dcrvtvajf@ex-aed1m123ple.2com/api/?a=1";
	test_url_host[12] = "ex-aed1m123ple.2com";
	test_url_proto[12] = APROTO_HTTP;
	test_url_transport[12] = APROTO_TCP;
	test_url_port[12] = "80";
	test_url_query[12] = "/api/?a=1";


	test_url[13] = "https://example.com/?arg_1=value_1";
	test_url_host[13] = "example.com";
	test_url_proto[13] = APROTO_HTTPS;
	test_url_transport[13] = APROTO_TLS;
	test_url_port[13] = "443";
	test_url_query[13] = "/?arg_1=value_1";

	test_url[14] = "https://example.com/api?a=1";
	test_url_host[14] = "example.com";
	test_url_proto[14] = APROTO_HTTPS;
	test_url_transport[14] = APROTO_TLS;
	test_url_port[14] = "443";
	test_url_query[14] = "/api?a=1";

	test_url[15] = "https://example.com/api?arg_1=value_1";
	test_url_host[15] = "example.com";
	test_url_proto[15] = APROTO_HTTPS;
	test_url_transport[15] = APROTO_TLS;
	test_url_port[15] = "443";
	test_url_query[15] = "/api?arg_1=value_1";

	test_url[16] = "https://example.com/api/?a=1";
	test_url_host[16] = "example.com";
	test_url_proto[16] = APROTO_HTTPS;
	test_url_transport[16] = APROTO_TLS;
	test_url_port[16] = "443";
	test_url_query[16] = "/api/?a=1";

	test_url[17] = "http://example.com/";
	test_url_host[17] = "example.com";
	test_url_proto[17] = APROTO_HTTP;
	test_url_transport[17] = APROTO_TCP;
	test_url_port[17] = "80";
	test_url_query[17] = "/";

	test_url[18] = "http://example.com";
	test_url_host[18] = "example.com";
	test_url_proto[18] = APROTO_HTTP;
	test_url_transport[18] = APROTO_TCP;
	test_url_port[18] = "80";
	test_url_query[18] = "/";

	//test_url[19] = "tcp://example.com:25";
	//test_url_host[19] = "example.com";
	//test_url_proto[19] = APROTO_TCP;
	//test_url_transport[19] = APROTO_TCP;
	//test_url_port[19] = "25";
	//test_url_query[19] = "";

	test_url[19] = "tcp://example.com:25/INFO";
	test_url_host[19] = "example.com";
	test_url_proto[19] = APROTO_TCP;
	test_url_transport[19] = APROTO_TCP;
	test_url_port[19] = "25";
	test_url_query[19] = "INFO";

	test_url[20] = "udp://example.org:53";
	test_url_host[20] = "example.org";
	test_url_proto[20] = APROTO_UDP;
	test_url_transport[20] = APROTO_UDP;
	test_url_port[20] = "53";

	test_url[21] = "file:///var/log/messages";
	test_url_host[21] = "/var/log/messages";
	test_url_proto[21] = APROTO_FILE;
	test_url_transport[21] = APROTO_FILE;

	test_url[22] = "http://unix:/var/run/prog.sock:usdfewin:dcrvtvajf@example.com/static//assets?a=1";
	test_url_host[22] = "/var/run/prog.sock";
	test_url_host_header[22] = "example.com";
	test_url_proto[22] = APROTO_HTTP;
	test_url_transport[22] = APROTO_UNIX;
	test_url_query[22] = "/static//assets?a=1";
	test_url_auth[22] = "dXNkZmV3aW46ZGNydnR2YWpm";

	test_url[23] = "http://unix:/var/run/prog.sock:usdfewin:dcrvtvajf@example.com/static//assets?a=1";
	test_url_host[23] = "/var/run/prog.sock";
	test_url_host_header[23] = "example.com";
	test_url_proto[23] = APROTO_HTTP;
	test_url_transport[23] = APROTO_UNIX;
	test_url_query[23] = "/static//assets?a=1";
	test_url_auth[23] = "dXNkZmV3aW46ZGNydnR2YWpm";

	test_url[24] = "tcp://unix:/var/run/haproxy";
	test_url_host[24] = "/var/run/haproxy";
	test_url_proto[24] = APROTO_TCP;
	test_url_transport[24] = APROTO_UNIX;

	test_url[25] = "http://unix:/var/run/prog.sock:linux:ubuntu@example.com/static//assets?a=1";
	test_url_host[25] = "/var/run/prog.sock";
	test_url_host_header[25] = "example.com";
	test_url_proto[25] = APROTO_HTTP;
	test_url_transport[25] = APROTO_UNIX;
	test_url_query[25] = "/static//assets?a=1";
	test_url_user[25] = "linux";
	test_url_pass[25] = "ubuntu";


	for (uint64_t i = 0; i<test_url_size; i++)
	{
		host_aggregator_info *hi = parse_url (test_url[i], strlen(test_url[i]));

		if (test_url[i])
			cut_assert_not_null(hi);
		if (test_url_proto[i])
			cut_assert_equal_int(test_url_proto[i], hi->proto);
		if (test_url_transport[i])
			cut_assert_equal_int(test_url_transport[i], hi->transport);
		if (test_url_host[i])
			cut_assert_equal_string(test_url_host[i], hi->host);
		if (test_url_host_header[i])
			cut_assert_equal_string(test_url_host_header[i], hi->host_header);
		if (test_url_port[i])
			cut_assert_equal_string(test_url_port[i], hi->port);
		if (test_url_query[i])
			cut_assert_equal_string(test_url_query[i], hi->query);
		if (test_url_auth[i])
			cut_assert_equal_string(test_url_auth[i], hi->auth);
		if (test_url_user[i])
			cut_assert_equal_string(test_url_user[i], hi->user);
		if (test_url_pass[i])
			cut_assert_equal_string(test_url_pass[i], hi->pass);

		url_free(hi);
	}
}
