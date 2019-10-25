#include "common/netlib.h"

#define TEST_ADDR_1 "127.0.0.1"
void test_ip_check_access_1()
{
	puts("=========\ntest_ip_check_access\n============\n");

	uint8_t ip_access;

	network_range *net_acl = calloc(1, sizeof(*net_acl));
	network_range_push(net_acl, "0.0.0.0/5", IPACCESS_DENY);
	network_range_push(net_acl, "10.0.0.0/8", IPACCESS_DENY);
	network_range_push(net_acl, "128.0.0.1", IPACCESS_DENY);
	network_range_delete(net_acl, "128.0.0.1");

	ip_access = ip_check_access(net_acl, TEST_ADDR_1);
	cut_assert_equal_int(ip_access, IPACCESS_ALLOW);

	network_range_push(net_acl, "127.0.0.1", IPACCESS_DENY);
	ip_access = ip_check_access(net_acl, TEST_ADDR_1);
	cut_assert_equal_int(ip_access, IPACCESS_DENY);

	network_range_delete(net_acl, "127.0.0.1", IPACCESS_ALLOW);
	ip_access = ip_check_access(net_acl, TEST_ADDR_1);
	cut_assert_equal_int(ip_access, IPACCESS_ALLOW);
}

void test_ip_to_int()
{
	cut_assert_equal_uintmax(ip_to_integer("37.204.163.52", 4, NULL), 634168116);
	cut_assert_equal_uintmax(ip_to_integer("127.0.0.1", 4, NULL), 2130706433);
	cut_assert_equal_uintmax(ip_to_integer("127.255.255.255", 4, NULL), 2147483647);
	cut_assert_equal_uintmax(ip_to_integer("255.255.255.255", 4, NULL), 4294967295);
	cut_assert_equal_uintmax(ip_to_integer("10.9.8.7", 4, NULL), 168364039);
	cut_assert_equal_uintmax(ip_to_integer("172.16.0.0", 4, NULL), 2886729728);
	cut_assert_equal_uintmax(ip_to_integer("1.0.0.0", 4, NULL), 16777216);
	cut_assert_equal_uintmax(ip_to_integer("0.0.0.0", 4, NULL), 0);
	cut_assert_equal_uintmax(ip_to_integer("0", 4, NULL), 0);
	cut_assert_equal_uintmax(ip_to_integer("10.0", 4, NULL), 167772160);
	cut_assert_equal_uintmax(ip_to_integer("127.1", 4, NULL), 2130706433);
	cut_assert_equal_uintmax(ip_to_integer("127.13.1", 4, NULL), 2131558401);
	//cut_assert_equal_uintmax(ip_to_integer("0000:0000:0000:0000:0000:0000:0370:7334", 6, NULL), 57701172);
	//cut_assert_equal_uintmax(ip_to_integer("0000:0000:0000:0000:10A0:0F00:03C0:7334", 6, NULL), 1197973993617912628LLU);
}

void test_integer_to_ip()
{
	char *network;

	network = integer_to_ip(ip_to_integer("37.204.163.52", 4, NULL), 4);
	cut_assert_equal_string(network, "37.204.163.52");
	free(network);

	//network = integer_to_ip(ip_to_integer("0000:0000:0000:0000:0000:0000:0370:7334", 6, NULL), 6);
	//cut_assert_equal_string(network, "0000:0000:0000:0000:0000:0000:0370:7334");
	//free(network);
}

void test_ip_get_version()
{
	cut_assert_equal_uint(ip_get_version("10.9.8.7"), 4);
	cut_assert_equal_uint(ip_get_version("127.0.0.1"), 4);
	cut_assert_equal_uint(ip_get_version("127.1"), 4);
	cut_assert_equal_uint(ip_get_version("127.12.32.1"), 4);
	cut_assert_equal_uint(ip_get_version("255.255.255.255"), 4);
	cut_assert_equal_uint(ip_get_version("0.0.0.0/0"), 4);
	cut_assert_equal_uint(ip_get_version("2001:0db8:0000:0000:0000:8a2e:0370:7334/64"), 6);
}
