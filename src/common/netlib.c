#include "netlib.h"
#include "patricia.h"

uint8_t ip_get_version(char *ip)
{
	uint64_t delim = strcspn(ip, ":.");
	uint8_t ip_version = 0;
	if (ip[delim] == '.')
		ip_version = 4;
	else if (ip[delim] == ':')
		ip_version = 6;
	else
		return 0;

	return ip_version;
}

char* integer_to_ip(uint128_t ipaddr, uint8_t ip_version)
{
	char *ip = calloc(1, 40);
	char *cur = ip;
	if (ip_version == 4)
	{
		int8_t blocks = 4;
		while (--blocks >= 0)
		{
			uint8_t number = 0;
			uint128_t select_octet = grpow(256, blocks);
			if (ipaddr >= select_octet)
			{
				number = ipaddr/select_octet;
				ipaddr -= (ipaddr/select_octet)*select_octet;
				int syms = sprintf(cur, "%"PRIu8, number);
				if (syms>0)
					cur += syms;
			}
			else
			{
				strcat(cur++, "0");
			}

			if (blocks != 0)
			{
				strcat(cur++, ".");
			}
		}
	}
	else if (ip_version == 6)
	{
		int8_t blocks = 8;
		while (--blocks >= 0)
		{
			uint16_t number = 0;
			uint128_t select_hextet = grpow(65536, blocks);
			if (ipaddr >= select_hextet)
			{
				number = ipaddr/select_hextet;
				ipaddr -= (ipaddr/select_hextet)*select_hextet;
				int syms = sprintf(cur, "%"PRIu16, number);
				if (syms>0)
					cur += syms;
			}
			else
			{
				strcat(cur++, "0");
			}

			if (blocks != 0)
			{
				strcat(cur++, ":");
			}
		}
	}
	else
	{
		free(ip);
		return NULL;
	}

	return ip;
}

uint128_t ip_get_range(uint8_t prefix, uint8_t ip_version)
{
	uint128_t ip_range;
	if (ip_version == 4)
		ip_range = grpow(2, (32 - prefix));
	else if (ip_version == 6)
		ip_range = grpow(2, (128 - prefix));
	else
		return 0;

	return ip_range;
}

uint128_t ip_get_network(uint128_t ipaddr, uint128_t ip_range)
{
	uint128_t ret = (ipaddr/ip_range)*ip_range;
	return ret;
}

void cidr_to_network_range(network_range_node *nr, char *cidr)
{
	uint128_t ip_range;
	uint128_t ip_network;
	char *pref;
	uint8_t ip_version = ip_get_version(cidr);
	uint128_t ipaddr = ip_to_integer(cidr, ip_version, &pref);
	uint8_t prefix;
	if (*pref == '/')
		prefix = strtoull(++pref, NULL, 10);
	else
		prefix = 32;

	ip_range = ip_get_range(prefix, ip_version);
	if (!ip_range)
		return;

	ip_network = ip_get_network(ipaddr, ip_range);

	char *network = integer_to_ip(ip_network, ip_version);
	free(network);

	nr->start = ip_network;
	nr->end = ip_network+ip_range-1;
}

uint8_t ip_check_access(network_range *nr, patricia_t* tree, char *ip)
{
	if (!nr)
		return 1;
	uint64_t i = nr->cur;
	if (!i)
		return 1;

	uint8_t ip_version = ip_get_version(ip);
	if (ip_version == 6) {
		uint128_t ipaddr = ip_to_integer(ip, ip_version, NULL);

		while (i--)
		{
			if (ipaddr >= nr->nr_node[i].start && ipaddr <= nr->nr_node[i].end)
				return nr->nr_node[i].action;
		}
	}
	else if (ip_version == 4) {
		uint32_t int_ip;
			cidr_to_ip_and_mask(ip, &int_ip, NULL);
			uint64_t t;
			rnode *node = patricia_find(tree, int_ip, &t);
			if (node && node->data) {
			return *(uint8_t*)node->data;
		}

	}
	return 0;
}

network_range* network_range_duplicate(network_range *nr)
{
	if (!nr)
		return nr;

	network_range *ret = calloc(1, sizeof(*ret));

	if (nr->nr_node)
	{
		ret->nr_node = malloc((nr->max) * sizeof(network_range_node));
		for (uint64_t i = 0; i < nr->cur; i++)
		{
			ret->nr_node[i].action = nr->nr_node[i].action;
			ret->nr_node[i].start = nr->nr_node[i].start;
			ret->nr_node[i].end = nr->nr_node[i].end;
		}
		ret->cur = nr->cur;
		ret->max = nr->max;
	}

	return ret;
}

void network_range_push(network_range *nr, patricia_t** tree, char *cidr, uint8_t action)
{
	if (!nr)
		return;

	if (!*tree)
		*tree = patricia_new();

	if (!nr->nr_node)
	{
		nr->nr_node = malloc(8*sizeof(network_range_node));
		cidr_to_network_range(&nr->nr_node[0], "0.0.0.0/0");
		nr->nr_node[0].action = !action;
		nr->cur = 1;
		nr->max = 8;

		uint8_t *action_pass = malloc(sizeof(uint8_t));
		*action_pass = !action;
		network_add_ip(*tree, "0.0.0.0/0", action_pass);
	}

	if (nr->cur == nr->max)
	{
		network_range_node *ptr = realloc(nr->nr_node, (nr->max*2)*sizeof(network_range_node));
		if (!ptr)
			return;
		nr->nr_node = ptr;
		nr->max *= 2;
	}

	cidr_to_network_range(&nr->nr_node[nr->cur], cidr);
	nr->nr_node[nr->cur].action = action;
	++nr->cur;

	uint8_t *action_pass = malloc(sizeof(uint8_t));
	*action_pass = action;
	network_add_ip(*tree, cidr, action_pass);
}

void network_range_delete(network_range *nr, patricia_t* tree, char *cidr)
{
	uint64_t i = nr->cur;

	network_range_node* nr_match = malloc(8*sizeof(network_range_node));
	nr->max = 8;
	cidr_to_network_range(nr_match, cidr);

	void *action_pass = network_del_ip(tree, cidr);
	if (action_pass)
		free(action_pass);

	while (i--)
	{
		if (nr_match->start == nr->nr_node[i].start && nr_match->end <= nr->nr_node[i].end)
		{
			nr->cur--;
			while(i < nr->cur)
			{
				nr->nr_node[i].start = nr->nr_node[i+1].start;
				nr->nr_node[i].end = nr->nr_node[i+1].end;
				nr->nr_node[i].action = nr->nr_node[i+1].action;
				++i;
			}
		}
	}
}

void network_range_free(network_range *nr)
{
	if (!nr)
		return;

	if (nr->nr_node)
		free(nr->nr_node);

	free(nr);
}
