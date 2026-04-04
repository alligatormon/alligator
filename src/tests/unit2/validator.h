#include <string.h>
#include "common/validator.h"

void test_metric_name_normalizer_statsd() {
	char buf[256];

	strcpy(buf, "foo.bar-baz_0");
	metric_name_normalizer_statsd(buf, strlen(buf));
	assert_equal_string(__FILE__, __FUNCTION__, __LINE__, "foo.bar-baz_0", buf);

	strcpy(buf, "foo:bar/baz");
	metric_name_normalizer_statsd(buf, strlen(buf));
	assert_equal_string(__FILE__, __FUNCTION__, __LINE__, "foo_bar_baz", buf);

	strcpy(buf, "a,b#c");
	metric_name_normalizer_statsd(buf, strlen(buf));
	assert_equal_string(__FILE__, __FUNCTION__, __LINE__, "a_b_c", buf);

	strcpy(buf, "");
	metric_name_normalizer_statsd(buf, 0);
	assert_equal_string(__FILE__, __FUNCTION__, __LINE__, "", buf);
}

void test_tag_normalizer_statsd() {
	char buf[256];

	strcpy(buf, "env");
	tag_normalizer_statsd(buf, strlen(buf));
	assert_equal_string(__FILE__, __FUNCTION__, __LINE__, "env", buf);

	strcpy(buf, "foo-bar_baz0");
	tag_normalizer_statsd(buf, strlen(buf));
	assert_equal_string(__FILE__, __FUNCTION__, __LINE__, "foo-bar_baz0", buf);

	strcpy(buf, "foo.bar-baz_0");
	tag_normalizer_statsd(buf, strlen(buf));
	assert_equal_string(__FILE__, __FUNCTION__, __LINE__, "foo_bar-baz_0", buf);

	strcpy(buf, "foo:bar/baz");
	tag_normalizer_statsd(buf, strlen(buf));
	assert_equal_string(__FILE__, __FUNCTION__, __LINE__, "foo_bar_baz", buf);

	strcpy(buf, "a,b#c");
	tag_normalizer_statsd(buf, strlen(buf));
	assert_equal_string(__FILE__, __FUNCTION__, __LINE__, "a_b_c", buf);

	strcpy(buf, "");
	tag_normalizer_statsd(buf, 0);
	assert_equal_string(__FILE__, __FUNCTION__, __LINE__, "", buf);
}

void test_tags_normalizer_dogstatsd() {
	char buf[256];

	strcpy(buf, "env");
	tags_normalizer_dogstatsd(buf, strlen(buf));
	assert_equal_string(__FILE__, __FUNCTION__, __LINE__, "env", buf);

	strcpy(buf, "production");
	tags_normalizer_dogstatsd(buf, strlen(buf));
	assert_equal_string(__FILE__, __FUNCTION__, __LINE__, "production", buf);

	strcpy(buf, "foo.bar-baz_0");
	tags_normalizer_dogstatsd(buf, strlen(buf));
	assert_equal_string(__FILE__, __FUNCTION__, __LINE__, "foo.bar-baz_0", buf);

	strcpy(buf, "foo:bar/baz");
	tags_normalizer_dogstatsd(buf, strlen(buf));
	assert_equal_string(__FILE__, __FUNCTION__, __LINE__, "foo_bar_baz", buf);

	strcpy(buf, "a,b#c");
	tags_normalizer_dogstatsd(buf, strlen(buf));
	assert_equal_string(__FILE__, __FUNCTION__, __LINE__, "a_b_c", buf);

	strcpy(buf, "");
	tags_normalizer_dogstatsd(buf, 0);
	assert_equal_string(__FILE__, __FUNCTION__, __LINE__, "", buf);
}
