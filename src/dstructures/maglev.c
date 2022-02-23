#include "dstructures/maglev.h"
#include <string.h>
#include <math.h>
#include <inttypes.h>

void maglev_init(struct maglev_lookup_hash *psrv)
{
	psrv->is_use_index = -1;
	psrv->is_modify_lock = 0;
	psrv->p_temp = NULL;

	psrv->item[0].hash_bucket_size = 0;
	psrv->item[1].hash_bucket_size = 0;

	psrv->item[0].node_size = 0;
	psrv->item[1].node_size = 0;

	psrv->item[0].permutation = NULL;
	psrv->item[1].permutation = NULL;

	psrv->item[0].next = NULL;
	psrv->item[1].next = NULL;

	psrv->item[0].hash_entry = NULL;
	psrv->item[1].hash_entry = NULL;

	psrv->item[0].node_name = NULL;
	psrv->item[1].node_name = NULL;

	psrv->item[0].node_info_entry = NULL;
	psrv->item[1].node_info_entry = NULL;

	psrv->item[0].node_add_index = 0;
	psrv->item[1].node_add_index = 0;
}

int8_t is_maglev_prime(uint64_t n)
{
	if (n < MAGLEV_HASH_SIZE_MIN)
	{
		printf("excess MAGLEV_HASH_SIZE_MIN: %"PRId64"\n", n);
		return 0;
	}
	if (n > MAGLEV_HASH_SIZE_MAX)
	{
		printf("excess MAGLEV_HASH_SIZE_MAX: %"PRId64"\n", n);
		return 0;
	}
	if (n % 2 == 0)
	{
		//printf("is_maglev_prime error: %"PRId64"\n", n);
		return 0;
	}

	int64_t i, j;
	j = (int64_t) sqrt(n + 1);
	for (i = 3; i <= j; i = i + 2)
		if (n % i == 0)
		{
			//printf("is_maglev_prime is not prime error: %"PRIu64" %% %"PRId64" == 0\n", n, i);
			return 0;
		}
	return 1;
}

static struct MAGLEV_SERVICE_PARAMS* __create_maglev_service_unit(struct MAGLEV_SERVICE_PARAMS* pServ, int node_size, int hash_bucket_size)
{
	if (!is_maglev_prime(hash_bucket_size))
		return NULL;

	pServ->hash_bucket_size = hash_bucket_size;
	pServ->node_size = node_size;
	pServ->node_add_index = 0;
	pServ->permutation = (int *) malloc(node_size * hash_bucket_size * sizeof(int));
	pServ->node_info_entry = (void **) malloc(node_size * sizeof(void *));

	pServ->next = (int *) malloc(node_size * sizeof(int));
	pServ->hash_entry = (void **) malloc(hash_bucket_size * sizeof(void *));

	if (NULL == pServ->hash_entry)
		return NULL;

	pServ->node_name = (char **) malloc(node_size * sizeof(char *));

	return pServ;
}

void maglev_loopup_item_clean(struct maglev_lookup_hash *psrv, int index)
{
	if (!psrv->item[index].hash_entry)
		return;

	struct MAGLEV_SERVICE_PARAMS *p_item = &psrv->item[index];

	int i;
	for (i=0;i < p_item->node_size; i++)
	{
		free(*(p_item->node_name + i));
	}
	free(p_item->node_name);
	free(p_item->hash_entry);
	free(p_item->node_info_entry);

	// free
	free(p_item->permutation);
	free(p_item->next);

	p_item->node_name = NULL;
	p_item->hash_entry = NULL;
	p_item->node_info_entry = NULL;
	p_item->permutation= NULL;
	p_item->next = NULL;

	p_item->hash_bucket_size = 0;
	p_item->node_size = 0;
	p_item->node_add_index = 0;
}

int maglev_update_service(struct maglev_lookup_hash *psrv, int node_size, int hash_bucket_size)
{
	if (psrv->is_modify_lock)
		return -1;

	psrv->is_modify_lock = 1;

	int		 i_index = (psrv->is_use_index + 1) % 2;
	maglev_loopup_item_clean(psrv, i_index);

	psrv->p_temp = __create_maglev_service_unit(&psrv->item[i_index], node_size, hash_bucket_size);

	if (!psrv->p_temp)
		return -1;
	return 0;
}

int  maglev_add_node(struct maglev_lookup_hash *psrv, char *node_name_key, void  *rs_info)
{
	if (!psrv->is_modify_lock)
		return -2;

	if (psrv->p_temp->node_add_index >= psrv->p_temp->node_size)
		return -1;

	int M = psrv->p_temp->hash_bucket_size;
	int *permutation = psrv->p_temp->permutation;

	unsigned int offset = DJBHash(node_name_key);
	offset = offset % M;
	unsigned int skip = murmur2(node_name_key, strlen(node_name_key));
	skip = (skip % (M -1)) + 1;

	void **cur_node_info = psrv->p_temp->node_info_entry;
	*(cur_node_info + psrv->p_temp->node_add_index) = rs_info;

	int  cur_name_size = strlen(node_name_key) + 1;
	char *cur_rs_name = (char *) malloc(cur_name_size);

	snprintf(cur_rs_name, cur_name_size, "%s", node_name_key);
	*(psrv->p_temp->node_name + psrv->p_temp->node_add_index) = cur_rs_name;

	int j;
	for (j = 0; j < psrv->p_temp->hash_bucket_size; ++j)
	{
		int perm = (offset + j * skip) % M;
		*(permutation + psrv->p_temp->node_add_index * M + j) = perm;
	}

	psrv->p_temp->node_add_index++;

	return 0;
}

void maglev_create_ht(struct maglev_lookup_hash *psrv)
{
	if (!psrv->is_modify_lock)
		return;

	struct MAGLEV_SERVICE_PARAMS *pServ = psrv->p_temp;

	int N = pServ->node_size;
	int M = pServ->hash_bucket_size;
	int *permutation = pServ->permutation;

	int *next = pServ->next;
	void **entry = pServ->hash_entry;
	void **cur_node_info = pServ->node_info_entry;

	int j;
	for (j=0; j<N; j++) {
		*(next + j) = 0;
	}

	for (j=0; j < M; j++) {
		*(entry + j) = NULL;
	}

	int n = 0;
	int is_run = 1;

	while (is_run)
	{
		int i;
		for (i=0; i< N; i++) {
			int lsnext = *(next + i);
			int c = *(permutation + i * M + lsnext);

			while (*(entry + c) != NULL) {
				*(next + i) = *(next + i) +1;
				lsnext = *(next + i);
				c = *(permutation + i * M + lsnext);
			}

			*(entry + c) = *(cur_node_info + i);
			*(next + i) = *(next + i) +1;
			n++;
			if (n == M)
			{
				is_run = 0;
				break;
			}
		}
	}

}

void maglev_swap_entry(struct maglev_lookup_hash *psrv)
{
	if (!psrv->is_modify_lock)
		return;

	int i_index = (psrv->is_use_index + 1) % 2;

	psrv->is_use_index = i_index;

	psrv->p_temp = NULL;
	psrv->is_modify_lock = 0;
}

void *maglev_lookup_node(struct maglev_lookup_hash *psrv, char *key, int key_size)
{
	int i_index = psrv->is_use_index;
	if (i_index < 0) {
		return NULL;
	}
	if (0 >= psrv->item[ i_index ].hash_bucket_size) {
		return NULL;
	}

	void *pnode_info;

	unsigned int new_key = murmur2(key,key_size);
	int M = psrv->item[i_index].hash_bucket_size;
	void **entry = psrv->item[i_index].hash_entry;

	unsigned int hashkey = new_key % M;
	pnode_info = *(entry + hashkey);

	return pnode_info;
}

unsigned int DJBHash(char *str)
{
	unsigned int hash = 5381;
	while (*str)
		hash = ((hash << 5) + hash) + (*str++);
	hash &= ~(1 << 31);
	return hash;
}

unsigned int murmur2(char *data, int len)
{
	unsigned int  h, k;

	h = 0 ^ len;
	
	while (len >= 4) {
		k = data[0];
		k |= data[1] << 8;
		k |= data[2] << 16;
		k |= data[3] << 24;
	
		k *= 0x5bd1e995;
		k ^= k >> 24;
		k *= 0x5bd1e995;
	
		h *= 0x5bd1e995;
		h ^= k;
	
		data += 4;
		len -= 4;
	}
	
	switch (len) {
	case 3:
		h ^= data[2] << 16;
	case 2:
		h ^= data[1] << 8;
	case 1:
		h ^= data[0];
		h *= 0x5bd1e995;
	}
	
	h ^= h >> 13;
	h *= 0x5bd1e995;
	h ^= h >> 15;
	
	return h;
}
