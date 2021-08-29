#include "main.h"
#include "common/selector.h"
#include "alligator_version.h"

void general_loop_cb(uv_timer_t* handle)
{
	extern aconf* ac;

	//check_https_cert("google.com");

	uint64_t val = 1;
	metric_add_labels("alligator_version", &val, DATATYPE_UINT, NULL, "version", ALLIGATOR_VERSION);
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

	r_time time = setrtime();
	expire_purge(time.sec, 0);
}

void dump_loop()
{
	metric_dump(-1);
	filetailer_write_state(ac->file_stat);
}

void internal_query_loop()
{
	//query_processing();

	//postgres_run("postgresql://postgres@localhost", "SELECT * FROM pg_stat_activity", "SELECT * FROM pg_stat_all_tables", "SELECT * FROM pg_stat_replication", "SELECT count(datname) FROM pg_database;");

	//mysql_run();
}

void internal_query_loop2()
{
	//mysql_init();
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

	//uv_timer_t *internal_query = calloc(1, sizeof(*internal_query));
	//uv_timer_init(loop, internal_query);
	//uv_timer_start(internal_query, internal_query_loop, 3000, 10000);

	//uv_timer_t *internal_query2 = calloc(1, sizeof(*internal_query2));
	//uv_timer_init(loop, internal_query2);
	//uv_timer_start(internal_query2, internal_query_loop2, 100000, 1000);
}
