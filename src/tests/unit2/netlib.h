#include "common/netlib.h"
#include "common/patricia.h"

#define TEST_ADDR_1 "127.0.0.1"
#define TEST_ADDR_2 "8.8.8.8"
void test_ip_check_access_1()
{
	puts("=========\ntest_ip_check_access\n============\n");

	uint8_t ip_access;

	//patricia_t *tree = patricia_new();
	//patricia_t *tree6 = patricia_new();
	patricia_t *tree = NULL;
	patricia_t *tree6 = NULL;
	network_range_push(&tree, &tree6, "0.0.0.0/5", IPACCESS_DENY);
	network_range_push(&tree, &tree6, "10.0.0.0/8", IPACCESS_DENY);
	network_range_push(&tree, &tree6, "128.0.0.1", IPACCESS_DENY);
	network_range_delete(tree, tree6, "128.0.0.1");

	ip_access = ip_check_access(tree, tree6, TEST_ADDR_1);
	assert_equal_int(__FILE__, __FUNCTION__, __LINE__, ip_access, IPACCESS_ALLOW);

	network_range_push(&tree, &tree6, TEST_ADDR_1, IPACCESS_DENY);
	ip_access = ip_check_access(tree, tree6, TEST_ADDR_1);
	assert_equal_int(__FILE__, __FUNCTION__, __LINE__, ip_access, IPACCESS_DENY);

	network_range_delete(tree, tree6, TEST_ADDR_2);
	ip_access = ip_check_access(tree, tree6, TEST_ADDR_2);
	assert_equal_int(__FILE__, __FUNCTION__, __LINE__, ip_access, IPACCESS_ALLOW);
}

void test_ip_to_int()
{
	assert_equal_uint(__FILE__, __FUNCTION__, __LINE__, 634168116, (uint32_t)ip_to_integer("37.204.163.52", 4, NULL));
	assert_equal_uint(__FILE__, __FUNCTION__, __LINE__, 2130706433, (uint32_t)ip_to_integer("127.0.0.1", 4, NULL));
	assert_equal_uint(__FILE__, __FUNCTION__, __LINE__, 2147483647, (uint32_t)ip_to_integer("127.255.255.255", 4, NULL));
	assert_equal_uint(__FILE__, __FUNCTION__, __LINE__, 4294967295, (uint32_t)ip_to_integer("255.255.255.255", 4, NULL));
	assert_equal_uint(__FILE__, __FUNCTION__, __LINE__, 168364039, (uint32_t)ip_to_integer("10.9.8.7", 4, NULL));
	assert_equal_uint(__FILE__, __FUNCTION__, __LINE__, 2886729728, (uint32_t)ip_to_integer("172.16.0.0", 4, NULL));
	assert_equal_uint(__FILE__, __FUNCTION__, __LINE__, 16777216, (uint32_t)ip_to_integer("1.0.0.0", 4, NULL));
	assert_equal_uint(__FILE__, __FUNCTION__, __LINE__, 0, (uint32_t)ip_to_integer("0.0.0.0", 4, NULL));
	assert_equal_uint(__FILE__, __FUNCTION__, __LINE__, 0, (uint32_t)ip_to_integer("0", 4, NULL));
	assert_equal_uint(__FILE__, __FUNCTION__, __LINE__, 167772160, (uint32_t)ip_to_integer("10.0", 4, NULL));
	assert_equal_uint(__FILE__, __FUNCTION__, __LINE__, 2130706433, (uint32_t)ip_to_integer("127.1", 4, NULL));
	assert_equal_uint(__FILE__, __FUNCTION__, __LINE__, 2131558401, (uint32_t)ip_to_integer("127.13.1", 4, NULL));
	assert_equal_uint(__FILE__, __FUNCTION__, __LINE__, ip_to_integer("0000:0000:0000:0000:0000:0000:0370:7334", 6, NULL), 57701172);
	assert_equal_uint(__FILE__, __FUNCTION__, __LINE__, ip_to_integer("0000:0000:0000:0000:10A0:0F00:03C0:7334", 6, NULL), 1197973993617912628LLU);
}

void test_integer_to_ip()
{
	char *network;

	network = integer_to_ip(ip_to_integer("37.204.163.52", 4, NULL), 4);
	assert_equal_string(__FILE__, __FUNCTION__, __LINE__, network, "37.204.163.52");
	free(network);

	//network = integer_to_ip(ip_to_integer("0000:0000:0000:0000:0000:0000:0370:7334", 6, NULL), 6);
    //printf("inter to ip: '%s'\n", network);
	//assert_equal_string(__FILE__, __FUNCTION__, __LINE__, network, "0000:0000:0000:0000:0000:0000:0370:7334");
	//free(network);
}

void test_ip_get_version()
{
	assert_equal_uint(__FILE__, __FUNCTION__, __LINE__, ip_get_version("10.9.8.7"), 4);
	assert_equal_uint(__FILE__, __FUNCTION__, __LINE__, ip_get_version("127.0.0.1"), 4);
	assert_equal_uint(__FILE__, __FUNCTION__, __LINE__, ip_get_version("127.1"), 4);
	assert_equal_uint(__FILE__, __FUNCTION__, __LINE__, ip_get_version("127.12.32.1"), 4);
	assert_equal_uint(__FILE__, __FUNCTION__, __LINE__, ip_get_version("255.255.255.255"), 4);
	assert_equal_uint(__FILE__, __FUNCTION__, __LINE__, ip_get_version("0.0.0.0/0"), 4);
	assert_equal_uint(__FILE__, __FUNCTION__, __LINE__, ip_get_version("2001:0db8:0000:0000:0000:8a2e:0370:7334/64"), 6);
}
