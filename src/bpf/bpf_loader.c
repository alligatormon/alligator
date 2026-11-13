#include "bpf/bpf.h"
#include "main.h"
#include "common/logs.h"
#include <bpf/libbpf.h>
#include "bpf/libbpf_sys.h"
#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/utsname.h>
#include <unistd.h>
#include "sched.skel.h"
#include "block.skel.h"
#include "tcp.skel.h"
#include "process.skel.h"
#include "dns.skel.h"
#include "generic.skel.h"
#ifndef BPF_NO_LOCK
#include "lock.skel.h"
#endif

extern aconf *ac;

static struct sched_bpf *sched_skel;
static struct block_bpf *block_skel;
static struct tcp_bpf *tcp_skel;
static struct process_bpf *proc_skel;
static struct dns_bpf *dns_skel;
static struct generic_bpf *gen_skel;
#ifndef BPF_NO_LOCK
static struct lock_bpf *lock_skel;
#endif
static struct bpf_link *links[128];
static int nlinks;
static int attached;
static int logged;
static int print_set;
static char machine[64];

static int bpf_libbpf_print(enum libbpf_print_level level, const char *fmt, va_list ap)
{
	char *buf;
	va_list copy;
	int n;

	if (level == LIBBPF_DEBUG)
		return 0;
	va_copy(copy, ap);
	n = vsnprintf(NULL, 0, fmt, copy);
	va_end(copy);
	if (n < 0)
		return 0;
	buf = malloc((size_t)n + 1);
	if (!buf) {
		fprintf(stderr, "bpf: (libbpf log truncated, OOM)\n");
		return 0;
	}
	vsnprintf(buf, (size_t)n + 1, fmt, ap);
	/* WARN so verifier/libbpf errors remain visible when log_level=warn. */
	if (ac)
		glog(L_WARN, "bpf: %s", buf);
	else
		fprintf(stderr, "bpf: %s", buf);
	free(buf);
	return 0;
}

static void bpf_libbpf_print_init(void)
{
	if (print_set)
		return;
	libbpf_set_print(bpf_libbpf_print);
	print_set = 1;
}

static void keep_link(struct bpf_link *link)
{
	if (!link || libbpf_get_error(link))
		return;
	if (nlinks < 128)
		links[nlinks++] = link;
	else
		bpf_link__destroy(link);
}

static void skip_custom_probes(void)
{
	int i;

	for (i = 0; i < bpf_probe_count(); i++) {
		const bpf_probe *p = bpf_probe_at(i);
		if (p && p->kind != BPF_KIND_NAMED)
			bpf_skip_set(p->name, 1);
	}
}

static int keep_link_or_fail(struct bpf_link *link, const char *probe, const char *what)
{
	long err = libbpf_get_error(link);
	char errbuf[256];

	if (!err) {
		keep_link(link);
		return 0;
	}
	errbuf[0] = 0;
	libbpf_strerror((int)-err, errbuf, sizeof(errbuf));
	if (!errbuf[0])
		snprintf(errbuf, sizeof(errbuf), "%s", strerror((int)-err));
	glog(L_ERROR, "bpf: %s '%s' failed: %s (%ld)\n", what, probe ? probe : "?", errbuf, err);
	if (probe)
		bpf_skip_set(probe, 1);
	return -1;
}

static void destroy_links(void)
{
	int i;

	for (i = 0; i < nlinks; i++) {
		bpf_link__destroy(links[i]);
		links[i] = NULL;
	}
	nlinks = 0;
}

static void drop_links_from(int before)
{
	if (before < 0)
		before = 0;
	while (nlinks > before) {
		nlinks--;
		bpf_link__destroy(links[nlinks]);
		links[nlinks] = NULL;
	}
}

static int kernel_ok(int maj, int min)
{
	struct utsname u;

	if (uname(&u))
		return 0;
	snprintf(machine, sizeof(machine), "%s", u.machine);
	return bpf_kernel_at_least(u.release, maj, min);
}

static int map_fd(const struct bpf_map *map)
{
	return bpf_map__fd(map);
}

static void poll_hist(int hist_fd, int sum_fd, const char *metric, const char *extra_key)
{
	struct bpf_hist_key cur, next;
	struct bpf_sum_key scur, snext;
	__u64 count;
	struct bpf_sum_val sum;
	int err;

	if (hist_fd < 0 || sum_fd < 0)
		return;
	err = bpf_map_get_next_key(hist_fd, NULL, &next);
	while (!err) {
		const char *extra = next.extra;
		const char *named = NULL;
		if (!bpf_map_lookup_elem(hist_fd, &next, &count)) {
			if (extra_key && !strcmp(extra_key, "syscall"))
				named = bpf_syscall_name(atoi(next.extra), machine);
			bpf_hist_add_bucket(metric, next.comm, named ? named : extra, extra_key, next.dev, next.cgroup, next.bucket, count);
		}
		cur = next;
		err = bpf_map_get_next_key(hist_fd, &cur, &next);
	}
	err = bpf_map_get_next_key(sum_fd, NULL, &snext);
	while (!err) {
		const char *named = NULL;
		if (!bpf_map_lookup_elem(sum_fd, &snext, &sum)) {
			if (extra_key && !strcmp(extra_key, "syscall"))
				named = bpf_syscall_name(atoi(snext.extra), machine);
			bpf_hist_set_sum(metric, snext.comm, named ? named : snext.extra, snext.dev, snext.cgroup, sum.sum_ns, sum.count);
		}
		scur = snext;
		err = bpf_map_get_next_key(sum_fd, &scur, &snext);
	}
}

static void poll_ctr(int fd, const char *metric, int mode)
{
	struct bpf_ctr_key cur, next;
	__u64 val;
	int err;

	if (fd < 0)
		return;
	err = bpf_map_get_next_key(fd, NULL, &next);
	while (!err) {
		if (!bpf_map_lookup_elem(fd, &next, &val)) {
			int has_dport = 0;
			int has_changed = 0;
			const char *event = next.extra[0] ? next.extra : NULL;
			const char *path = NULL;
			const char *parent = next.parent[0] ? next.parent : NULL;
			if (mode == 1) {
				has_dport = next.changed ? 1 : 0;
				event = NULL;
			} else if (mode == 2) {
				has_changed = 1;
				event = NULL;
			} else if (mode == 3) {
				path = next.extra[0] ? next.extra : NULL;
				event = NULL;
			}
			bpf_counter_set(metric, next.comm, event, path, parent, next.cgroup,
				has_dport, next.dport, has_changed, next.changed, val);
		}
		cur = next;
		err = bpf_map_get_next_key(fd, &cur, &next);
	}
}

static int want(int named)
{
	int i;

	for (i = 0; i < bpf_probe_count(); i++) {
		const bpf_probe *p = bpf_probe_at(i);
		if (p && p->enabled && p->named == named)
			return 1;
	}
	return 0;
}

static void append_comm(char dst[BPF_COMM_MAX][16], int named)
{
	int p, n, s;

	for (n = 0; n < BPF_COMM_MAX && dst[n][0]; n++)
		;
	for (p = 0; p < bpf_probe_count() && n < BPF_COMM_MAX; p++) {
		const bpf_probe *pr = bpf_probe_at(p);
		if (!pr || !pr->enabled || pr->named != named)
			continue;
		for (s = 0; s < pr->ncomm && n < BPF_COMM_MAX; s++)
			snprintf(dst[n++], 16, "%s", pr->comm[s]);
	}
}

static void fill_proc(struct bpf_proc_cfg *cfg)
{
	int i, p;

	memset(cfg, 0, sizeof(*cfg));
	for (i = 0; i < BPF_SYSCALL_SLOTS; i++)
		cfg->sys_nr[i] = 0xffffffffu;
	cfg->syscall_on = want(BPF_NAMED_SYSCALL);
	cfg->life_on = want(BPF_NAMED_LIFE);
	cfg->fd_on = want(BPF_NAMED_FD);
	cfg->io_on = want(BPF_NAMED_IO);
	cfg->exec_on = want(BPF_NAMED_EXEC);
	cfg->file_on = want(BPF_NAMED_FILE);
	cfg->net_on = want(BPF_NAMED_NET);
	cfg->priv_on = want(BPF_NAMED_PRIV);
	cfg->cgroup_labels = bpf_cgroup_labels_on();
	cfg->openat_nr = ( __u32)bpf_syscall_nr("openat", machine);
	cfg->read_nr = (__u32)bpf_syscall_nr("read", machine);
	cfg->write_nr = (__u32)bpf_syscall_nr("write", machine);
	cfg->close_nr = (__u32)bpf_syscall_nr("close", machine);
	cfg->connect_nr = (__u32)bpf_syscall_nr("connect", machine);
	cfg->accept_nr = (__u32)bpf_syscall_nr("accept", machine);
	cfg->accept4_nr = (__u32)bpf_syscall_nr("accept4", machine);
	{
	int nsys = 0;
	for (p = 0; p < bpf_probe_count(); p++) {
		const bpf_probe *pr = bpf_probe_at(p);
		int s;
		if (!pr || !pr->enabled)
			continue;
		if (pr->named == BPF_NAMED_SYSCALL) {
			for (s = 0; s < pr->nsys && nsys < BPF_SYSCALL_SLOTS; s++) {
				int nr = bpf_syscall_nr(pr->syscalls[s], machine);
				if (nr >= 0)
					cfg->sys_nr[nsys++] = (__u32)nr;
			}
		}
		if (pr->named == BPF_NAMED_FILE) {
			for (s = 0; s < pr->nprefix && cfg->nprefix < BPF_PREFIX_MAX; s++) {
				snprintf(cfg->prefix[cfg->nprefix], BPF_STR_MAX, "%s", pr->prefix[s]);
				cfg->nprefix++;
			}
		}
		if (pr->named == BPF_NAMED_EXEC) {
			cfg->exec_filter_op = (__u8)pr->filter_op;
			snprintf(cfg->exec_filter, BPF_STR_MAX, "%s", pr->filter_value);
		}
		if (pr->named == BPF_NAMED_FILE && pr->filter_op) {
			cfg->file_filter_op = (__u8)pr->filter_op;
			snprintf(cfg->file_filter, BPF_STR_MAX, "%s", pr->filter_value);
		}
	}
	append_comm(cfg->comm_allow, BPF_NAMED_FD);
	append_comm(cfg->sys_comm, BPF_NAMED_SYSCALL);
	append_comm(cfg->life_comm, BPF_NAMED_LIFE);
	append_comm(cfg->io_comm, BPF_NAMED_IO);
	append_comm(cfg->exec_comm, BPF_NAMED_EXEC);
	append_comm(cfg->file_comm, BPF_NAMED_FILE);
	append_comm(cfg->net_comm, BPF_NAMED_NET);
	append_comm(cfg->priv_comm, BPF_NAMED_PRIV);
	}
}

static int load_named(void)
{
	__u32 key = 0;
	int err;

	if (want(BPF_NAMED_RUNQUEUE) || want(BPF_NAMED_SCHED) || want(BPF_NAMED_OFFCPU)) {
		struct bpf_sched_cfg cfg;
		memset(&cfg, 0, sizeof(cfg));
		cfg.runq_on = want(BPF_NAMED_RUNQUEUE) || want(BPF_NAMED_SCHED);
		cfg.off_on = want(BPF_NAMED_OFFCPU);
		cfg.cgroup_labels = bpf_cgroup_labels_on();
		append_comm(cfg.runq_comm, BPF_NAMED_RUNQUEUE);
		append_comm(cfg.runq_comm, BPF_NAMED_SCHED);
		append_comm(cfg.off_comm, BPF_NAMED_OFFCPU);
		sched_skel = sched_bpf__open();
		if (!sched_skel) {
			bpf_skip_set("runqueue_latency", 1);
		} else {
		err = sched_bpf__load(sched_skel);
		if (err) {
			bpf_skip_set("runqueue_latency", 1);
			sched_bpf__destroy(sched_skel);
			sched_skel = NULL;
		} else {
			bpf_map__update_elem(sched_skel->maps.sched_cfg, &key, sizeof(key), &cfg, sizeof(cfg), BPF_ANY);
			if (sched_bpf__attach(sched_skel)) {
				bpf_skip_set("runqueue_latency", 1);
				sched_bpf__destroy(sched_skel);
				sched_skel = NULL;
			}
		}
		}
	}
	if (want(BPF_NAMED_BLOCK)) {
		struct bpf_block_cfg cfg;
		memset(&cfg, 0, sizeof(cfg));
		cfg.on = 1;
		cfg.cgroup_labels = bpf_cgroup_labels_on();
		append_comm(cfg.comm, BPF_NAMED_BLOCK);
		block_skel = block_bpf__open();
		if (block_skel && !block_bpf__load(block_skel)) {
			bpf_map__update_elem(block_skel->maps.block_cfg, &key, sizeof(key), &cfg, sizeof(cfg), BPF_ANY);
			if (block_bpf__attach(block_skel)) {
				bpf_skip_set("block_io", 1);
				block_bpf__destroy(block_skel);
				block_skel = NULL;
			}
		} else {
			bpf_skip_set("block_io", 1);
			if (block_skel)
				block_bpf__destroy(block_skel);
			block_skel = NULL;
		}
	}
#ifndef BPF_NO_LOCK
	if (want(BPF_NAMED_LOCK)) {
		if (!kernel_ok(5, 17)) {
			bpf_skip_set("lock_contention", 1);
		} else {
			struct bpf_lock_cfg cfg;
			memset(&cfg, 0, sizeof(cfg));
			append_comm(cfg.comm, BPF_NAMED_LOCK);
			lock_skel = lock_bpf__open();
			if (!lock_skel || lock_bpf__load(lock_skel)) {
				bpf_skip_set("lock_contention", 1);
				if (lock_skel)
					lock_bpf__destroy(lock_skel);
				lock_skel = NULL;
			} else {
				bpf_map__update_elem(lock_skel->maps.lock_cfg, &key, sizeof(key), &cfg, sizeof(cfg), BPF_ANY);
				if (lock_bpf__attach(lock_skel)) {
					bpf_skip_set("lock_contention", 1);
					lock_bpf__destroy(lock_skel);
					lock_skel = NULL;
				}
			}
		}
	}
#else
	if (want(BPF_NAMED_LOCK))
		bpf_skip_set("lock_contention", 1);
#endif
	if (want(BPF_NAMED_TCP_RETRANS) || want(BPF_NAMED_TCP_CONNECT) || want(BPF_NAMED_TCP_RTT) || want(BPF_NAMED_TCP_RESET)) {
		struct bpf_tcp_cfg cfg;
		memset(&cfg, 0, sizeof(cfg));
		cfg.retrans_on = want(BPF_NAMED_TCP_RETRANS);
		cfg.connect_on = want(BPF_NAMED_TCP_CONNECT);
		cfg.rtt_on = want(BPF_NAMED_TCP_RTT);
		cfg.reset_on = want(BPF_NAMED_TCP_RESET);
		cfg.cgroup_labels = bpf_cgroup_labels_on();
		append_comm(cfg.retrans_comm, BPF_NAMED_TCP_RETRANS);
		append_comm(cfg.connect_comm, BPF_NAMED_TCP_CONNECT);
		append_comm(cfg.rtt_comm, BPF_NAMED_TCP_RTT);
		append_comm(cfg.reset_comm, BPF_NAMED_TCP_RESET);
		for (err = 0; err < bpf_probe_count(); err++) {
			const bpf_probe *p = bpf_probe_at(err);
			if (p && p->want_dport)
				cfg.dport_on = 1;
		}
		tcp_skel = tcp_bpf__open();
		if (tcp_skel && !tcp_bpf__load(tcp_skel)) {
			bpf_map__update_elem(tcp_skel->maps.tcp_cfg, &key, sizeof(key), &cfg, sizeof(cfg), BPF_ANY);
			if (tcp_bpf__attach(tcp_skel)) {
				bpf_skip_set("tcp_retransmit", 1);
				tcp_bpf__destroy(tcp_skel);
				tcp_skel = NULL;
			}
		} else {
			bpf_skip_set("tcp_retransmit", 1);
			if (tcp_skel)
				tcp_bpf__destroy(tcp_skel);
			tcp_skel = NULL;
		}
	}
	if (want(BPF_NAMED_SYSCALL) || want(BPF_NAMED_LIFE) || want(BPF_NAMED_FD) || want(BPF_NAMED_IO) ||
	    want(BPF_NAMED_EXEC) || want(BPF_NAMED_FILE) || want(BPF_NAMED_NET) || want(BPF_NAMED_PRIV)) {
		struct bpf_proc_cfg cfg;
		fill_proc(&cfg);
		proc_skel = process_bpf__open();
		if (proc_skel && !process_bpf__load(proc_skel)) {
			bpf_map__update_elem(proc_skel->maps.proc_cfg, &key, sizeof(key), &cfg, sizeof(cfg), BPF_ANY);
			if (process_bpf__attach(proc_skel)) {
				bpf_skip_set("syscall_latency", 1);
				process_bpf__destroy(proc_skel);
				proc_skel = NULL;
			}
		} else {
			bpf_skip_set("syscall_latency", 1);
			if (proc_skel)
				process_bpf__destroy(proc_skel);
			proc_skel = NULL;
		}
	}
	if (want(BPF_NAMED_DNS)) {
		struct bpf_dns_cfg cfg;
		const bpf_probe *dns = NULL;
		int i;
		memset(&cfg, 0, sizeof(cfg));
		cfg.on = 1;
		cfg.cgroup_labels = bpf_cgroup_labels_on();
		for (i = 0; i < bpf_probe_count(); i++) {
			const bpf_probe *p = bpf_probe_at(i);
			if (p && p->named == BPF_NAMED_DNS)
				dns = p;
		}
		if (dns) {
			cfg.filter_op = (__u8)dns->filter_op;
			snprintf(cfg.value, sizeof(cfg.value), "%s", dns->filter_value);
		}
		append_comm(cfg.comm, BPF_NAMED_DNS);
		dns_skel = dns_bpf__open();
		if (dns_skel) {
			bpf_program__set_autoattach(dns_skel->progs.dns_entry, false);
			bpf_program__set_autoattach(dns_skel->progs.dns_ret, false);
		}
		if (dns_skel && dns && !dns_bpf__load(dns_skel)) {
			LIBBPF_OPTS(bpf_uprobe_opts, opt);
			int before = nlinks;
			bpf_map__update_elem(dns_skel->maps.dns_cfg, &key, sizeof(key), &cfg, sizeof(cfg), BPF_ANY);
			opt.func_name = dns->symbol;
			opt.retprobe = false;
			keep_link(bpf_program__attach_uprobe_opts(dns_skel->progs.dns_entry, -1, dns->binary, 0, &opt));
			opt.retprobe = true;
			keep_link(bpf_program__attach_uprobe_opts(dns_skel->progs.dns_ret, -1, dns->binary, 0, &opt));
			if (nlinks < before + 2) {
				bpf_skip_set("dns_latency", 1);
				drop_links_from(before);
				dns_bpf__destroy(dns_skel);
				dns_skel = NULL;
			}
		} else {
			bpf_skip_set("dns_latency", 1);
			if (dns_skel)
				dns_bpf__destroy(dns_skel);
			dns_skel = NULL;
		}
	}
	return 0;
}

static int load_generic(void)
{
	int i;
	int any = 0;
	int attached_n = 0;
	__u32 slot;

	if (!kernel_ok(5, 15)) {
		glog(L_WARN, "bpf: custom kprobe/uprobe need kernel >= 5.15\n");
		skip_custom_probes();
		return 0;
	}
	for (i = 0; i < bpf_probe_count(); i++) {
		const bpf_probe *p = bpf_probe_at(i);
		if (p && p->enabled && p->kind != BPF_KIND_NAMED)
			any = 1;
	}
	if (!any)
		return 0;
	gen_skel = generic_bpf__open();
	if (!gen_skel) {
		glog(L_ERROR, "bpf: generic skeleton open failed: %s\n", strerror(errno));
		skip_custom_probes();
		return 0;
	}
	bpf_program__set_autoattach(gen_skel->progs.generic_kprobe, false);
	bpf_program__set_autoattach(gen_skel->progs.generic_kretprobe, false);
	bpf_program__set_autoattach(gen_skel->progs.generic_uprobe, false);
	bpf_program__set_autoattach(gen_skel->progs.generic_uretprobe, false);
	if (generic_bpf__load(gen_skel)) {
		glog(L_ERROR, "bpf: generic kprobe/uprobe programs failed to load: %s\n", strerror(errno));
		generic_bpf__destroy(gen_skel);
		gen_skel = NULL;
		skip_custom_probes();
		return 0;
	}
	for (i = 0; i < bpf_probe_count(); i++) {
		const bpf_probe *p = bpf_probe_at(i);
		struct bpf_probe_cfg cfg;
		int a;
		int ok;
		int ok_ret;
		int before;

		if (!p || !p->enabled || p->kind == BPF_KIND_NAMED || p->generic_slot < 0)
			continue;
		memset(&cfg, 0, sizeof(cfg));
		cfg.nargs = (__u32)p->nargs;
		cfg.latency = p->latency ? 1 : 0;
		for (a = 0; a < p->nargs && a < BPF_ARG_MAX; a++) {
			cfg.args[a].kind = (__u8)p->args[a].kind;
			cfg.args[a].filter = (__u8)p->args[a].filter;
			cfg.args[a].reg = (__u8)p->args[a].index;
			cfg.args[a].field = (__u8)p->args[a].field;
			snprintf(cfg.args[a].value, BPF_STR_MAX, "%s", p->args[a].value);
			if (p->args[a].nallow > 0)
				snprintf(cfg.args[a].allow0, BPF_STR_MAX, "%s", p->args[a].allow[0]);
			if (p->args[a].nallow > 1)
				snprintf(cfg.args[a].allow1, BPF_STR_MAX, "%s", p->args[a].allow[1]);
			if (p->args[a].nallow > 2)
				snprintf(cfg.args[a].allow2, BPF_STR_MAX, "%s", p->args[a].allow[2]);
			if (p->args[a].nallow > 3)
				snprintf(cfg.args[a].allow3, BPF_STR_MAX, "%s", p->args[a].allow[3]);
		}
		slot = (__u32)p->generic_slot;
		{
			int s, n = 0;
			for (s = 0; s < p->ncomm && n < BPF_COMM_MAX; s++)
				snprintf(cfg.comm[n++], 16, "%s", p->comm[s]);
		}
		bpf_map__update_elem(gen_skel->maps.probe_cfg, &slot, sizeof(slot), &cfg, sizeof(cfg), BPF_ANY);
		ok = -1;
		ok_ret = 0;
		before = nlinks;
		if (p->kind == BPF_KIND_KPROBE) {
			LIBBPF_OPTS(bpf_kprobe_opts, opt);
			opt.bpf_cookie = slot;
			opt.retprobe = false;
			ok = keep_link_or_fail(bpf_program__attach_kprobe_opts(gen_skel->progs.generic_kprobe,
				p->symbol, &opt), p->name, "kprobe attach");
			if (!ok && p->latency) {
				opt.retprobe = true;
				ok_ret = keep_link_or_fail(bpf_program__attach_kprobe_opts(gen_skel->progs.generic_kretprobe,
					p->symbol, &opt), p->name, "kretprobe attach");
				if (ok_ret) {
					drop_links_from(before);
					ok = -1;
				}
			}
		} else if (p->kind == BPF_KIND_UPROBE) {
			LIBBPF_OPTS(bpf_uprobe_opts, opt);
			opt.func_name = p->symbol;
			opt.bpf_cookie = slot;
			opt.retprobe = false;
			ok = keep_link_or_fail(bpf_program__attach_uprobe_opts(gen_skel->progs.generic_uprobe,
				-1, p->binary, 0, &opt), p->name, "uprobe attach");
			if (!ok && p->latency) {
				opt.retprobe = true;
				ok_ret = keep_link_or_fail(bpf_program__attach_uprobe_opts(gen_skel->progs.generic_uretprobe,
					-1, p->binary, 0, &opt), p->name, "uretprobe attach");
				if (ok_ret) {
					drop_links_from(before);
					ok = -1;
				}
			}
		} else {
			bpf_skip_set(p->name, 1);
		}
		if (!ok)
			attached_n++;
	}
	if (!attached_n) {
		generic_bpf__destroy(gen_skel);
		gen_skel = NULL;
		skip_custom_probes();
	}
	return 0;
}

void bpf_loader_shutdown(void)
{
	destroy_links();
	if (sched_skel)
		sched_bpf__destroy(sched_skel);
	if (block_skel)
		block_bpf__destroy(block_skel);
	if (tcp_skel)
		tcp_bpf__destroy(tcp_skel);
	if (proc_skel)
		process_bpf__destroy(proc_skel);
	if (dns_skel)
		dns_bpf__destroy(dns_skel);
	if (gen_skel)
		generic_bpf__destroy(gen_skel);
#ifndef BPF_NO_LOCK
	if (lock_skel)
		lock_bpf__destroy(lock_skel);
	lock_skel = NULL;
#endif
	sched_skel = NULL;
	block_skel = NULL;
	tcp_skel = NULL;
	proc_skel = NULL;
	dns_skel = NULL;
	gen_skel = NULL;
	attached = 0;
}

int bpf_loader_sync(void)
{
	bpf_libbpf_print_init();
	if (access("/sys/kernel/btf/vmlinux", R_OK)) {
		if (!logged) {
			glog(L_INFO, "bpf: /sys/kernel/btf/vmlinux is not readable\n");
			logged = 1;
		}
		return -1;
	}
	if (!kernel_ok(5, 8)) {
		if (!logged) {
			glog(L_INFO, "bpf: kernel is older than 5.8\n");
			logged = 1;
		}
		return -1;
	}
	bpf_loader_shutdown();
	logged = 0;
#ifndef BPF_NO_LOCK
	if (load_named() && !sched_skel && !block_skel && !tcp_skel && !proc_skel && !dns_skel && !lock_skel)
		return -1;
#else
	if (load_named() && !sched_skel && !block_skel && !tcp_skel && !proc_skel && !dns_skel)
		return -1;
#endif
	load_generic();
#ifndef BPF_NO_LOCK
	attached = sched_skel || block_skel || tcp_skel || proc_skel || dns_skel || gen_skel || lock_skel || nlinks > 0;
#else
	attached = sched_skel || block_skel || tcp_skel || proc_skel || dns_skel || gen_skel || nlinks > 0;
#endif
	return attached ? 0 : -1;
}

void bpf_loader_poll(void)
{
	if (!attached)
		return;
	bpf_series_reset();
	if (sched_skel) {
		poll_hist(map_fd(sched_skel->maps.runq_hist), map_fd(sched_skel->maps.runq_sum), "bpf_runqueue_latency_seconds", NULL);
		poll_hist(map_fd(sched_skel->maps.off_hist), map_fd(sched_skel->maps.off_sum), "bpf_offcpu_seconds", NULL);
	}
	if (block_skel)
		poll_hist(map_fd(block_skel->maps.block_hist), map_fd(block_skel->maps.block_sum), "bpf_block_io_latency_seconds", "op");
#ifndef BPF_NO_LOCK
	if (lock_skel)
		poll_hist(map_fd(lock_skel->maps.lock_hist), map_fd(lock_skel->maps.lock_sum), "bpf_lock_contention_seconds", NULL);
#endif
	if (tcp_skel) {
		poll_ctr(map_fd(tcp_skel->maps.retrans_ctr), "bpf_tcp_retransmits_total", 1);
		poll_ctr(map_fd(tcp_skel->maps.reset_ctr), "bpf_tcp_resets_total", 1);
		poll_hist(map_fd(tcp_skel->maps.connect_hist), map_fd(tcp_skel->maps.connect_sum), "bpf_tcp_connect_latency_seconds", NULL);
		poll_hist(map_fd(tcp_skel->maps.rtt_hist), map_fd(tcp_skel->maps.rtt_sum), "bpf_tcp_rtt_seconds", NULL);
	}
	if (proc_skel) {
		poll_hist(map_fd(proc_skel->maps.sys_hist), map_fd(proc_skel->maps.sys_sum), "bpf_syscall_latency_seconds", "syscall");
		poll_ctr(map_fd(proc_skel->maps.life_ctr), "bpf_process_events_total", 0);
		poll_ctr(map_fd(proc_skel->maps.fd_ctr), "bpf_process_fds", 0);
		poll_ctr(map_fd(proc_skel->maps.io_ctr), "bpf_process_io_bytes_total", 0);
		poll_ctr(map_fd(proc_skel->maps.exec_ctr), "bpf_exec_total", 3);
		poll_ctr(map_fd(proc_skel->maps.file_ctr), "bpf_file_access_total", 3);
		poll_ctr(map_fd(proc_skel->maps.net_ctr), "bpf_net_activity_total", 0);
		poll_ctr(map_fd(proc_skel->maps.priv_ctr), "bpf_priv_change_total", 2);
	}
	if (dns_skel)
		poll_hist(map_fd(dns_skel->maps.dns_hist), map_fd(dns_skel->maps.dns_sum), "bpf_dns_latency_seconds", "name");
	if (gen_skel) {
		int fd = map_fd(gen_skel->maps.events);
		struct bpf_event_key cur, next;
		__u64 val;
		int err = bpf_map_get_next_key(fd, NULL, &next);
		while (!err) {
			if (!bpf_map_lookup_elem(fd, &next, &val))
				bpf_generic_set((int)next.probe_id, next.l0, next.l1, next.l2, next.l3, next.l4, next.l5, val);
			cur = next;
			err = bpf_map_get_next_key(fd, &cur, &next);
		}
		{
			int hfd = map_fd(gen_skel->maps.lat_hist);
			int sfd = map_fd(gen_skel->maps.lat_sum);
			struct bpf_lat_hist_key hcur, hnext;
			struct bpf_lat_sum_key scur, snext;
			struct bpf_sum_val sum;
			__u64 hval;

			if (hfd >= 0) {
				err = bpf_map_get_next_key(hfd, NULL, &hnext);
				while (!err) {
					if (!bpf_map_lookup_elem(hfd, &hnext, &hval))
						bpf_generic_hist_bucket((int)hnext.probe_id, hnext.l0, hnext.l1, hnext.l2,
							hnext.l3, hnext.l4, hnext.l5, hnext.bucket, hval);
					hcur = hnext;
					err = bpf_map_get_next_key(hfd, &hcur, &hnext);
				}
			}
			if (sfd >= 0) {
				err = bpf_map_get_next_key(sfd, NULL, &snext);
				while (!err) {
					if (!bpf_map_lookup_elem(sfd, &snext, &sum))
						bpf_generic_hist_sum((int)snext.probe_id, snext.l0, snext.l1, snext.l2,
							snext.l3, snext.l4, snext.l5, sum.sum_ns, sum.count);
					scur = snext;
					err = bpf_map_get_next_key(sfd, &scur, &snext);
				}
			}
		}
		fd = map_fd(gen_skel->maps.drops);
		for (err = 0; err < BPF_PROBE_SLOTS; err++) {
			__u32 k = (__u32)err;
			struct bpf_drop_val d;
			const char *name = NULL;
			int pi;
			if (bpf_map_lookup_elem(fd, &k, &d))
				continue;
			for (pi = 0; pi < bpf_probe_count(); pi++) {
				const bpf_probe *p = bpf_probe_at(pi);
				if (p && p->generic_slot == err) {
					name = p->name;
					break;
				}
			}
			if (!name)
				continue;
			if (d.trunc)
				bpf_drop_add(name, "trunc", d.trunc);
			if (d.lru)
				bpf_drop_add(name, "lru", d.lru);
		}
	}
}

int bpf_loader_attached(void)
{
	return attached;
}

int bpf_loader_selftest(void)
{
	struct sched_bpf *sched;
	struct generic_bpf *gen;

	bpf_libbpf_print_init();
	if (access("/sys/kernel/btf/vmlinux", R_OK))
		return 0;
	sched = sched_bpf__open_and_load();
	if (!sched)
		return 1;
	sched_bpf__destroy(sched);
	/* Regression check for "dereference of modified ctx ptr" on generic_kprobe. */
	gen = generic_bpf__open();
	if (!gen)
		return 1;
	bpf_program__set_autoattach(gen->progs.generic_kprobe, false);
	bpf_program__set_autoattach(gen->progs.generic_kretprobe, false);
	bpf_program__set_autoattach(gen->progs.generic_uprobe, false);
	bpf_program__set_autoattach(gen->progs.generic_uretprobe, false);
	if (generic_bpf__load(gen)) {
		generic_bpf__destroy(gen);
		return 1;
	}
	generic_bpf__destroy(gen);
	return 0;
}
