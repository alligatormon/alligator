#pragma once
#include "events/context_arg.h"

#ifndef DPKG_ADMINDIR
#define DPKG_ADMINDIR "/var/lib/dpkg"
#endif

void packages_register_metric_families(context_arg *carg);
void dpkg_crawl(char *path);
