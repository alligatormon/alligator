#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "metric/namespace.h"
#include "common/reject.h"
#include "common/validator.h"
#define METRIC_LABEL_MAX_SIZE 255

alligator_ht *get_labels_from_url_pushgateway_format(char *uri, size_t uri_size, context_arg *carg)
{
	if (!uri)
		return 0;

	if (!uri_size)
		return 0;

	char *metrics_str  = strstr(uri, "/metrics/");
	if (!metrics_str)
		return 0;

	alligator_ht *lbl = alligator_ht_init(NULL);
	uint64_t index_get, prev_get;
	char key[METRIC_LABEL_MAX_SIZE];
	char mname[METRIC_LABEL_MAX_SIZE];

	for (index_get = metrics_str - uri + 9; index_get < uri_size;)
	{
		for (; index_get < uri_size && uri[index_get]=='/' && uri[index_get]!=0; ++index_get);
		prev_get = index_get;
		for (; index_get < uri_size && uri[index_get]!='/' && uri[index_get]!=0; ++index_get);
		size_t mname_len = uint_min(index_get-prev_get+1, METRIC_LABEL_MAX_SIZE);
		strlcpy(mname, uri+prev_get, mname_len);

		//prev_get = index_get;
		for (; index_get < uri_size && uri[index_get]=='/' && uri[index_get]!=0; ++index_get);
		if (index_get < uri_size && uri[index_get+1] == 0)
			break;

		prev_get = index_get;
		for (; index_get < uri_size && uri[index_get]!='/' && uri[index_get]!=0; ++index_get);
		size_t key_len = uint_min(index_get-prev_get+1, METRIC_LABEL_MAX_SIZE);
		strlcpy(key, uri+prev_get, key_len);

		if (carg && reject_metric(carg->reject, mname, key))
		{
			labels_hash_free(lbl);
			return (alligator_ht*)1;
		}

		metric_label_value_validator_normalizer(key, key_len);


		metric_name_normalizer(mname, mname_len);
		labels_hash_insert_nocache(lbl, mname, key);

		++index_get;
		if (index_get >= uri_size || uri[index_get]==0)
			break;
	}

	carg->parser_status = 1;
	return lbl;
}
