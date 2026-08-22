#pragma once
#include "events/context_arg.h"
char* filetailer_handler(context_arg *carg);
void filetailer_directory_file_crawl(void *arg);
void filetailer_write_state_foreach(void *funcarg, void *arg);
void filetailer_write_state(alligator_ht *hash);
void filetailer_handler_del(context_arg *carg);
void filestat_restore_v1(char *buf, size_t len);
void filetailer_crawl_handler();
void filetailer_shutdown(void);
void filestat_restore();
/* Used by unit tests; also called from filetailer_handler. */
void filetailer_apply_path_glob(context_arg *carg);
uint8_t filetailer_wants_content_read(context_arg *carg);
