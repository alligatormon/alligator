#include <inttypes.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#include <assert.h>

int64_t from_human_get_size(const char *hrange, size_t hsize, uint64_t *cur) {
	if (*cur == hsize)
		return 0;
	
	char *newptr = (char*)hrange + *cur;
	if (!newptr)
		return 0;

	if (!*newptr)
		return 0;

	int sz = 1;
	int64_t ret = strtoll(newptr, &newptr, 10);
	if (!strncasecmp(newptr, "ms", 2)) {
		sz = 2;
	} else if (!strncasecmp(newptr, "m", 1)) {
		ret *= 60000;
	} else if (!strncasecmp(newptr, "s", 1)) {
		ret *= 1000;
	} else if (!strncasecmp(newptr, "h", 1)) {
		ret *= 3600000;
	} else if (!strncasecmp(newptr, "d", 1)) {
		ret *= 86400000;
	} else if (!strncasecmp(newptr, "w", 1)) {
		ret *= 604800000;
	} else {
		ret *= 1000;
		sz = 0;
	}

	*cur = newptr - hrange;
	*cur += sz;
	return ret;
}

int64_t get_ms_from_human_range(const char *hrange, size_t hsize) {
	if (!hrange)
		return 0;

	int64_t ret = 0;
	for (uint64_t i = 0; i < hsize; ) {
		uint64_t pre = i;
		ret += from_human_get_size(hrange, hsize, &i);
		if (pre == i) {
			break;
		}
	}

	return ret;
}

int64_t get_sec_from_human_range(const char *hrange, size_t hsize) {
	if ((strcspn(hrange, "wmshd")) == hsize)
		return strtoll(hrange, NULL, 10);
	return get_ms_from_human_range(hrange, hsize) / 1000;
}

int quantile_window_parse(const char *s, size_t n, int64_t *out)
{
	size_t i = 0;
	int any = 0;

	if (out)
		*out = 0;
	if (!s || !out || n == 0)
		return 0;
	if (s[0] == '+' || s[0] == '-')
		i = 1;
	while (i < n) {
		unsigned char c = (unsigned char)s[i];
		if (!isdigit(c))
			return 0;
		any = 1;
		while (i < n && isdigit((unsigned char)s[i]))
			i++;
		if (i >= n)
			break;
		if (i + 1 < n && tolower((unsigned char)s[i]) == 'm' && tolower((unsigned char)s[i + 1]) == 's') {
			i += 2;
			continue;
		}
		{
			int u = tolower((unsigned char)s[i]);
			if (u == 's' || u == 'm' || u == 'h' || u == 'd' || u == 'w') {
				i++;
				continue;
			}
		}
		return 0;
	}
	if (!any)
		return 0;
	*out = get_sec_from_human_range(s, n);
	return 1;
}

//#define TEST_1 "1h12m"
//#define TEST_2 "1m15s"
//#define TEST_3 "18d64h4m12s78ms"
//#define TEST_4 "123456"
//
//void get_ms_from_human_range_test() {
//	assert(get_ms_from_human_range(TEST_1, strlen(TEST_1)) == 4320000);
//	assert(get_ms_from_human_range(TEST_2, strlen(TEST_2)) == 75000);
//	assert(get_ms_from_human_range(TEST_3, strlen(TEST_3)) == 1785852078);
//	assert(get_ms_from_human_range(TEST_4, strlen(TEST_4)) == 123456);
//}
//
//int main() {
//	get_ms_from_human_range_test();
//}
