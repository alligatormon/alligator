#pragma once
struct context_arg;
void icmp_client_del(struct context_arg *carg);
void icmp_client_handler();
void icmp_start(void *arg);
/* 1 if the global ICMP crawl must not icmp_start this carg (oneshot finished,
 * UV teardown, or permanent period aggregate already owned by period_timer). */
int icmp_crawl_should_skip(struct context_arg *carg);
