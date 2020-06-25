#include "main.h"
#include "common/selector.h"

void general_loop_cb(uv_timer_t* handle)
{
	extern aconf* ac;

	//check_https_cert("google.com");

	metric_add_auto("alligator_metric_cache_hit", &ac->metric_cache_hits, DATATYPE_UINT, NULL);
	metric_add_auto("alligator_metric_allocates", &ac->metric_allocates, DATATYPE_UINT, NULL);
	metric_add_auto("alligator_metric_free", &ac->metric_freed, DATATYPE_UINT, NULL);
	//fs_cert_check(strdup("../trash/t2/evt-tls/sample/libuv-tls/server-cert.pem"));
	//fs_cert_check(strdup("cert2.pem"));

	//sd_scrape();
}

void expire_loop(uv_timer_t* handle)
{
	extern aconf* ac;

	if (ac->ttl != 0)
	{
		r_time time = setrtime();
		expire_purge(time.sec, 0);
	}
}

void dump_loop()
{
	metric_dump(-1);
}

void internal_query_loop()
{
	//puts("internal loop");
	//string *body = string_init(10000000);

	//tommy_hashdyn *hash = malloc(sizeof(*hash));
	//tommy_hashdyn_init(hash);

	//labels_hash_insert(hash, "pid", "1");
	//labels_hash_insert(hash, "name", "tail");
	//labels_hash_insert(hash, "type", "stack_bytes");

	//metric_query(0, body, "process_stats", hash, "> 0");
	//puts(body->s);
	//string_free(body);
	query_processing();

}

void general_loop()
{
	extern aconf* ac;
	uv_loop_t *loop = ac->loop;

	uv_timer_t *general_timer = calloc(1, sizeof(*general_timer));
	uv_timer_init(loop, general_timer);
	uv_timer_start(general_timer, general_loop_cb, 1000, 1000);

	uv_timer_t *expire_timer = calloc(1, sizeof(*expire_timer));
	uv_timer_init(loop, expire_timer);
	uv_timer_start(expire_timer, expire_loop, 10000, 10000);

	uv_timer_t *dump_timer = calloc(1, sizeof(*dump_timer));
	uv_timer_init(loop, dump_timer);
	uv_timer_start(dump_timer, dump_loop, 11000, ac->persistence_period);

	uv_timer_t *internal_query = calloc(1, sizeof(*internal_query));
	uv_timer_init(loop, internal_query);
	uv_timer_start(internal_query, internal_query_loop, 3000, 10000);
}
