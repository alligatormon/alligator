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
