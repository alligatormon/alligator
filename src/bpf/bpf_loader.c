#include "bpf/bpf.h"
/* main.h #defines u32 and u64 as printf conversions. sys/epoll.h uses those
 * names as union members, so this header has to be parsed first. uv_poll
 * still watches a dup of ring_buffer__epoll_fd; this only fixes the clash. */
#include <sys/epoll.h>
#include "main.h"
#include "common/logs.h"
#include <bpf/libbpf.h>
#include "bpf/libbpf_sys.h"
#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/utsname.h>
#include <unistd.h>
#include <uv.h>
/* Skeletons are generated under the build directory. Angle brackets do not
 * search src/bpf/, so a leftover generic.skel.h next to this file cannot
 * hide capture_rb and the capture programs. */
#include <sched.skel.h>
#include <block.skel.h>
#include <tcp.skel.h>
#include <process.skel.h>
#include <dns.skel.h>
#include <generic.skel.h>
#ifndef BPF_NO_LOCK
#include <lock.skel.h>
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
static unsigned char link_capture[128];
static int nlinks;
static int attached;
static int logged;
static int print_set;
static char machine[64];
/* One wakeup drains at most this many samples so a burst cannot stall the loop.
 * Returning -1 from the sample callback stops libbpf after that sample is consumed. */
#define CAP_DRAIN_BUDGET 64

struct cap_watch {
	uv_poll_t poll;
	int dupfd;
};

static struct cap_watch *cap_watch;
static struct ring_buffer *cap_rb;
static int cap_budget;
static int cap_consume_logged;

static int cap_sample(void *ctx, void *data, size_t size)
{
	(void)ctx;
	bpf_capture_on_event(data, size);
	if (cap_budget > 0 && --cap_budget == 0)
		return -1;
	return 0;
}

static struct ring_buffer *cap_rb_current(void)
{
	return cap_rb;
}

/* budget > 0 stops after that many samples. 0 drains whatever is queued. */
static int cap_drain(struct ring_buffer *rb, int budget)
{
	int n;

	if (!rb)
		return 0;
	cap_budget = budget;
	n = ring_buffer__consume(rb);
	if (budget > 0 && cap_budget == 0 && n < 0) {
		errno = 0;
		return 0;
	}
	if (n < 0 && !cap_consume_logged) {
		cap_consume_logged = 1;
		glog(L_ERROR, "bpf: capture ring buffer consume failed: %d\n", n);
	}
	return n;
}

static void cap_watch_closed(uv_handle_t *h)
{
	struct cap_watch *w = (struct cap_watch *)h;

	if (w->dupfd >= 0)
		close(w->dupfd);
	free(w);
}

static void cap_poll_cb(uv_poll_t *handle, int status, int events)
{
	struct epoll_event ev;
	int fd;

	(void)handle;
	(void)events;
	if (!cap_rb)
		return;
	if (status < 0) {
		glog(L_ERROR, "bpf: capture ring buffer poll: %s\n", uv_strerror(status));
		return;
	}
	/* Harvest one inner epoll notification. A partial drain leaves the map
	 * readable, so libuv calls us again without spinning inside this callback. */
	fd = ring_buffer__epoll_fd(cap_rb);
	if (fd >= 0)
		(void)epoll_wait(fd, &ev, 1, 0);
	cap_drain(cap_rb, CAP_DRAIN_BUDGET);
}

static int cap_poll_arm(struct ring_buffer *rb)
{
	uv_loop_t *loop;
	struct cap_watch *w;
	int fd;
	int dupfd;
	int err;

	fd = ring_buffer__epoll_fd(rb);
	if (fd < 0)
		return -1;
	loop = (ac && ac->loop) ? ac->loop : uv_default_loop();
	if (!loop)
		return -1;
	/* libuv watches the fd it is given. Dup so ring_buffer__free can close
	 * the original while the poll handle is still shutting down. */
	dupfd = dup(fd);
	if (dupfd < 0)
		return -1;
	fcntl(dupfd, F_SETFD, FD_CLOEXEC);
	w = calloc(1, sizeof(*w));
	if (!w) {
		close(dupfd);
		return -1;
	}
	w->dupfd = dupfd;
	w->poll.data = w;
	err = uv_poll_init(loop, &w->poll, dupfd);
	if (err) {
		glog(L_ERROR, "bpf: capture uv_poll_init: %s\n", uv_strerror(err));
		close(dupfd);
		free(w);
		return -1;
	}
	err = uv_poll_start(&w->poll, UV_READABLE, cap_poll_cb);
	if (err) {
		glog(L_ERROR, "bpf: capture uv_poll_start: %s\n", uv_strerror(err));
		uv_close((uv_handle_t *)&w->poll, cap_watch_closed);
		return -1;
	}
	cap_watch = w;
	cap_consume_logged = 0;
	return 0;
}

static void cap_rb_close(void)
{
	struct cap_watch *w = cap_watch;
	struct ring_buffer *rb = cap_rb;

	cap_watch = NULL;
	cap_rb = NULL;
	if (w && !uv_is_closing((uv_handle_t *)&w->poll)) {
		uv_poll_stop(&w->poll);
		uv_close((uv_handle_t *)&w->poll, cap_watch_closed);
	}
	if (rb)
		ring_buffer__free(rb);
}

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

static void keep_link(struct bpf_link *link, int capture)
{
	if (!link || libbpf_get_error(link))
		return;
	if (nlinks < 128) {
		link_capture[nlinks] = capture ? 1 : 0;
		links[nlinks++] = link;
	} else
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

static int keep_link_or_fail(struct bpf_link *link, const char *probe, const char *what, int capture)
{
	long err = libbpf_get_error(link);
	char errbuf[256];

	if (!err) {
		keep_link(link, capture);
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
		link_capture[i] = 0;
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
		link_capture[nlinks] = 0;
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
			keep_link(bpf_program__attach_uprobe_opts(dns_skel->progs.dns_entry, -1, dns->binary, 0, &opt), 0);
			opt.retprobe = true;
			keep_link(bpf_program__attach_uprobe_opts(dns_skel->progs.dns_ret, -1, dns->binary, 0, &opt), 0);
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

static int any_capture(void)
{
	int i;

	for (i = 0; i < bpf_probe_count(); i++) {
		const bpf_probe *p = bpf_probe_at(i);
		if (p && p->enabled && p->kind != BPF_KIND_NAMED && p->mode == BPF_MODE_CAPTURE)
			return 1;
	}
	return 0;
}

static void skip_capture_probes(void)
{
	int i;

	for (i = 0; i < bpf_probe_count(); i++) {
		const bpf_probe *p = bpf_probe_at(i);
		if (p && p->mode == BPF_MODE_CAPTURE)
			bpf_skip_set(p->name, 1);
	}
}

static void detach_capture_links(void)
{
	int i, w = 0;

	for (i = 0; i < nlinks; i++) {
		if (link_capture[i]) {
			bpf_link__destroy(links[i]);
			links[i] = NULL;
			link_capture[i] = 0;
			continue;
		}
		if (w != i) {
			links[w] = links[i];
			links[i] = NULL;
		}
		link_capture[w] = 0;
		w++;
	}
	nlinks = w;
}

static void generic_prepare(struct generic_bpf *skel, int load_capture)
{
	bpf_program__set_autoattach(skel->progs.generic_kprobe, false);
	bpf_program__set_autoattach(skel->progs.generic_kretprobe, false);
	bpf_program__set_autoattach(skel->progs.generic_uprobe, false);
	bpf_program__set_autoattach(skel->progs.generic_uretprobe, false);
	bpf_program__set_autoattach(skel->progs.generic_kprobe_capture, false);
	bpf_program__set_autoattach(skel->progs.generic_uprobe_capture, false);
	bpf_program__set_autoattach(skel->progs.generic_uretprobe_capture, false);
	/* Count-only configs never verify or load the capture programs. */
	bpf_program__set_autoload(skel->progs.generic_kprobe_capture, load_capture);
	bpf_program__set_autoload(skel->progs.generic_uprobe_capture, load_capture);
	bpf_program__set_autoload(skel->progs.generic_uretprobe_capture, load_capture);
	if (!load_capture)
		bpf_map__set_max_entries(skel->maps.capture_rb, 4096);
}

static int generic_open_load(int load_capture)
{
	gen_skel = generic_bpf__open();
	if (!gen_skel) {
		glog(L_ERROR, "bpf: generic skeleton open failed: %s\n", strerror(errno));
		return -1;
	}
	generic_prepare(gen_skel, load_capture);
	if (generic_bpf__load(gen_skel)) {
		glog(L_ERROR, "bpf: generic programs failed to load%s: %s\n",
			load_capture ? " with capture" : "", strerror(errno));
		generic_bpf__destroy(gen_skel);
		gen_skel = NULL;
		return -1;
	}
	return 0;
}

static int load_generic(void)
{
	int i;
	int any = 0;
	int capture;
	int capture_ok;
	int links_before;
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
	capture = any_capture();
	if (generic_open_load(capture)) {
		if (!capture) {
			skip_custom_probes();
			return 0;
		}
		/* Capture verifier failure must not take count/latency with it. */
		glog(L_ERROR, "bpf: capture programs failed to load; retrying count and latency only\n");
		skip_capture_probes();
		if (generic_open_load(0)) {
			skip_custom_probes();
			return 0;
		}
		capture_ok = 0;
	} else {
		capture_ok = capture;
	}
	/* DNS and other named probes already own slots in links[]. */
	links_before = nlinks;
	for (i = 0; i < bpf_probe_count(); i++) {
		const bpf_probe *p = bpf_probe_at(i);
		struct bpf_probe_cfg cfg;
		int a;
		int ok;
		int ok_ret;
		int before;

		if (!p || !p->enabled || p->kind == BPF_KIND_NAMED || p->generic_slot < 0)
			continue;
		if (p->mode == BPF_MODE_CAPTURE && !capture_ok) {
			bpf_skip_set(p->name, 1);
			continue;
		}
		memset(&cfg, 0, sizeof(cfg));
		cfg.nargs = (__u32)p->nargs;
		cfg.latency = p->latency ? 1 : 0;
		cfg.mode = p->mode == BPF_MODE_CAPTURE ? BPF_MODE_CAPTURE : BPF_MODE_COUNT;
		cfg.pair_id = (__u32)p->pair_slot;
		cfg.has_when = p->has_when ? 1 : 0;
		cfg.retval_gt = p->retval_gt;
		cfg.save_mask = 0;
		for (a = 0; a < p->nsave && a < BPF_ARG_MAX; a++) {
			int bit = p->save_arg[a];
			if (bit >= 0 && bit < BPF_ARG_MAX)
				cfg.save_mask |= (1u << bit);
		}
		for (a = 0; a < p->nargs && a < BPF_ARG_MAX; a++) {
			cfg.args[a].kind = (__u8)p->args[a].kind;
			cfg.args[a].filter = (__u8)p->args[a].filter;
			cfg.args[a].reg = (__u8)p->args[a].index;
			cfg.args[a].field = (__u8)p->args[a].field;
			cfg.args[a].size_src = (__u8)p->args[a].size_src;
			cfg.args[a].size_reg = (__u8)p->args[a].size_arg;
			cfg.args[a].from_saved = p->args[a].from_saved ? 1 : 0;
			cfg.args[a].max = (p->args[a].buf_max > 0 && p->args[a].buf_max < 65536) ? (__u16)p->args[a].buf_max : 0;
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
			struct bpf_program *prog = gen_skel->progs.generic_kprobe;
			opt.bpf_cookie = slot;
			opt.retprobe = false;
			if (p->mode == BPF_MODE_CAPTURE)
				prog = gen_skel->progs.generic_kprobe_capture;
			ok = keep_link_or_fail(bpf_program__attach_kprobe_opts(prog,
				p->symbol, &opt), p->name, "kprobe attach", p->mode == BPF_MODE_CAPTURE);
			if (!ok && p->latency && p->mode != BPF_MODE_CAPTURE) {
				opt.retprobe = true;
				ok_ret = keep_link_or_fail(bpf_program__attach_kprobe_opts(gen_skel->progs.generic_kretprobe,
					p->symbol, &opt), p->name, "kretprobe attach", 0);
				if (ok_ret) {
					drop_links_from(before);
					ok = -1;
				}
			}
		} else if (p->kind == BPF_KIND_UPROBE) {
			LIBBPF_OPTS(bpf_uprobe_opts, opt);
			struct bpf_program *prog = gen_skel->progs.generic_uprobe;
			opt.func_name = p->symbol;
			opt.bpf_cookie = slot;
			opt.retprobe = false;
			if (p->mode == BPF_MODE_CAPTURE)
				prog = gen_skel->progs.generic_uprobe_capture;
			ok = keep_link_or_fail(bpf_program__attach_uprobe_opts(prog,
				-1, p->binary, 0, &opt), p->name, "uprobe attach", p->mode == BPF_MODE_CAPTURE);
			if (!ok && p->latency && p->mode != BPF_MODE_CAPTURE) {
				opt.retprobe = true;
				ok_ret = keep_link_or_fail(bpf_program__attach_uprobe_opts(gen_skel->progs.generic_uretprobe,
					-1, p->binary, 0, &opt), p->name, "uretprobe attach", 0);
				if (ok_ret) {
					drop_links_from(before);
					ok = -1;
				}
			}
		} else if (p->kind == BPF_KIND_URETPROBE) {
			LIBBPF_OPTS(bpf_uprobe_opts, opt);
			if (!p->binary[0] || !p->symbol[0]) {
				bpf_skip_set(p->name, 1);
			} else {
				opt.func_name = p->symbol;
				opt.bpf_cookie = slot;
				opt.retprobe = true;
				ok = keep_link_or_fail(bpf_program__attach_uprobe_opts(gen_skel->progs.generic_uretprobe_capture,
					-1, p->binary, 0, &opt), p->name, "uretprobe attach", 1);
			}
		} else {
			bpf_skip_set(p->name, 1);
		}
	}
	if (capture_ok) {
		int capture_links = 0;
		int rfd;
		struct ring_buffer *rb = NULL;

		for (i = 0; i < nlinks; i++) {
			if (link_capture[i])
				capture_links++;
		}
		if (capture_links) {
			rfd = map_fd(gen_skel->maps.capture_rb);
			cap_rb_close();
			if (rfd >= 0)
				rb = ring_buffer__new(rfd, cap_sample, NULL, NULL);
			if (!rb) {
				glog(L_ERROR, "bpf: capture ring buffer open failed; detaching capture probes\n");
				detach_capture_links();
				skip_capture_probes();
			} else if (cap_poll_arm(rb)) {
				glog(L_ERROR, "bpf: capture ring buffer poll failed; scrape will drain it\n");
			}
			if (rb)
				cap_rb = rb;
		}
	}
	if (nlinks == links_before) {
		cap_rb_close();
		if (gen_skel)
			generic_bpf__destroy(gen_skel);
		gen_skel = NULL;
		skip_custom_probes();
	}
	return 0;
}

void bpf_loader_shutdown(void)
{
	cap_rb_close();
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
		/* Safety net for anything the poll wakeup has not drained yet. */
		cap_drain(cap_rb_current(), 0);
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
			if (d.ringbuf_full)
				bpf_drop_add(name, "ringbuf_full", d.ringbuf_full);
			if (d.user_read_fail)
				bpf_drop_add(name, "user_read_fail", d.user_read_fail);
			if (d.stash_miss)
				bpf_drop_add(name, "stash_miss", d.stash_miss);
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
	bpf_program__set_autoattach(gen->progs.generic_kprobe_capture, false);
	bpf_program__set_autoattach(gen->progs.generic_uprobe_capture, false);
	bpf_program__set_autoattach(gen->progs.generic_uretprobe_capture, false);
	if (generic_bpf__load(gen)) {
		generic_bpf__destroy(gen);
		return 1;
	}
	generic_bpf__destroy(gen);
	return 0;
}
