#pragma once
char* filetailer_handler(context_arg *carg);
void filetailer_write_state_foreach(void *funcarg, void *arg);
void filetailer_write_state(alligator_ht *hash);
void filetailer_handler_del(context_arg *carg);
void filestat_restore_v1(char *buf, size_t len);
void filetailer_crawl_handler();
void filestat_restore();
