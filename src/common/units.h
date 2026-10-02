#pragma once

int64_t get_ms_from_human_range(const char *hrange, size_t hsize);
int64_t get_sec_from_human_range(const char *hrange, size_t hsize);
/* 1 and *out set when s is a bare number or a duration (60, 60s, 1m). */
int quantile_window_parse(const char *s, size_t n, int64_t *out);
