#if defined(__FreeBSD__) || defined(__APPLE__)
#include "main.h"
#include "system/bsd/firewall.h"
#include "metric/namespace.h"
#include "metric/metric_types.h"
#include "common/validator.h"
#include "common/logs.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <stdint.h>
#include <unistd.h>
#ifdef __FreeBSD__
#include <sys/sysctl.h>
#endif

extern aconf *ac;

#define PF_LINE_LEN 4096
#define PF_ANCHOR_DEPTH 16

static void emit_firewall_counter(const char *handler, const char *table, const char *chain,
	const char *verdict, const char *userdata, uint64_t packets, uint64_t bytes)
{
	char handler_l[32];
	char table_l[256];
	char chain_l[256];
	char verdict_l[64];
	char userdata_l[512];

	strlcpy(handler_l, handler, sizeof(handler_l));
	strlcpy(table_l, table, sizeof(table_l));
	strlcpy(chain_l, chain, sizeof(chain_l));
	strlcpy(verdict_l, verdict, sizeof(verdict_l));
	strlcpy(userdata_l, userdata, sizeof(userdata_l));

	metric_label_value_validator_normalizer(table_l, strlen(table_l));
	metric_label_value_validator_normalizer(chain_l, strlen(chain_l));
	metric_label_value_validator_normalizer(verdict_l, strlen(verdict_l));
	metric_label_value_validator_normalizer(userdata_l, strlen(userdata_l));

	metric_add_labels5("firewall_packets_total", &packets, DATATYPE_UINT, ac->system_carg,
		"handler", handler_l, "table", table_l, "chain", chain_l, "verdict", verdict_l, "userdata", userdata_l);
	metric_add_labels5("firewall_bytes_total", &bytes, DATATYPE_UINT, ac->system_carg,
		"handler", handler_l, "table", table_l, "chain", chain_l, "verdict", verdict_l, "userdata", userdata_l);
}

static void emit_firewall_works(const char *handler, int works)
{
	char handler_l[32];
	uint64_t val = works ? 1 : 0;

	strlcpy(handler_l, handler, sizeof(handler_l));
	metric_add_labels("firewall_works", &val, DATATYPE_UINT, ac->system_carg, "handler", handler_l);
}

static void trim_line(char *line)
{
	size_t len;

	if (!line)
		return;

	len = strlen(line);
	while (len > 0 && (line[len - 1] == '\n' || line[len - 1] == '\r'))
		line[--len] = '\0';
}

static int pf_parse_counters(const char *line, uint64_t *packets, uint64_t *bytes)
{
	const char *p;

	*packets = 0;
	*bytes = 0;
	if (!strstr(line, "Packets:") || !strstr(line, "Bytes:"))
		return 0;

	p = strstr(line, "Packets:");
	if (p)
		*packets = strtoull(p + 8, NULL, 10);
	p = strstr(line, "Bytes:");
	if (p)
		*bytes = strtoull(p + 6, NULL, 10);
	return 1;
}

static void pf_parse_rule_meta(const char *line, char *verdict, size_t verdict_sz,
	char *chain, size_t chain_sz, char *label, size_t label_sz)
{
	const char *p;
	const char *in;
	const char *out;
	char word[64];
	size_t i;

	verdict[0] = chain[0] = label[0] = '\0';

	p = line;
	while (*p == ' ' || *p == '\t')
		++p;
	if (*p == '@') {
		p = strchr(p, ' ');
		if (p)
			++p;
	}

	i = 0;
	while (*p && *p != ' ' && *p != '\t' && i + 1 < sizeof(word)) {
		word[i++] = *p++;
	}
	word[i] = '\0';
	strlcpy(verdict, word, verdict_sz);

	in = strstr(line, " in ");
	out = strstr(line, " out ");
	if (in && (!out || in < out))
		strlcpy(chain, "in", chain_sz);
	else if (out)
		strlcpy(chain, "out", chain_sz);
	else
		strlcpy(chain, "any", chain_sz);

	p = strstr(line, "label \"");
	if (p) {
		p += 7;
		i = 0;
		while (*p && *p != '"' && i + 1 < label_sz)
			label[i++] = *p++;
		label[i] = '\0';
	}
}

static void pf_rule_userdata(const char *line, const char *label, char *userdata, size_t userdata_sz)
{
	const char *p;
	size_t i;

	if (label[0]) {
		strlcpy(userdata, label, userdata_sz);
		return;
	}

	p = line;
	while (*p == ' ' || *p == '\t')
		++p;
	if (*p == '@') {
		i = 0;
		while (*p && *p != ' ' && *p != '\t' && i + 1 < userdata_sz)
			userdata[i++] = *p++;
		userdata[i] = '\0';
		return;
	}

	strlcpy(userdata, line, userdata_sz);
	metric_label_value_validator_normalizer(userdata, strlen(userdata));
	if (strlen(userdata) > 200)
		userdata[200] = '\0';
}

static int pf_anchor_name(const char *line, char *anchor, size_t anchor_sz)
{
	const char *p;
	const char *end;
	size_t len;

	anchor[0] = '\0';
	p = strstr(line, "anchor \"");
	if (!p)
		return 0;

	p += 8;
	end = strchr(p, '"');
	if (!end)
		return 0;

	len = (size_t)(end - p);
	if (len >= anchor_sz)
		len = anchor_sz - 1;
	memcpy(anchor, p, len);
	anchor[len] = '\0';
	return 1;
}

static const char *pf_current_table(char anchor_stack[][256], int anchor_depth)
{
	if (anchor_depth > 0)
		return anchor_stack[anchor_depth - 1];
	return "main";
}

static void pf_handle_brace(char anchor_stack[][256], int *anchor_depth)
{
	if (*anchor_depth > 0)
		--(*anchor_depth);
	if (*anchor_depth < 0)
		*anchor_depth = 0;
	if (*anchor_depth == 0)
		anchor_stack[0][0] = '\0';
}

static void pf_collect_rules(void)
{
	FILE *fp;
	char line[PF_LINE_LEN];
	char anchor_stack[PF_ANCHOR_DEPTH][256];
	int anchor_depth = 0;
	char pending_rule[PF_LINE_LEN];
	char pending_verdict[64];
	char pending_chain[64];
	char pending_label[256];
	char pending_userdata[512];
	const char *table;
	int rules_seen = 0;

	fp = popen("/sbin/pfctl -vvs rules 2>/dev/null", "r");
	if (!fp)
		return;

	pending_rule[0] = '\0';
	anchor_stack[0][0] = '\0';

	while (fgets(line, sizeof(line), fp)) {
		trim_line(line);
		if (!line[0])
			continue;

		if (strstr(line, "Packets:") && strstr(line, "Bytes:")) {
			uint64_t packets = 0;
			uint64_t bytes = 0;

			if (!pf_parse_counters(line, &packets, &bytes))
				continue;
			if (!pending_rule[0])
				continue;

			table = pf_current_table(anchor_stack, anchor_depth);
			pf_rule_userdata(pending_rule, pending_label, pending_userdata, sizeof(pending_userdata));
			emit_firewall_counter("pf", table, pending_chain, pending_verdict, pending_userdata, packets, bytes);
			rules_seen = 1;
			pending_rule[0] = '\0';
			continue;
		}

		if (!strcmp(line, "}") || !strcmp(line, "{")) {
			if (!strcmp(line, "}"))
				pf_handle_brace(anchor_stack, &anchor_depth);
			continue;
		}

		if (line[0] == ' ' || line[0] == '\t')
			continue;

		{
			char anchor_name[256];

			if (pf_anchor_name(line, anchor_name, sizeof(anchor_name)) && anchor_depth < PF_ANCHOR_DEPTH) {
				strlcpy(anchor_stack[anchor_depth], anchor_name, sizeof(anchor_stack[0]));
				++anchor_depth;
			}
		}

		strlcpy(pending_rule, line, sizeof(pending_rule));
		pf_parse_rule_meta(line, pending_verdict, sizeof(pending_verdict),
			pending_chain, sizeof(pending_chain), pending_label, sizeof(pending_label));
	}

	pclose(fp);

	if (rules_seen)
		emit_firewall_works("pf", 1);
}

static void pf_collect_labels(void)
{
	FILE *fp;
	char line[PF_LINE_LEN];
	char pending_label[256];
	int labels_seen = 0;

	fp = popen("/sbin/pfctl -vvs labels 2>/dev/null", "r");
	if (!fp)
		return;

	pending_label[0] = '\0';

	while (fgets(line, sizeof(line), fp)) {
		trim_line(line);
		if (!line[0])
			continue;

		if (strstr(line, "Packets:") && strstr(line, "Bytes:")) {
			uint64_t packets = 0;
			uint64_t bytes = 0;

			if (!pf_parse_counters(line, &packets, &bytes))
				continue;
			if (!pending_label[0])
				continue;

			emit_firewall_counter("pf", "main", "labels", "label", pending_label, packets, bytes);
			labels_seen = 1;
			pending_label[0] = '\0';
			continue;
		}

		if (line[0] == ' ' || line[0] == '\t')
			continue;

		if (!strncmp(line, "label \"", 7)) {
			const char *p = line + 7;
			size_t i = 0;

			while (*p && *p != '"' && i + 1 < sizeof(pending_label))
				pending_label[i++] = *p++;
			pending_label[i] = '\0';
		}
	}

	pclose(fp);

	if (labels_seen)
		emit_firewall_works("pf", 1);
}

static int pf_enabled(void)
{
	FILE *fp;
	char line[256];

	fp = popen("/sbin/pfctl -s info 2>/dev/null", "r");
	if (!fp)
		return 0;

	while (fgets(line, sizeof(line), fp)) {
		if (strstr(line, "Status: Enabled")) {
			pclose(fp);
			return 1;
		}
	}

	pclose(fp);
	return 0;
}

static void pf_collect(void)
{
	if (!pf_enabled())
		return;

	pf_collect_rules();
	pf_collect_labels();
}

#ifdef __FreeBSD__
static int ipfw_enabled(void)
{
	int enable = 0;
	size_t len = sizeof(enable);

	if (sysctlbyname("net.inet.ip.fw.enable", &enable, &len, NULL, 0) != 0)
		return 0;
	return enable != 0;
}

static void ipfw_collect(void)
{
	FILE *fp;
	char line[PF_LINE_LEN];
	char chain[32];
	char verdict[64];
	char userdata[512];
	int rules_seen = 0;

	if (!ipfw_enabled())
		return;

	fp = popen("/sbin/ipfw -a list 2>/dev/null", "r");
	if (!fp)
		return;

	while (fgets(line, sizeof(line), fp)) {
		char *p = line;
		char *end;
		unsigned long ruleno;
		unsigned long packets;
		unsigned long bytes;

		trim_line(line);
		if (!line[0])
			continue;

		while (*p == ' ' || *p == '\t')
			++p;
		if (*p < '0' || *p > '9')
			continue;

		ruleno = strtoul(p, &end, 10);
		if (end == p)
			continue;
		p = end;

		packets = strtoul(p, &end, 10);
		if (end == p)
			continue;
		p = end;

		bytes = strtoul(p, &end, 10);
		if (end == p)
			continue;
		p = end;

		while (*p == ' ' || *p == '\t')
			++p;

		if (!strncmp(p, "prob", 4)) {
			p += 4;
			while (*p == ' ' || *p == '\t')
				++p;
			strtoul(p, &end, 10);
			if (end != p)
				p = end;
			while (*p == ' ' || *p == '\t')
				++p;
		}

		{
			size_t i = 0;

			while (*p && *p != ' ' && *p != '\t' && i + 1 < sizeof(verdict)) {
				verdict[i++] = *p++;
			}
			verdict[i] = '\0';
		}

		while (*p == ' ' || *p == '\t')
			++p;

		snprintf(chain, sizeof(chain), "%lu", ruleno);
		strlcpy(userdata, p, sizeof(userdata));
		metric_label_value_validator_normalizer(userdata, strlen(userdata));
		if (strlen(userdata) > 200)
			userdata[200] = '\0';

		emit_firewall_counter("ipfw", "ipfw", chain, verdict, userdata,
			(uint64_t)packets, (uint64_t)bytes);
		rules_seen = 1;
	}

	pclose(fp);

	if (rules_seen)
		emit_firewall_works("ipfw", 1);
}
#endif

void bsd_firewall_collect(void)
{
	if (!ac || !ac->system_carg)
		return;

	pf_collect();
#ifdef __FreeBSD__
	ipfw_collect();
#endif
}
#endif
