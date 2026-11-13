#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "bpf/bpf.h"
#include "config/plain.h"
#include "config/get.h"
#include "api/api.h"

static void bpf_apply_plain(const char *plain)
{
	string *s = string_init(1024);
	char *json_s;

	string_cat(s, (char *)plain, strlen(plain));
	json_s = config_plain_to_json(s);
	string_free(s);
	assert_ptr_notnull(__FILE__, __FUNCTION__, __LINE__, json_s);
	http_api_v1(NULL, NULL, json_s);
	free(json_s);
}

static void test_bpf_filters_and_buckets(void)
{
	char dst[64];
	int trunc = 0;
	char longsrc[80];
	uint32_t len = 0;
	uint64_t dropped = 0;
	const char *allow[] = { "nginx", "sshd" };
	char pod[64], ns[64], container[128];
	const char *cg = "/kubepods/namespace-demo/podabc-def/cri-containerd-cid.scope";

	memset(longsrc, 'a', sizeof(longsrc) - 1);
	longsrc[sizeof(longsrc) - 1] = 0;

	assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 1, bpf_filter_match(BPF_FILTER_NONE, "", "anything"));
	assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 0, bpf_filter_match(BPF_FILTER_PREFIX, "", "abc"));
	assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 1, bpf_filter_match(BPF_FILTER_PREFIX, "10.", "10.1.2.3"));
	assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 0, bpf_filter_match(BPF_FILTER_PREFIX, "10.", "11.0.0.1"));
	assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 1, bpf_filter_match(BPF_FILTER_SUFFIX, ".example.com", "host.example.com"));
	assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 0, bpf_filter_match(BPF_FILTER_SUFFIX, ".example.com", "example.org"));
	assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 0, bpf_filter_match(BPF_FILTER_EQ, "", "x"));
	assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 1, bpf_filter_match(BPF_FILTER_EQ, "nginx", "nginx"));
	assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 1, bpf_filter_allow(allow, 2, "sshd"));
	assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 0, bpf_filter_allow(allow, 2, "bash"));

	assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 3, (int)bpf_str_copy(dst, 4, "abcdef", &trunc));
	assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 1, trunc);
	assert_equal_string(__FILE__, __FUNCTION__, __LINE__, "abc", dst);
	trunc = 0;
	assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 63, (int)bpf_str_copy(dst, 64, longsrc, &trunc));
	assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 1, trunc);

	assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 0, bpf_hist_bucket(0));
	assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 0, bpf_hist_bucket(999));
	assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 1, bpf_hist_bucket(1000));
	assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 2, bpf_hist_bucket(2000));
	assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 1, bpf_hist_le(0) > 0);

	assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 1, bpf_lru_note(&len, 2, &dropped, 1));
	assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 1, bpf_lru_note(&len, 2, &dropped, 1));
	assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 0, bpf_lru_note(&len, 2, &dropped, 1));
	assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 1, (int)dropped);
	assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 1, bpf_lru_note(&len, 2, &dropped, 0));

	assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 1, bpf_kernel_at_least("5.8.0", 5, 8));
	assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 0, bpf_kernel_at_least("5.7.1", 5, 8));
	assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 1, bpf_kernel_at_least("5.15", 5, 15));
	assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 0, bpf_kernel_at_least("5.14.0", 5, 15));
	assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 0, bpf_syscall_nr("read", "x86_64"));
	assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 63, bpf_syscall_nr("read", "aarch64"));
	assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 257, bpf_syscall_nr("openat", "x86_64"));
	assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 56, bpf_syscall_nr("openat", "arm64"));
	assert_equal_string(__FILE__, __FUNCTION__, __LINE__, "openat", (char *)bpf_syscall_name(257, "x86_64"));

	assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 0, bpf_event_label_ok("path", BPF_FILTER_NONE, "", "/etc/passwd", NULL, 0));
	assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 1, bpf_event_label_ok("path", BPF_FILTER_PREFIX, "/etc", "/etc/passwd", NULL, 0));
	assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 0, bpf_event_label_ok("filename", BPF_FILTER_SUFFIX, ".so", "/bin/ls", NULL, 0));
	assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 1, bpf_event_label_ok("daddr", BPF_FILTER_NONE, "", "10.0.0.1", NULL, 0));

	assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 1, bpf_cgroup_parse(cg, pod, sizeof(pod), ns, sizeof(ns), container, sizeof(container)));
	assert_equal_string(__FILE__, __FUNCTION__, __LINE__, "abc-def", pod);
	assert_equal_string(__FILE__, __FUNCTION__, __LINE__, "demo", ns);
	assert_equal_string(__FILE__, __FUNCTION__, __LINE__, "cri-containerd-cid", container);
	assert_equal_int(__FILE__, __FUNCTION__, __LINE__, BPF_NAMED_RUNQUEUE, bpf_named_id("runqueue_latency"));
	assert_equal_int(__FILE__, __FUNCTION__, __LINE__, BPF_NAMED_SCHED, bpf_named_id("sched_latency"));
	assert_equal_string(__FILE__, __FUNCTION__, __LINE__, "bpf_runqueue_latency_seconds", (char *)bpf_named_metric(BPF_NAMED_SCHED));
	assert_equal_string(__FILE__, __FUNCTION__, __LINE__, "bpf_runqueue_latency_seconds", (char *)bpf_named_metric(BPF_NAMED_RUNQUEUE));
	assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 1, bpf_named_is_hist(BPF_NAMED_OFFCPU));
	assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 0, bpf_loader_selftest());
}

static void test_bpf_config_and_scrape(void)
{
	const bpf_probe *p;
	json_t *cfg;
	json_t *bpf;
	json_t *wrap;
	char *dump;
	const char *example =
		"bpf {\n"
		"    kprobe tcp_v4_connect {\n"
		"        latency;\n"
		"        arg 1 type=sockaddr label=daddr filter=prefix value=10.;\n"
		"        arg 1 type=sockaddr label=dport;\n"
		"    }\n"
		"    uprobe /lib64/libc.so.6:getaddrinfo {\n"
		"        arg 0 type=user_str label=name filter=suffix value=.example.com;\n"
		"    }\n"
		"}\n";

	bpf_apply_plain(example);
	assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 2, bpf_probe_count());
	p = bpf_probe_at(0);
	assert_ptr_notnull(__FILE__, __FUNCTION__, __LINE__, p);
	assert_equal_int(__FILE__, __FUNCTION__, __LINE__, BPF_KIND_KPROBE, p->kind);
	assert_equal_string(__FILE__, __FUNCTION__, __LINE__, "tcp_v4_connect", p->symbol);
	assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 1, p->latency);
	assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 2, p->nargs);
	assert_equal_int(__FILE__, __FUNCTION__, __LINE__, BPF_ARG_SOCKADDR, p->args[0].kind);
	assert_equal_int(__FILE__, __FUNCTION__, __LINE__, BPF_FILTER_PREFIX, p->args[0].filter);
	assert_equal_string(__FILE__, __FUNCTION__, __LINE__, "10.", p->args[0].value);
	assert_equal_string(__FILE__, __FUNCTION__, __LINE__, "daddr", p->args[0].label);
	assert_equal_int(__FILE__, __FUNCTION__, __LINE__, BPF_FIELD_PORT, p->args[1].field);
	assert_equal_string(__FILE__, __FUNCTION__, __LINE__, "dport", p->args[1].label);
	p = bpf_probe_at(1);
	assert_ptr_notnull(__FILE__, __FUNCTION__, __LINE__, p);
	assert_equal_int(__FILE__, __FUNCTION__, __LINE__, BPF_KIND_UPROBE, p->kind);
	assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 0, p->latency);
	assert_equal_string(__FILE__, __FUNCTION__, __LINE__, "/lib64/libc.so.6", p->binary);
	assert_equal_string(__FILE__, __FUNCTION__, __LINE__, "getaddrinfo", p->symbol);
	assert_equal_int(__FILE__, __FUNCTION__, __LINE__, BPF_ARG_USER_STR, p->args[0].kind);
	assert_equal_int(__FILE__, __FUNCTION__, __LINE__, BPF_FILTER_SUFFIX, p->args[0].filter);
	assert_equal_string(__FILE__, __FUNCTION__, __LINE__, ".example.com", p->args[0].value);

	cfg = config_get();
	bpf = json_object_get(cfg, "bpf");
	assert_ptr_notnull(__FILE__, __FUNCTION__, __LINE__, bpf);
	wrap = json_object();
	json_object_set(wrap, "bpf", bpf);
	dump = json_dumps(wrap, 0);
	json_decref(wrap);
	json_decref(cfg);
	assert_ptr_notnull(__FILE__, __FUNCTION__, __LINE__, dump);
	http_api_v1(NULL, NULL, dump);
	free(dump);
	assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 2, bpf_probe_count());
	p = bpf_probe_at(0);
	assert_ptr_notnull(__FILE__, __FUNCTION__, __LINE__, p);
	assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 1, p->latency);
	p = bpf_probe_at(1);
	assert_equal_string(__FILE__, __FUNCTION__, __LINE__, "getaddrinfo", p->symbol);
	assert_equal_int(__FILE__, __FUNCTION__, __LINE__, BPF_FILTER_SUFFIX, p->args[0].filter);
	assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 0, p->latency);

	bpf_apply_plain("bpf { probe file_access; }\n");
	assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 0, bpf_probe_on_name("file_access"));
	bpf_apply_plain("bpf { probe file_access { filter=eq value=/etc/passwd; } }\n");
	assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 0, bpf_probe_on_name("file_access"));
	bpf_apply_plain("bpf { probe file_access { filter=prefix value=/etc; } }\n");
	assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 1, bpf_probe_on_name("file_access"));
	bpf_apply_plain("bpf { probe file_access { prefix /etc; filter=suffix value=.so; } }\n");
	assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 0, bpf_probe_on_name("file_access"));
	bpf_apply_plain("bpf { probe file_access { prefix /etc; filter=suffix value=.so; } }\n");
	assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 0, bpf_probe_on_name("file_access"));
	/* Plain allow= sets FILTER_ALLOW so path labels are accepted. */
	bpf_apply_plain("bpf { kprobe do_sys_open { arg 1 type=user_str label=path allow=/etc; } }\n");
	assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 1, bpf_probe_count());
	p = bpf_probe_at(0);
	assert_ptr_notnull(__FILE__, __FUNCTION__, __LINE__, p);
	assert_equal_int(__FILE__, __FUNCTION__, __LINE__, BPF_FILTER_ALLOW, p->args[0].filter);
	assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 1, p->args[0].nallow);
	assert_equal_string(__FILE__, __FUNCTION__, __LINE__, "/etc", p->args[0].allow[0]);
	bpf_apply_plain("bpf { probe fd_track; }\n");
	assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 0, bpf_probe_on_name("fd_track"));
	bpf_apply_plain("bpf { kprobe do_sys_open { arg 1 type=user_str label=path; } }\n");
	assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 0, bpf_probe_count());
	bpf_apply_plain("bpf { probe fd_track { comm nginx; } probe syscall_latency; probe exec; }\n");
	assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 1, bpf_probe_on_name("fd_track"));
	assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 1, bpf_probe_on_name("syscall_latency"));
	assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 1, bpf_probe_on_name("exec"));
	p = bpf_probe_at(1);
	assert_ptr_notnull(__FILE__, __FUNCTION__, __LINE__, p);
	assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 6, p->nsys);
	assert_equal_string(__FILE__, __FUNCTION__, __LINE__, "read", p->syscalls[0]);
	assert_equal_string(__FILE__, __FUNCTION__, __LINE__, "connect", p->syscalls[5]);

	cfg = config_get();
	bpf = json_object_get(cfg, "bpf");
	wrap = json_object();
	json_object_set(wrap, "bpf", bpf);
	dump = json_dumps(wrap, 0);
	json_decref(wrap);
	json_decref(cfg);
	http_api_v1(NULL, NULL, dump);
	free(dump);
	p = NULL;
	{
		int i;
		for (i = 0; i < bpf_probe_count(); i++) {
			const bpf_probe *q = bpf_probe_at(i);
			if (q && q->named == BPF_NAMED_SYSCALL)
				p = q;
		}
	}
	assert_ptr_notnull(__FILE__, __FUNCTION__, __LINE__, p);
	assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 6, p->nsys);
	assert_equal_string(__FILE__, __FUNCTION__, __LINE__, "openat", p->syscalls[2]);

	bpf_apply_plain("bpf { probe runqueue_latency; probe cgroup_memory; }\n");
	assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 1, bpf_probe_on_name("runqueue_latency"));
	bpf_hist_observe("bpf_runqueue_latency_seconds", "bash", NULL, NULL, NULL, 0, 1500);
	bpf_drop_add("runqueue_latency", "lru", 4);
	bpf_pressure_sample("/kubepods/namespace-demo/podabc-def/cri-containerd-cid.scope", "some", 1, 2, 3, 9);
	bpf_scrape();
	metric_test_run_ns(CMP_EQUAL, "bpf_up", "bpf_up", 0, NULL);
	metric_test_run_ns(CMP_EQUAL, "bpf_runqueue_latency_seconds_count", "bpf_runqueue_latency_seconds_count", 1, NULL);
	metric_test_run_ns(CMP_EQUAL, "bpf_probe_dropped_total", "bpf_probe_dropped_total", 4, NULL);
	metric_test_run_ns(CMP_EQUAL, "bpf_cgroup_memory_pressure_waiting_seconds_total", "bpf_cgroup_memory_pressure_waiting_seconds_total", 9, NULL);

	/* Staging table is shared across probes. When full, recycle oldest so a
	 * later metric (e.g. tcp_connect) is still emitted instead of dropped.
	 * Detach first so scrape does not reset the table via bpf_loader_poll. */
	bpf_loader_shutdown();
	bpf_series_reset();
	{
		int i;
		char comm[16];
		for (i = 0; i < BPF_SERIES_MAX; i++) {
			snprintf(comm, sizeof(comm), "c%d", i);
			bpf_hist_observe("bpf_offcpu_seconds", comm, NULL, NULL, NULL, 0, 1000);
		}
		bpf_hist_observe("bpf_tcp_connect_latency_seconds", "curl", NULL, NULL, NULL, 0, 2000);
	}
	bpf_scrape();
	metric_test_run_ns(CMP_EQUAL, "bpf_tcp_connect_latency_seconds_count{comm=\"curl\"}", "bpf_tcp_connect_latency_seconds_count", 1, NULL);
	metric_test_run_ns(CMP_EQUAL, "bpf_probe_dropped_total{probe=\"bpf_tcp_connect_latency_seconds\",reason=\"lru\"}", "bpf_probe_dropped_total", 1, NULL);
	cfg = config_get();
	bpf = json_object_get(cfg, "bpf");
	assert_ptr_null(__FILE__, __FUNCTION__, __LINE__, json_object_get(bpf, "namespace"));
	json_decref(cfg);

	bpf_apply_plain("bpf { namespace custom; probe runqueue_latency; }\n");
	bpf_hist_observe("bpf_runqueue_latency_seconds", "bash", NULL, NULL, NULL, 0, 1500);
	bpf_scrape();
	metric_test_run_ns(CMP_EQUAL, "bpf_up", "bpf_up", 0, "custom");
	metric_test_run_ns(CMP_EQUAL, "bpf_runqueue_latency_seconds_count", "bpf_runqueue_latency_seconds_count", 1, "custom");
	cfg = config_get();
	bpf = json_object_get(cfg, "bpf");
	assert_equal_string(__FILE__, __FUNCTION__, __LINE__, "custom", json_string_value(json_object_get(bpf, "namespace")));
	json_decref(cfg);

	bpf_apply_plain("bpf { probe offcpu { comm nginx; comm searchd; } kprobe tcp_v4_connect { comm nginx; } }\n");
	p = NULL;
	{
		int i;
		for (i = 0; i < bpf_probe_count(); i++) {
			const bpf_probe *q = bpf_probe_at(i);
			if (q && q->named == BPF_NAMED_OFFCPU)
				p = q;
		}
	}
	assert_ptr_notnull(__FILE__, __FUNCTION__, __LINE__, p);
	assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 2, p->ncomm);
	assert_equal_string(__FILE__, __FUNCTION__, __LINE__, "nginx", p->comm[0]);
	assert_equal_string(__FILE__, __FUNCTION__, __LINE__, "searchd", p->comm[1]);
	p = bpf_probe_at(1);
	assert_ptr_notnull(__FILE__, __FUNCTION__, __LINE__, p);
	assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 1, p->ncomm);
	assert_equal_string(__FILE__, __FUNCTION__, __LINE__, "nginx", p->comm[0]);

	/* Per-probe skip gauge: marking one custom probe skipped leaves the other unset. */
	bpf_loader_shutdown();
	bpf_apply_plain(
		"bpf {\n"
		"    kprobe tcp_v4_connect {\n"
		"        arg 1 type=sockaddr label=daddr filter=prefix value=10.;\n"
		"    }\n"
		"    uprobe /lib64/libc.so.6:getaddrinfo {\n"
		"        arg 0 type=user_str label=name filter=suffix value=.example.com;\n"
		"    }\n"
		"}\n");
	assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 2, bpf_probe_count());
	bpf_skip_set("tcp_v4_connect", 1);
	bpf_scrape();
	metric_test_run_ns(CMP_EQUAL, "bpf_probe_skipped{probe=\"tcp_v4_connect\"}", "bpf_probe_skipped", 1, NULL);
	/* getaddrinfo was not skip_set — only the failed probe is gauged. */
	bpf_skip_set("/lib64/libc.so.6:getaddrinfo", 1);
	bpf_scrape();
	metric_test_run_ns(CMP_EQUAL, "bpf_probe_skipped{probe=\"tcp_v4_connect\"}", "bpf_probe_skipped", 1, NULL);
	metric_test_run_ns(CMP_EQUAL, "bpf_probe_skipped{probe=\"/lib64/libc.so.6:getaddrinfo\"}", "bpf_probe_skipped", 1, NULL);

	/* Latency hist staging: probe + arg labels (no comm). Detach so poll cannot reset. */
	bpf_loader_shutdown();
	bpf_apply_plain(
		"bpf {\n"
		"    kprobe tcp_v4_connect {\n"
		"        latency;\n"
		"        arg 1 type=sockaddr label=daddr filter=prefix value=10.;\n"
		"    }\n"
		"}\n");
	p = bpf_probe_at(0);
	assert_ptr_notnull(__FILE__, __FUNCTION__, __LINE__, p);
	assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 1, p->latency);
	assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 0, p->generic_slot);
	bpf_loader_shutdown();
	bpf_series_reset();
	bpf_generic_hist_bucket(0, "10.1.2.3", NULL, NULL, NULL, NULL, NULL, 1, 3);
	bpf_generic_hist_sum(0, "10.1.2.3", NULL, NULL, NULL, NULL, NULL, 2000, 3);
	bpf_scrape();
	metric_test_run_ns(CMP_EQUAL,
		"bpf_probe_latency_seconds_count{probe=\"tcp_v4_connect\",daddr=\"10.1.2.3\"}",
		"bpf_probe_latency_seconds_count", 3, NULL);

	bpf_cleanup();
	assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 0, bpf_probe_count());
}

void bpf_test(void)
{
	test_bpf_filters_and_buckets();
	test_bpf_config_and_scrape();
}
