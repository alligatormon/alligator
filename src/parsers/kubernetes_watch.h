#pragma once
#include "kubernetes_common.h"
#include "common/url.h"

void kubernetes_watch_start(host_aggregator_info *hi, kubernetes_operator_opts *opts);
uint8_t kubernetes_watch_active(void);
