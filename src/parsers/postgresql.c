#ifdef __linux__
#include <uv.h>
#include <pquv.h>
#include <stdlib.h>
#include "modules/modules.h"
#include "modules/postgresql.h"
extern pq_library *pqlib;

static void pg_wait_cb(void* opaque, PGresult* r)
{
	puts("=========================================");
	puts(opaque);
	printf("tupes: %d\nfield: %d\nresult: %d\n", pqlib->PQntuples(r), pqlib->PQnfields(r), pqlib->PQresultStatus(r));
	int i;
	int j;
	for (i=0; i<pqlib->PQntuples(r); i++)
	{
		for (j=0; j<pqlib->PQnfields(r); j++)
		{
			int type = pqlib->PQftype(r,j);
			if ((type == 17) || (type == 19) || (type == 25))
				printf("'%s:%d:%s' ", pqlib->PQfname(r, j), pqlib->PQftype(r,j), pqlib->PQgetvalue(r, i, j));
			else
				printf("'%s:%d:%d' ", pqlib->PQfname(r, j), pqlib->PQftype(r,j), ntohl(*((uint32_t *)pqlib->PQgetvalue(r, i, j))));
		}
		puts("");
	}
	pqlib->PQclear(r);
	puts("=========================================");
}

void pgsql_query(pquv_t *pquv, char *query)
{
	pquv_query(pquv, query, pg_wait_cb, query);
}

void do_pg_client()
{
    	char *conninfo = "postgresql://uvuser:uvpass@127.0.0.1:5432/uvtest?connect_timeout=10";
	pquv_t *pquv = pquv_init(conninfo, uv_default_loop());

	int i=1;
	while(i--)
	{
		//pgsql_query(pquv, "SELECT * FROM pg_stat_activity");
		//pgsql_query(pquv, "SELECT EXTRACT(EPOCH FROM (now() - pg_last_xact_replay_timestamp())) as lag");
		//pgsql_query(pquv, "SELECT pg_postmaster_start_time as start_time_seconds from pg_postmaster_start_time()");
		//pgsql_query(pquv, "SELECT schemaname, relname, seq_scan, seq_tup_read, idx_scan, idx_tup_fetch, n_tup_ins, n_tup_upd, n_tup_del, n_tup_hot_upd, n_live_tup, n_dead_tup, last_vacuum, last_autovacuum, last_analyze, last_autoanalyze, vacuum_count, autovacuum_count, analyze_count, autoanalyze_count FROM pg_stat_user_tables");
		//pgsql_query(pquv, "SELECT schemaname, relname, heap_blks_read, heap_blks_hit, idx_blks_read, idx_blks_hit, toast_blks_read, toast_blks_hit, tidx_blks_read, tidx_blks_hit FROM pg_statio_user_tables");
		//pgsql_query(pquv, "SELECT pg_database.datname, pg_database_size(pg_database.datname) as size FROM pg_database");
		//pgsql_query(pquv, "SELECT current_database() as datname, schemaname, relname, heap_blks_read, heap_blks_hit FROM pg_statio_user_tables");
	}

	uv_run(uv_default_loop(), UV_RUN_DEFAULT);
	pquv_free(pquv);
}
#endif
