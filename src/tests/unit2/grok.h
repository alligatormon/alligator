#include "grok/type.h"
#include <pcre.h>
#include <string.h>

void test_grok_pcre_expand_and_match(void)
{
	char buf[64];

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

	grok_pattern_node nodes[4];
	memset(nodes, 0, sizeof(nodes));
	strcpy(nodes[0].name, "WORD");
	strcpy(nodes[0].regex, "\\b\\w+\\b");
	strcpy(nodes[1].name, "NUMBER");
	strcpy(nodes[1].regex, "(?:[+-]?(?:[0-9]+(?:\\.[0-9]+)?))");
	strcpy(nodes[2].name, "IPORHOST");
	strcpy(nodes[2].regex, "(?:[0-9.]+|[A-Za-z0-9.-]+)");
	strcpy(nodes[3].name, "PROG");
	strcpy(nodes[3].regex, "[A-Za-z0-9_-]+");
	grok_pattern gp = { .nodes = nodes, .count = 4 };

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
}
