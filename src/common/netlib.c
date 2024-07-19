#include "netlib.h"
#include "patricia.h"

uint8_t ip_check_access(patricia_t* tree, patricia_t* tree6, char *ip)
{
	uint8_t ip_version = ip_get_version(ip);
	if (!tree && !tree6)
		return 1;

	if (ip_version == 6) {

		uint128_t int_ip;
		cidr_to_ip_and_mask128(ip, &int_ip, NULL);
		uint64_t t;
		rnode *node = patricia_find128(tree, int_ip, &t);
		if (node && node->data) {
			return *(uint8_t*)node->data;
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

void network_range_push(patricia_t** tree, patricia_t** tree6, char *cidr, uint8_t action)
{
	if (!*tree && !*tree6) {
		*tree = patricia_new();
		*tree6 = patricia_new();

		uint8_t *action_pass = malloc(sizeof(uint8_t));
		*action_pass = !action;
		network_add_ip(*tree, *tree6, "0.0.0.0/0", action_pass);

		action_pass = malloc(sizeof(uint8_t));
		*action_pass = !action;
		network_add_ip(*tree, *tree6, "::0", action_pass);
	}

	uint8_t *action_pass = malloc(sizeof(uint8_t));
	*action_pass = action;
	network_add_ip(*tree, *tree6, cidr, action_pass);
}

void network_range_delete(patricia_t* tree, patricia_t* tree6, char *cidr)
{
	void *action_pass = network_del_ip(tree, tree6, cidr);
	if (action_pass)
		free(action_pass);
}
