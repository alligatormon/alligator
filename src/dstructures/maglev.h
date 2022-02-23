#pragma once

#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <mm_malloc.h>

#define  MAGLEV_HASH_SIZE_MIN   211
#define  MAGLEV_HASH_SIZE_MAX	40009

struct MAGLEV_SERVICE_PARAMS
{
	int     node_size;
	int     node_add_index;
	void    **node_info_entry;
	char    **node_name;

	int     hash_bucket_size;
	void    **hash_entry;

	int     *permutation;
	int     *next;
};

typedef struct maglev_lookup_hash
{
	volatile int is_use_index;
	volatile int is_modify_lock;

	struct MAGLEV_SERVICE_PARAMS item[2];
	struct MAGLEV_SERVICE_PARAMS *p_temp;
} maglev_lookup_hash;

void  maglev_init(struct maglev_lookup_hash *psrv);
int  maglev_update_service(struct maglev_lookup_hash *psrv, int node_size, int hash_bucket_size);
int  maglev_add_node(struct maglev_lookup_hash *psrv, char *node_name_key, void  *rs_info);
void maglev_create_ht(struct maglev_lookup_hash *psrv);
void maglev_swap_entry(struct maglev_lookup_hash *psrv);
void *maglev_lookup_node(struct maglev_lookup_hash *psrv, char *key, int key_size);
void maglev_loopup_item_clean(struct maglev_lookup_hash *psrv, int index);

unsigned int DJBHash(char *str);
unsigned int murmur2(char *data, int len);
int8_t is_maglev_prime(uint64_t n);
