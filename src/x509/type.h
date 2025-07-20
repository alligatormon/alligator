#pragma once
#include <inttypes.h>
#include <uv.h>
#include <jansson.h>
#include "common/selector.h"
#include "dstructures/ht.h"
#define X509_TYPE_PEM 1
#define X509_TYPE_PFX 1

typedef struct x509_fs_t {
	char *name;
	char *path;
	string_tokens *match;
	char *password;
	uint8_t type;

	uint64_t period;
	uv_timer_t *period_timer;

	tommy_node node;
} x509_fs_t;

void tls_fs_free();
void tls_fs_handler();
void for_tls_fs_recurse_repeat_period(uv_timer_t *timer);
int x509_push(json_t *x509);
int x509_del(json_t *x509);
