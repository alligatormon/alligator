#pragma once
#include <jansson.h>
#include <uv.h>
#include "dstructures/tommy.h"
#include "dstructures/ht.h"
#include "common/selector.h"

/* ------------------------------------------------------------------ */
/* Tunables                                                            */
/* ------------------------------------------------------------------ */
#define CHROMECDP_DEFAULT_PORT     9222
#define CHROMECDP_DEFAULT_EXEC     "chromium-browser"
#define CHROMECDP_IDLE_INTERVAL_MS 500   /* poll interval for networkidle2 */
#define CHROMECDP_IDLE_TICKS       4     /* idle_count threshold (= 2 s)   */
#define CHROMECDP_MAX_INFLIGHT     2     /* networkidle2 in-flight limit    */
#define CHROMECDP_WS_RBUF_INIT     65536
#define CHROMECDP_WS_RBUF_MAX      (8 * 1024 * 1024)
#define CHROMECDP_CHROME_START_DELAY_MS 2500  /* wait for Chrome to start */
#define CHROMECDP_BATCH_INTERVAL_MS     1000  /* start next batch every 1 s  */
#define CHROMECDP_DEFAULT_BATCH_SIZE    2     /* URLs started per batch tick */
#define CHROMECDP_DEFAULT_MAX_CONCURRENT 20   /* max parallel page crawls    */
#define CHROMECDP_DEFAULT_TIMEOUT_MS    10000 /* per-URL networkidle wait    */
/* Hard deadline slack beyond per-URL "timeout" (navigation idle cap) */
#define CHROMECDP_SETUP_BUDGET_MS       10000 /* create context/attach/enable */
#define CHROMECDP_POST_NAV_BUDGET_MS    10000 /* perf, eval, screenshot, teardown */

/* ------------------------------------------------------------------ */
/* Per-URL configuration node (mirrors puppeteer_node)                 */
/* ------------------------------------------------------------------ */
typedef struct cdp_node {
	string     *url;    /* target URL                              */
	char       *value;  /* serialised JSON options (same as pup.)  */
	tommy_node  node;
} cdp_node;

/* ------------------------------------------------------------------ */
/* Public API                                                          */
/* ------------------------------------------------------------------ */

/* Config management — mirrors puppeteer API */
void cdp_insert(json_t *root);
void cdp_delete(json_t *root);
void cdp_done(void);

/* Called once from main() after config is loaded */
void chromecdp_generator(void);

/* Current module log_level (for config export via config_get). */
int chromecdp_config_log_level(void);

/* Per-URL "timeout" from config ("10s", 10000, …); used for nav idle and budgets. */
uint64_t chromecdp_config_timeout_ms(json_t *config);

/* Total page hard deadline from crawl start: setup + timeout + post-nav phases. */
uint64_t chromecdp_page_budget_ms(json_t *config);
