#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <sched.h>
#include "common/logs.h"
#include "common/rtime.h"
#include "metric/percentile_heap.h"
#include "metric/namespace.h"
#include "main.h"

void maxheapify(double *arr, int64_t i, int64_t heapsize)
{
	int64_t temp, largest, left, right;
	left = (2*i+1);
	right = ((2*i)+2);
	if (left >= heapsize)
		return;
	else
	{
		if (left < (heapsize) && arr[left] > arr[i])
			largest = left;
		else
			largest = i;
		if (right < (heapsize) && arr[right] > arr[largest])
			largest = right;
		if (largest != i)
		{
			temp = arr[i];
			arr[i] = arr[largest];
			arr[largest] = temp;
			maxheapify(arr, largest, heapsize);
		}
	}
}

void buildmaxheap(double *arr, int64_t n)
{
	int64_t heapsize = n;
	int64_t j;
	for (j = n/2; j >= 0; j--)
		maxheapify(arr, j, heapsize);
}

void heapify(double* arr, int64_t n, int64_t i)
{ 
	int64_t smallest = i;
	int64_t l = 2 * i + 1;
	int64_t r = 2 * i + 2;
  
	if (l < n && arr[l] < arr[smallest]) 
		smallest = l; 
  
	if (r < n && arr[r] < arr[smallest]) 
		smallest = r; 
  
	if (smallest != i)
	{ 
		double tmp = arr[i];
		arr[i] = arr[smallest];
		arr[smallest] = tmp;
  
		heapify(arr, n, smallest); 
	} 
} 

void heapSort(double *arr, int64_t n) 
{ 
	for (int64_t i = n / 2 - 1; i >= 0; i--) 
		heapify(arr, n, i); 
  
	for (int64_t i = n - 1; i >= 0; i--)
	{ 
		if (i)
		{
			double tmp = arr[0];
			arr[0] = arr[i];
			arr[i] = tmp;
		}
  
		heapify(arr, i, 0); 
	} 
} 

static int cmp_double_asc(const void *a, const void *b)
{
	double da = *(const double *)a;
	double db = *(const double *)b;
	if (da < db)
		return -1;
	if (da > db)
		return 1;
	return 0;
}

/* stored percentile is the digits after "0." (9 → 0.9, 90 → 0.90). -1 → 1.0. */
static int64_t window_quantile_index(int64_t stored, int64_t k)
{
	double q = 1.0;
	int64_t idx;

	if (k <= 1)
		return 0;
	if (stored > 0)
	{
		int digits = (int)log10((double)stored) + 1;
		q = (double)stored / pow(10.0, (double)digits);
	}
	if (q > 1.0)
		q = 1.0;
	if (q < 0.0)
		q = 0.0;
	idx = (int64_t)ceil(q * (double)k) - 1;
	if (idx < 0)
		idx = 0;
	if (idx >= k)
		idx = k - 1;
	return idx;
}

int8_t quantile_window_empty_parse_string(const char *pol)
{
	if (pol && !strcmp(pol, "zero"))
		return QUANTILE_WINDOW_EMPTY_ZERO;
	if (pol && !strcmp(pol, "delete"))
		return QUANTILE_WINDOW_EMPTY_DELETE;
	glog(L_WARN, "quantile_window_empty: unknown value '%s', using delete\n", pol ? pol : "");
	return QUANTILE_WINDOW_EMPTY_DELETE;
}

struct qw_cached_label {
	char *name;
	char *key;
	struct qw_cached_label *next;
};

static void qw_labels_free(struct qw_cached_label *l)
{
	while (l) {
		struct qw_cached_label *n = l->next;
		free(l->name);
		free(l->key);
		free(l);
		l = n;
	}
}

static struct qw_cached_label *qw_labels_dup(struct qw_cached_label *src)
{
	struct qw_cached_label *head = NULL;
	struct qw_cached_label **tail = &head;
	for (; src; src = src->next) {
		struct qw_cached_label *n = calloc(1, sizeof(*n));
		if (!n || !src->name || !src->key)
		{
			free(n);
			qw_labels_free(head);
			return NULL;
		}
		n->name = strdup(src->name);
		n->key = strdup(src->key);
		if (!n->name || !n->key) {
			free(n->name);
			free(n->key);
			free(n);
			qw_labels_free(head);
			return NULL;
		}
		*tail = n;
		tail = &n->next;
	}
	return head;
}

typedef struct qw_label_acc {
	struct qw_cached_label *head;
	struct qw_cached_label **tail;
} qw_label_acc;

static void qw_label_acc_add(void *funcarg, void *arg)
{
	qw_label_acc *acc = funcarg;
	labels_container *c = arg;
	struct qw_cached_label *n;
	if (!acc || !c || !c->name || !c->key)
		return;
	n = calloc(1, sizeof(*n));
	if (!n)
		return;
	n->name = strdup(c->name);
	n->key = strdup(c->key);
	if (!n->name || !n->key) {
		free(n->name);
		free(n->key);
		free(n);
		return;
	}
	if (!acc->tail)
		acc->tail = &acc->head;
	*acc->tail = n;
	acc->tail = &n->next;
}

static void qw_cache_install(percentile_buffer *pb, const char *name, struct qw_cached_label *labels)
{
	struct qw_cached_label *copy;
	char *owned;
	if (!pb || !name)
		return;
	pthread_mutex_lock(&pb->lock);
	if (pb->series_ready) {
		pthread_mutex_unlock(&pb->lock);
		return;
	}
	pthread_mutex_unlock(&pb->lock);
	copy = qw_labels_dup(labels);
	owned = strdup(name);
	if (!owned || (labels && !copy)) {
		free(owned);
		qw_labels_free(copy);
		return;
	}
	pthread_mutex_lock(&pb->lock);
	if (!pb->series_ready) {
		pb->series_name = owned;
		pb->series_labels = copy;
		pb->series_ready = 1;
		owned = NULL;
		copy = NULL;
	}
	pthread_mutex_unlock(&pb->lock);
	free(owned);
	qw_labels_free(copy);
}

/* Build the series identity the insert path would publish, then cache it. */
static int qw_resolve_identity(percentile_buffer *pb, metric_node *mnode, context_arg *carg, char *custom_mname, alligator_ht *custom_labels, char **name_out, struct qw_cached_label **labels_out)
{
	char name[255];
	alligator_ht *hash;
	qw_label_acc acc;

	hash = alligator_ht_init(NULL);
	if (!hash)
		return 0;
	if (mnode && mnode->labels) {
		snprintf(name, sizeof(name), "%s_quantile", mnode->labels->key ? mnode->labels->key : "quantile");
		for (labels_t *lab = mnode->labels->next; lab; lab = lab->next) {
			if (!lab->name || !lab->key)
				continue;
			labels_hash_insert_nocache(hash, lab->name, lab->key);
		}
	} else {
		snprintf(name, sizeof(name), "%s", custom_mname ? custom_mname : "quantile");
		if (custom_labels)
			labels_merge(hash, custom_labels);
	}
	metric_apply_context_labels(name, hash, carg);
	memset(&acc, 0, sizeof(acc));
	alligator_ht_foreach_arg(hash, qw_label_acc_add, &acc);
	labels_hash_free(hash);
	*name_out = strdup(name);
	*labels_out = acc.head;
	if (!*name_out) {
		qw_labels_free(acc.head);
		*labels_out = NULL;
		return 0;
	}
	qw_cache_install(pb, *name_out, *labels_out);
	return 1;
}

typedef struct qw_job {
	char *name;
	struct qw_cached_label *labels;
	double *values;
	int64_t *pct;
	int64_t nvalues;
	int64_t k;
	uint8_t policy;
	int64_t ttl;
	namespace_struct *ns;
} qw_job;

static void qw_job_free(qw_job *job)
{
	if (!job)
		return;
	free(job->name);
	qw_labels_free(job->labels);
	free(job->values);
	free(job->pct);
	memset(job, 0, sizeof(*job));
}

/* Copy the in-window samples under the buffer lock. Sort and emit happen later. */
static int qw_prepare(percentile_buffer *pb, metric_node *mnode, context_arg *carg, char *custom_mname, alligator_ht *custom_labels, int force, qw_job *job)
{
	r_time now;
	int64_t cutoff, limit, i, k;
	double *samples = NULL;
	int have_cache = 0;

	if (!pb || !job || pb->window_sec <= 0 || !pb->lock_ready)
		return 0;
	memset(job, 0, sizeof(*job));
	now = setrtime();
	pthread_mutex_lock(&pb->lock);
	if (!force && pb->last_calc_sec == now.sec) {
		pthread_mutex_unlock(&pb->lock);
		return 0;
	}
	cutoff = now.sec - pb->window_sec;
	limit = pb->filled;
	if (limit > (int64_t)pb->n)
		limit = (int64_t)pb->n;
	if (limit > 0) {
		samples = malloc((size_t)limit * sizeof(double));
		if (!samples) {
			pthread_mutex_unlock(&pb->lock);
			return 0;
		}
	}
	k = 0;
	for (i = 0; i < limit; i++) {
		if (!pb->ts || pb->ts[i] < cutoff)
			continue;
		samples[k++] = pb->arr[i];
	}
	job->pct = malloc((size_t)pb->percentile_size * sizeof(int64_t));
	job->values = calloc((size_t)pb->percentile_size, sizeof(double));
	if (!job->pct || !job->values || pb->percentile_size < 0) {
		free(samples);
		free(job->pct);
		free(job->values);
		job->pct = NULL;
		job->values = NULL;
		pthread_mutex_unlock(&pb->lock);
		return 0;
	}
	memcpy(job->pct, pb->percentile, (size_t)pb->percentile_size * sizeof(int64_t));
	job->nvalues = pb->percentile_size;
	job->k = k;
	job->policy = pb->empty_policy;
	job->ttl = (pb->empty_policy == QUANTILE_WINDOW_EMPTY_DELETE) ? pb->window_sec : -1;
	job->ns = pb->ns;
	if (pb->series_ready && pb->series_name) {
		job->name = strdup(pb->series_name);
		if (pb->series_labels)
			job->labels = qw_labels_dup(pb->series_labels);
		have_cache = job->name && (job->labels || !pb->series_labels);
		if (!have_cache) {
			free(job->name);
			job->name = NULL;
			qw_labels_free(job->labels);
			job->labels = NULL;
		}
	}
	pb->last_calc_sec = now.sec;
	pthread_mutex_unlock(&pb->lock);

	if (!have_cache) {
		if (!qw_resolve_identity(pb, mnode, carg, custom_mname, custom_labels, &job->name, &job->labels)) {
			free(samples);
			qw_job_free(job);
			return 0;
		}
	}
	if (k > 1)
		qsort(samples, (size_t)k, sizeof(double), cmp_double_asc);
	if (k > 0) {
		for (i = 0; i < job->nvalues; i++)
			job->values[i] = samples[window_quantile_index(job->pct[i], k)];
	}
	free(samples);
	return 1;
}

static void qw_emit_one(qw_job *job, int64_t stored, double value, int64_t ttl)
{
	alligator_ht *hash;
	char quantilekey[32];
	struct qw_cached_label *lab;
	namespace_struct *ns;

	hash = alligator_ht_init(NULL);
	if (!hash)
		return;
	for (lab = job->labels; lab; lab = lab->next)
		labels_hash_insert(hash, lab->name, lab->key);
	if (stored == -1)
		labels_hash_insert(hash, "quantile", "1.0");
	else {
		snprintf(quantilekey, sizeof(quantilekey), "0.%"d64"", stored);
		labels_hash_insert(hash, "quantile", quantilekey);
	}
	ns = job->ns ? job->ns : get_namespace_by_carg(NULL);
	metric_add_ttl(job->name, hash, &value, DATATYPE_DOUBLE, NULL, ns, ttl);
}

static void qw_delete_one(qw_job *job, int64_t stored)
{
	alligator_ht *hash;
	char quantilekey[32];
	struct qw_cached_label *lab;
	namespace_struct *ns;
	labels_t *ll;

	hash = alligator_ht_init(NULL);
	if (!hash)
		return;
	for (lab = job->labels; lab; lab = lab->next)
		labels_hash_insert(hash, lab->name, lab->key);
	if (stored == -1)
		labels_hash_insert(hash, "quantile", "1.0");
	else {
		snprintf(quantilekey, sizeof(quantilekey), "0.%"d64"", stored);
		labels_hash_insert(hash, "quantile", quantilekey);
	}
	ns = job->ns ? job->ns : get_namespace_by_carg(NULL);
	if (!ns || !ns->metrictree) {
		labels_hash_free(hash);
		return;
	}
	ll = labels_initiate(ns, hash, job->name, NULL, ns, 0);
	if (!ll)
		return;
	metric_delete(ns->metrictree, ll, ns->expiretree);
	labels_head_free(ll);
}

static void qw_finish(qw_job *job)
{
	int64_t i;
	if (!job || !job->name)
		return;
	if (job->k <= 0 && job->policy == QUANTILE_WINDOW_EMPTY_DELETE) {
		for (i = 0; i < job->nvalues; i++)
			qw_delete_one(job, job->pct[i]);
		return;
	}
	if (job->k <= 0 && job->policy != QUANTILE_WINDOW_EMPTY_ZERO)
		return;
	for (i = 0; i < job->nvalues; i++) {
		double value = 0;
		int64_t ttl = -1;
		if (job->k > 0) {
			value = job->values[i];
			ttl = job->ttl;
		}
		qw_emit_one(job, job->pct[i], value, ttl);
	}
}

static void emit_quantile(percentile_buffer *pb, metric_node *mnode, context_arg *carg, char *custom_mname, alligator_ht *custom_labels, int64_t i, double value, int64_t ttl_override)
{
	alligator_ht *hash = NULL;
	char metric_name[255];
	char quantilekey[30];
	namespace_struct *ns = NULL;

	if (mnode)
	{
		hash = alligator_ht_init(NULL);
		labels_t *labels = mnode->labels;
		snprintf(metric_name, 255, "%s_quantile", labels->key);
		labels = labels->next;
		for (; labels; labels = labels->next)
		{
			if (!labels->key)
				continue;
			labels_hash_insert(hash, labels->name, labels->key);
		}
		ns = mnode->window_ns;
	}
	else
	{
		hash = labels_dup(custom_labels);
		snprintf(metric_name, 255, "%s", custom_mname ? custom_mname : "quantile");
	}
	if (!ns)
		ns = pb->ns;

	if (pb->percentile[i] == -1)
		labels_hash_insert(hash, "quantile", "1.0");
	else
	{
		snprintf(quantilekey, 30, "0.%"d64"", pb->percentile[i]);
		labels_hash_insert(hash, "quantile", quantilekey);
	}
	metric_add_ttl(metric_name, hash, &value, DATATYPE_DOUBLE, carg, ns, ttl_override);
}

void calc_percentiles(void *arg, percentile_buffer *pb, void *node, char *custom_mname, alligator_ht *custom_labels)
{
	metric_node *mnode = node;
	context_arg *carg = arg;
	int64_t i;
	size_t n = pb->n;
	/* 0 = every insert (resolved at init from mapping or global) */
	int64_t every = pb->calc_every > 0 ? pb->calc_every : 1;
	int need_sort;
	double *values;

	if (pb->window_sec > 0)
	{
		qw_job job;
		if (qw_prepare(pb, mnode, carg, custom_mname, custom_labels, 0, &job))
		{
			qw_finish(&job);
			qw_job_free(&job);
		}
		return;
	}

	if (!pb->lock_ready)
		return;
	pthread_mutex_lock(&pb->lock);
	need_sort = !pb->sorted || pb->inserts_since_calc >= every;
	if (need_sort)
	{
		/* Snapshot collect ring so a concurrent insert cannot clobber the sort. */
		memcpy(pb->sort_arr, pb->arr, n * sizeof(double));
		buildmaxheap(pb->sort_arr, n);
		heapSort(pb->sort_arr, pb->sortsize);
		pb->inserts_since_calc = 0;
		pb->sorted = 1;
	}

	values = malloc((size_t)pb->percentile_size * sizeof(double));
	if (!values)
	{
		pthread_mutex_unlock(&pb->lock);
		return;
	}
	for (i = 0; i < pb->percentile_size; i++)
		values[i] = pb->sort_arr[pb->ipercentile[i]];
	pthread_mutex_unlock(&pb->lock);

	for (i = 0; i < pb->percentile_size; i++)
		emit_quantile(pb, mnode, carg, custom_mname, custom_labels, i, values[i], -1);
	free(values);
}

void heap_insert_at(percentile_buffer *pb, double key, int64_t ts_sec)
{
	if (!pb || !pb->lock_ready)
		return;
	pthread_mutex_lock(&pb->lock);
	if (pb->cur >= (int64_t)pb->n)
		pb->cur = 0;

	pb->arr[pb->cur] = key;
	if (pb->ts)
		pb->ts[pb->cur] = ts_sec;

	/* Legacy rings repeat the first sample so a cold buffer is not half zeros.
	   Windowed rings must not: unfilled slots stay out of the window via filled. */
	if (!pb->window_sec && pb->cur == 0 && pb->arr[1] == 0)
	{
		for (uint64_t i = 1; i < pb->n; ++i)
			pb->arr[i] = key;
	}

	if (pb->filled < (int64_t)pb->n)
		++pb->filled;
	++pb->cur;
	++pb->inserts_since_calc;
	pthread_mutex_unlock(&pb->lock);
}

void heap_insert(percentile_buffer *pb, double key)
{
	r_time now = setrtime();
	heap_insert_at(pb, key, now.sec);
}

void free_percentile_buffer(percentile_buffer *pb)
{
	if (!pb)
		return;

	if (pb->ipercentile)
		free(pb->ipercentile);

	if (pb->percentile)
		free(pb->percentile);

	if (pb->arr)
		free(pb->arr);

	if (pb->sort_arr)
		free(pb->sort_arr);

	if (pb->ts)
		free(pb->ts);

	free(pb->series_name);
	qw_labels_free(pb->series_labels);
	if (pb->lock_ready)
		pthread_mutex_destroy(&pb->lock);

	free(pb);
}

static void percentile_buffer_fail(percentile_buffer *pb)
{
	if (!pb)
		return;
	free(pb->ipercentile);
	free(pb->percentile);
	free(pb->arr);
	free(pb->sort_arr);
	free(pb->ts);
	if (pb->lock_ready)
		pthread_mutex_destroy(&pb->lock);
	free(pb);
}

percentile_buffer* init_percentile_buffer_opts(int64_t *percentile, size_t n, int64_t buffer_min, int64_t calc_every, int64_t window_sec, int8_t empty_policy)
{
	percentile_buffer *pb = calloc(1, sizeof(*pb));
	if (!pb) {
		free(percentile);
		return NULL;
	}
	if (pthread_mutex_init(&pb->lock, NULL) != 0) {
		free(pb);
		free(percentile);
		return NULL;
	}
	pb->lock_ready = 1;
	/* Caller hands off percentile. On failure this function frees it. */
	if (!percentile || n == 0) {
		free(percentile);
		n = 1;
		percentile = calloc(1, sizeof(*percentile));
		if (!percentile) {
			percentile_buffer_fail(pb);
			return NULL;
		}
		percentile[0] = -1;
	}
	pb->percentile_size = n;
	uint64_t i;

	pb->ipercentile = calloc(n, sizeof(int64_t));
	pb->percentile = percentile;
	if (!pb->ipercentile) {
		percentile_buffer_fail(pb);
		return NULL;
	}

	int64_t percentilelong = percentile[0];
	for (i=0; i<n; i++)
		if (percentilelong < percentile[i])
			percentilelong = percentile[i];
	if (percentilelong <= 0)
		percentilelong = 1;

	int8_t digits = (int64_t)log10(percentilelong) + 1;
	size_t natural_n = (size_t)pow(10, digits);
	pb->n = natural_n;

	/* Resolve floor: mapping override (-1 unset) → global ac fallback */
	int64_t resolved_min = buffer_min;
	if (resolved_min < 0)
		resolved_min = ac ? ac->percentile_buffer_min : 0;
	/*
	 * Floor only: e.g. quantiles 0.9 → natural 10, with percentile_buffer_min 1000
	 * grows to 1000 while label stays "0.9". Fine specs like 0.9000 stay at 10000.
	 */
	if (resolved_min > 0 && (size_t)resolved_min > pb->n)
		pb->n = (size_t)resolved_min;

	int64_t percentilemax = 0;
	for (i=0; i<n; i++)
	{
		if (pb->percentile[i] <= 0)
			pb->ipercentile[i] = 0;
		else
		{
			int8_t percdigits = (int64_t)log10(pb->percentile[i]);
			int64_t curpercentile = pb->percentile[i]*pow(10, digits - percdigits - 1);
			int64_t natural_ip = (int64_t)natural_n - curpercentile;
			if (pb->n == natural_n)
				pb->ipercentile[i] = natural_ip;
			else
				pb->ipercentile[i] = (natural_ip * (int64_t)pb->n) / (int64_t)natural_n;

			if (pb->ipercentile[i] < 0)
				pb->ipercentile[i] = 0;
			if (pb->ipercentile[i] >= (int64_t)pb->n)
				pb->ipercentile[i] = (int64_t)pb->n - 1;

			if (percentilemax < pb->ipercentile[i])
				percentilemax = pb->ipercentile[i];
		}
	}

	pb->cur = 0;
	pb->inserts_since_calc = 0;
	pb->sorted = 0;
	/* Resolve calc interval: mapping override → global; 0 = every insert */
	int64_t resolved_every = calc_every;
	if (resolved_every < 0)
		resolved_every = ac ? ac->percentile_calc_every : 0;
	if (resolved_every < 0)
		resolved_every = 0;
	pb->calc_every = resolved_every;

	int64_t resolved_window = window_sec;
	if (resolved_window < 0)
		resolved_window = ac ? ac->quantile_window : 0;
	if (resolved_window < 0)
		resolved_window = 0;
	pb->window_sec = resolved_window;
	/* Count-based deferral would publish a stale window. The insert path
	   recalculates at most once a second; the expire sweep covers the rest. */
	if (pb->window_sec > 0)
		pb->calc_every = 0;

	int8_t resolved_empty = empty_policy;
	if (resolved_empty < 0)
		resolved_empty = ac ? ac->quantile_window_empty : QUANTILE_WINDOW_EMPTY_DELETE;
	if (resolved_empty != QUANTILE_WINDOW_EMPTY_ZERO)
		resolved_empty = QUANTILE_WINDOW_EMPTY_DELETE;
	pb->empty_policy = (uint8_t)resolved_empty;

	for (i=1; i<percentilemax; i*=2);
	if (i >= pb->n)
		pb->sortsize = pb->n;
	else
		pb->sortsize = i;

	glog(L_TRACE, "init_percentile_buffer: n %zu natural %zu sortsize %"d64" calc_every %"d64" window %"d64" for diff %"d64"\n",
		pb->n, natural_n, pb->sortsize, pb->calc_every, pb->window_sec, percentilemax);

	pb->arr = calloc(pb->n, sizeof(double));
	if (!pb->arr) {
		percentile_buffer_fail(pb);
		return NULL;
	}

	pb->sort_arr = calloc(pb->n, sizeof(double));
	if (!pb->sort_arr) {
		percentile_buffer_fail(pb);
		return NULL;
	}

	if (pb->window_sec > 0)
	{
		pb->ts = calloc(pb->n, sizeof(int64_t));
		if (!pb->ts) {
			percentile_buffer_fail(pb);
			return NULL;
		}
	}

	return pb;
}

percentile_buffer* init_percentile_buffer(int64_t *percentile, size_t n)
{
	return init_percentile_buffer_opts(percentile, n, -1, -1, -1, -1);
}

void quantile_window_register(namespace_struct *ns, metric_node *mnode)
{
	if (!ns || !mnode || !ns->quantile_window_lock)
		return;
	pthread_mutex_lock(ns->quantile_window_lock);
	if (mnode->window_registered)
	{
		pthread_mutex_unlock(ns->quantile_window_lock);
		return;
	}
	mnode->window_ns = ns;
	mnode->window_prev = NULL;
	mnode->window_next = ns->quantile_window_head;
	if (ns->quantile_window_head)
		ns->quantile_window_head->window_prev = mnode;
	ns->quantile_window_head = mnode;
	mnode->window_registered = 1;
	pthread_mutex_unlock(ns->quantile_window_lock);
}

void quantile_window_detach(metric_node *mnode)
{
	namespace_struct *ns;
	if (!mnode)
		return;
	ns = mnode->window_ns;
	if (!ns || !ns->quantile_window_lock)
		return;
	pthread_mutex_lock(ns->quantile_window_lock);
	if (!mnode->window_registered)
	{
		pthread_mutex_unlock(ns->quantile_window_lock);
		return;
	}
	while (mnode->window_sweeping)
	{
		pthread_mutex_unlock(ns->quantile_window_lock);
		sched_yield();
		pthread_mutex_lock(ns->quantile_window_lock);
		if (!mnode->window_registered)
		{
			pthread_mutex_unlock(ns->quantile_window_lock);
			return;
		}
	}
	if (mnode->window_prev)
		mnode->window_prev->window_next = mnode->window_next;
	else if (ns->quantile_window_head == mnode)
		ns->quantile_window_head = mnode->window_next;
	if (mnode->window_next)
		mnode->window_next->window_prev = mnode->window_prev;
	mnode->window_next = NULL;
	mnode->window_prev = NULL;
	mnode->window_registered = 0;
	mnode->window_ns = NULL;
	pthread_mutex_unlock(ns->quantile_window_lock);
}

void quantile_window_sweep(namespace_struct *ns)
{
	metric_node *mnode;
	if (!ns || !ns->quantile_window_lock)
		return;
	/* Mark the node sweeping before any tree lock. metric_delete waits on
	   that flag while it already holds the tree lock, so the snapshot must
	   not take the tree lock or the two threads deadlock. Emit only after
	   window_sweeping is cleared. */
	pthread_mutex_lock(ns->quantile_window_lock);
	mnode = ns->quantile_window_head;
	if (mnode)
		mnode->window_sweeping = 1;
	pthread_mutex_unlock(ns->quantile_window_lock);
	while (mnode)
	{
		metric_node *next = NULL;
		qw_job job;
		int got = 0;
		memset(&job, 0, sizeof(job));
		if (mnode->percentile_buf)
			got = qw_prepare(mnode->percentile_buf, mnode, NULL, NULL, NULL, 1, &job);
		pthread_mutex_lock(ns->quantile_window_lock);
		next = mnode->window_next;
		if (next)
			next->window_sweeping = 1;
		mnode->window_sweeping = 0;
		pthread_mutex_unlock(ns->quantile_window_lock);
		if (got)
		{
			qw_finish(&job);
			qw_job_free(&job);
		}
		mnode = next;
	}
}

int64_t* percentile_init_3n(int64_t n1, int64_t n2, int64_t n3)
{
	int64_t *percentile = calloc(3, sizeof(int64_t));
	percentile[0] = n1;
	percentile[1] = n2;
	percentile[2] = n3;

	return percentile;
}
