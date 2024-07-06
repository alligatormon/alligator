#include "cluster/type.h"
#include "common/selector.h"
#include "main.h"

int cluster_compare(const void* arg, const void* obj)
{
	char *s1 = (char*)arg;
	char *s2 = ((cluster_node*)obj)->name;
	return strcmp(s1, s2);
}

oplog_record *oplog_record_init(uint64_t size)
{
	oplog_record *oplog = calloc(1, sizeof(*oplog));
	oplog->size = size;
	oplog->container = calloc(size, sizeof(oplog_node));

	return oplog;
}

void oplog_record_free(oplog_record *oplog)
{
	for (uint64_t i = 0; i < oplog->size; i++)
	{
		string *data = oplog->container[i].data;
		if (data)
			string_free(data);
	}
	free(oplog->container);
	free(oplog);
}

oplog_node* oplog_record_insert(oplog_record *oplog, string *data)
{
	oplog_node *opnode = &oplog->container[oplog->end];
	if (!opnode->data)
		opnode->data = string_new();

	string *opdata = opnode->data;

	string_string_copy(opdata, data);

	// move end
	oplog->end = (oplog->end + 1 ) % oplog->size;

	// move begin
	if (oplog->end == oplog->begin)
		oplog->begin = (oplog->begin + 1 ) % oplog->size;


	return opnode;
}

string* oplog_record_get_string(oplog_record *oplog)
{
	string *ret = string_new();

	for (uint64_t i = 0; i < oplog->size; i++)
	{
		uint64_t index = (i + oplog->begin) % oplog->size;
		if (index == oplog->end)
			break;

		if (oplog->container[index].data)
			string_string_cat(ret, oplog->container[index].data);
	}

	return ret;
}

string* oplog_record_shift_string(oplog_record *oplog)
{
	string *ret = string_new();

	for (uint64_t i = 0; i < oplog->size; i++)
	{
		uint64_t index = (i + oplog->begin) % oplog->size;
		if (index == oplog->end)
			break;

		if (oplog->container[index].data)
			string_string_cat(ret, oplog->container[index].data);
	}

	oplog->begin = oplog->end;

	return ret;
}

string* maglev_key_make(cluster_node *cn, char* name, alligator_ht *lbl) {
	string *hash_str = string_new();

	if (cn->sharding_key)
	{
		uint64_t sharding_key_size = cn->sharding_key_size;
		for (uint64_t i = 0; i < sharding_key_size; i++)
		{
			labels_container *labelscont = NULL;
			if (!cn->sharding_key[i]) // alias for __name__: NULL
				string_cat(hash_str, name, strlen(name));
			else if ((labelscont = alligator_ht_search(lbl, labels_hash_compare, cn->sharding_key[i], tommy_strhash_u32(0, cn->sharding_key[i]))))
				string_cat(hash_str, labelscont->key, strlen(labelscont->key));
		}
	}
	else
		string_cat(hash_str, name, strlen(name));

	return hash_str;
}
