#include "main.h"
#include "amtail/type.h"
#include "events/context_arg.h"

extern aconf *ac;

void main_free()
{
	free(ac->system_carg);
	if (ac->cadvisor_carg)
		free(ac->cadvisor_carg);
	free(ac->log_host);
	free(ac->log_dest);
	free(ac->process_shell);

	alligator_ht_done(ac->tcp_server_handler);
	free(ac->tcp_server_handler);

	alligator_ht_done(ac->aggregator);
	free(ac->aggregator);

	alligator_ht_done(ac->pg_aggregator);
	free(ac->pg_aggregator);

	alligator_ht_done(ac->cass_aggregator);
	free(ac->cass_aggregator);

	alligator_ht_done(ac->file_aggregator);
	free(ac->file_aggregator);

	alligator_ht_done(ac->zk_aggregator);
	free(ac->zk_aggregator);

	alligator_ht_done(ac->my_aggregator);
	free(ac->my_aggregator);

	alligator_ht_done(ac->uggregator);
	free(ac->uggregator);

	alligator_ht_done(ac->modules);
	free(ac->modules);

	free(ac->uv_cache_timer);
	free(ac->uv_cache_fs);

	alligator_ht_done(ac->udpaggregator);
	free(ac->udpaggregator);

	alligator_ht_done(ac->iggregator);
	free(ac->iggregator);

	alligator_ht_done(ac->ping_hash);
	free(ac->ping_hash);

	alligator_ht_done(ac->unixgram_aggregator);
	free(ac->unixgram_aggregator);

	alligator_ht_done(ac->system_aggregator);
	free(ac->system_aggregator);

	alligator_ht_done(ac->process_spawner);
	free(ac->process_spawner);

	alligator_ht_done(ac->fs_x509);
	free(ac->fs_x509);

	alligator_ht_done(ac->action);
	free(ac->action);

	alligator_ht_done(ac->probe);
	free(ac->probe);

	alligator_ht_done(ac->file_stat);
	free(ac->file_stat);

	alligator_ht_done(ac->aggregators);
	free(ac->aggregators);

	alligator_ht_done(ac->entrypoints);
	free(ac->entrypoints);

	thread_loop_free();
	alligator_ht_done(ac->threads);
	free(ac->threads);

	amtail_free();

	free(ac);
}
