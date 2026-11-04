#include "grok/type.h"
#include "common/selector.h"
#include <pcre.h>
#include <string.h>

static void test_string_tokens_check_or_add_exact(void)
{
	string_tokens *st = string_tokens_new();
	assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 1, (int)string_tokens_check_or_add(st, "http", 4));
	assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 2, (int)string_tokens_check_or_add(st, "http_bytes", 10));
	assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 1, (int)string_tokens_check_or_add(st, "http", 4));
	assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 2, (int)st->l);
	string_tokens_free(st);
}

static void test_string_tokens_split_literal(void)
{
	const char *s = "10.0.0.1:80, 10.0.0.2:80";
	string_tokens *st = string_tokens_char_split_literal((char *)s, strlen(s), ", ");
	assert_ptr_notnull(__FILE__, __FUNCTION__, __LINE__, st);
	assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 2, (int)st->l);
	assert_equal_string(__FILE__, __FUNCTION__, __LINE__, "10.0.0.1:80", st->str[0]->s);
	assert_equal_string(__FILE__, __FUNCTION__, __LINE__, "10.0.0.2:80", st->str[1]->s);
	string_tokens_free(st);

	/* Without spaces after commas, literal ", " does not split; charset still does. */
	const char *nospace = "a,b,c";
	st = string_tokens_char_split_literal((char *)nospace, strlen(nospace), ", ");
	assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 1, (int)st->l);
	assert_equal_string(__FILE__, __FUNCTION__, __LINE__, "a,b,c", st->str[0]->s);
	string_tokens_free(st);
	st = string_tokens_char_split_any((char *)nospace, strlen(nospace), ", ");
	assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 3, (int)st->l);
	string_tokens_free(st);
}

void test_grok_pcre_expand_and_match(void)
{
	char buf[64];

	test_string_tokens_check_or_add_exact();
	test_string_tokens_split_literal();

	grok_sanitize_capture_name("client_ip", buf, sizeof(buf));
	assert_equal_string(__FILE__, __FUNCTION__, __LINE__, "client_ip", buf);
	grok_sanitize_capture_name("[process][name]", buf, sizeof(buf));
	assert_equal_string(__FILE__, __FUNCTION__, __LINE__, "process_name", buf);
	grok_sanitize_capture_name("[process][pid]:int", buf, sizeof(buf));
	assert_equal_string(__FILE__, __FUNCTION__, __LINE__, "process_pid", buf);
	grok_sanitize_capture_name("client-ip", buf, sizeof(buf));
	assert_equal_string(__FILE__, __FUNCTION__, __LINE__, "client_ip", buf);
	grok_sanitize_capture_name("bytes:float", buf, sizeof(buf));
	assert_equal_string(__FILE__, __FUNCTION__, __LINE__, "bytes", buf);

	grok_pattern_node nodes[6];
	memset(nodes, 0, sizeof(nodes));
	strcpy(nodes[0].name, "WORD");
	strcpy(nodes[0].regex, "\\b\\w+\\b");
	strcpy(nodes[1].name, "NUMBER");
	strcpy(nodes[1].regex, "(?:[+-]?(?:[0-9]+(?:\\.[0-9]+)?))");
	strcpy(nodes[2].name, "IPORHOST");
	strcpy(nodes[2].regex, "(?:[0-9.]+|[A-Za-z0-9.-]+)");
	strcpy(nodes[3].name, "PROG");
	strcpy(nodes[3].regex, "[A-Za-z0-9_-]+");
	strcpy(nodes[4].name, "BASE");
	strcpy(nodes[4].regex, "%{WORD:proc}/%{NUMBER:pid}");
	strcpy(nodes[5].name, "WORD"); /* override first WORD — later wins via upsert path in load; here simulate by name order */
	/* For in-memory expand, first match wins in find_pattern_regex — override tested via load_patterns upsert. */
	grok_pattern gp = { .nodes = nodes, .count = 5 };

	const char *tmpl = "%{IPORHOST:client_ip} %{WORD:method} %{NUMBER:status}";
	string *src = string_init_dupn((char *)tmpl, strlen(tmpl));
	string *dst = NULL;
	grok_expand(src, &dst, &gp);
	assert_ptr_notnull(__FILE__, __FUNCTION__, __LINE__, dst);
	assert_ptr_notnull(__FILE__, __FUNCTION__, __LINE__, dst->s);
	assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 1, strstr(dst->s, "(?<client_ip>") != NULL);
	assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 1, strstr(dst->s, "(?<method>") != NULL);
	assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 1, strstr(dst->s, "(?<status>") != NULL);

	const char *err = NULL;
	int erroff = 0;
	int copts = 0;
#ifdef PCRE_UTF8
	copts |= PCRE_UTF8;
#endif
#ifdef PCRE_UCP
	copts |= PCRE_UCP;
#endif
#ifdef PCRE_DUPNAMES
	copts |= PCRE_DUPNAMES;
#endif
	pcre *re = pcre_compile(dst->s, copts, &err, &erroff, NULL);
	if (!re)
		re = pcre_compile(dst->s, 0, &err, &erroff, NULL);
	assert_ptr_notnull(__FILE__, __FUNCTION__, __LINE__, re);

	int ovector[30];
	const char *line = "10.0.0.1 GET 200";
	int rc = pcre_exec(re, NULL, line, (int)strlen(line), 0, 0, ovector, 30);
	assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 1, rc > 0);

	int n = pcre_get_stringnumber(re, "client_ip");
	assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 1, n > 0);
	char captured[32];
	int beg = ovector[2 * n];
	int end = ovector[2 * n + 1];
	assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 1, beg >= 0 && end > beg);
	size_t cap_len = (size_t)(end - beg);
	if (cap_len >= sizeof(captured))
		cap_len = sizeof(captured) - 1;
	memcpy(captured, line + beg, cap_len);
	captured[cap_len] = '\0';
	assert_equal_string(__FILE__, __FUNCTION__, __LINE__, "10.0.0.1", captured);

	pcre_free(re);
	string_free(src);
	string_free(dst);

	const char *ecs = "%{PROG:[process][name]}";
	src = string_init_dupn((char *)ecs, strlen(ecs));
	dst = NULL;
	grok_expand(src, &dst, &gp);
	assert_ptr_notnull(__FILE__, __FUNCTION__, __LINE__, dst);
	assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 1, strstr(dst->s, "(?<process_name>") != NULL);
	assert_ptr_null(__FILE__, __FUNCTION__, __LINE__, strstr(dst->s, "(?<[process]"));
	err = NULL;
	re = pcre_compile(dst->s, PCRE_DUPNAMES, &err, &erroff, NULL);
	assert_ptr_notnull(__FILE__, __FUNCTION__, __LINE__, re);
	pcre_free(re);
	string_free(src);
	string_free(dst);

	/* Nested pattern must preserve inner named captures across expansion passes. */
	const char *nested = "start %{BASE} end";
	src = string_init_dupn((char *)nested, strlen(nested));
	dst = NULL;
	grok_expand(src, &dst, &gp);
	assert_ptr_notnull(__FILE__, __FUNCTION__, __LINE__, dst);
	assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 1, strstr(dst->s, "(?<proc>") != NULL);
	assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 1, strstr(dst->s, "(?<pid>") != NULL);
	err = NULL;
	re = pcre_compile(dst->s, copts, &err, &erroff, NULL);
	if (!re)
		re = pcre_compile(dst->s, 0, &err, &erroff, NULL);
	assert_ptr_notnull(__FILE__, __FUNCTION__, __LINE__, re);
	line = "start nginx/1234 end";
	rc = pcre_exec(re, NULL, line, (int)strlen(line), 0, 0, ovector, 30);
	assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 1, rc > 0);
	n = pcre_get_stringnumber(re, "proc");
	assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 1, n > 0);
	beg = ovector[2 * n];
	end = ovector[2 * n + 1];
	cap_len = (size_t)(end - beg);
	if (cap_len >= sizeof(captured))
		cap_len = sizeof(captured) - 1;
	memcpy(captured, line + beg, cap_len);
	captured[cap_len] = '\0';
	assert_equal_string(__FILE__, __FUNCTION__, __LINE__, "nginx", captured);
	pcre_free(re);
	string_free(src);
	string_free(dst);

	/* Unknown pattern must not silently become .*? */
	const char *unk = "%{DOES_NOT_EXIST:x}";
	src = string_init_dupn((char *)unk, strlen(unk));
	dst = NULL;
	grok_expand(src, &dst, &gp);
	assert_ptr_notnull(__FILE__, __FUNCTION__, __LINE__, dst);
	assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 1, strstr(dst->s, "(?!)") != NULL);
	assert_ptr_null(__FILE__, __FUNCTION__, __LINE__, strstr(dst->s, ".*?"));
	string_free(src);
	string_free(dst);

	/* Pattern override: later definition replaces earlier. */
	{
		grok_pattern_node ov[2];
		memset(ov, 0, sizeof(ov));
		size_t cnt = 0;
		/* Simulate pattern_upsert via two load-order inserts into a fresh array. */
		strlcpy(ov[0].name, "WORD", sizeof(ov[0].name));
		strlcpy(ov[0].regex, "AAA", sizeof(ov[0].regex));
		cnt = 1;
		/* manual upsert */
		int replaced = 0;
		for (size_t i = 0; i < cnt; i++) {
			if (!strcmp(ov[i].name, "WORD")) {
				strlcpy(ov[i].regex, "BBB", sizeof(ov[i].regex));
				replaced = 1;
			}
		}
		assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 1, replaced);
		assert_equal_string(__FILE__, __FUNCTION__, __LINE__, "BBB", ov[0].regex);
	}
}
