#include <stdlib.h>
#include <string.h>
#include "common/validator.h"

void test_tag_normalizer_statsd() {
	char *buf = malloc(256);
	assert_ptr_notnull(__FILE__, __FUNCTION__, __LINE__, buf);

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
	free(buf);
}


void test_tag_normalizer_dynatrace() {
	char *buf = malloc(256);
	assert_ptr_notnull(__FILE__, __FUNCTION__, __LINE__, buf);

	strcpy(buf, "env");
	tag_normalizer_dynatrace(buf, strlen(buf));
	assert_equal_string(__FILE__, __FUNCTION__, __LINE__, "env", buf);

	strcpy(buf, "foo-bar_baz0");
	tag_normalizer_dynatrace(buf, strlen(buf));
	assert_equal_string(__FILE__, __FUNCTION__, __LINE__, "foo-bar_baz0", buf);

	strcpy(buf, "foo.bar-baz_0");
	tag_normalizer_dynatrace(buf, strlen(buf));
	assert_equal_string(__FILE__, __FUNCTION__, __LINE__, "foo.bar-baz_0", buf);

	strcpy(buf, "foo.bar");
	tag_normalizer_dynatrace(buf, strlen(buf));
	assert_equal_string(__FILE__, __FUNCTION__, __LINE__, "foo.bar", buf);

	strcpy(buf, "foo:bar/baz");
	tag_normalizer_dynatrace(buf, strlen(buf));
	assert_equal_string(__FILE__, __FUNCTION__, __LINE__, "foo_bar_baz", buf);

	strcpy(buf, "a,b#c");
	tag_normalizer_dynatrace(buf, strlen(buf));
	assert_equal_string(__FILE__, __FUNCTION__, __LINE__, "a_b_c", buf);

	strcpy(buf, "");
	tag_normalizer_dynatrace(buf, 0);
	assert_equal_string(__FILE__, __FUNCTION__, __LINE__, "", buf);
	free(buf);
}

void test_validator_core() {
    char *buf = malloc(256);
    assert_ptr_notnull(__FILE__, __FUNCTION__, __LINE__, buf);

    assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 1, validate_domainname("example.org", strlen("example.org")));
    assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 0, validate_domainname("localhost", strlen("localhost")));
    assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 0, validate_domainname("bad_domain?.org", strlen("bad_domain?.org")));

    assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 1, validate_path("/tmp/alligator.sock", strlen("/tmp/alligator.sock")));
    assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 0, validate_path("tmp/alligator.sock", strlen("tmp/alligator.sock")));

    assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 1, metric_name_validator("metric_ok_01", strlen("metric_ok_01")));
    assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 0, metric_name_validator("1metric_bad", strlen("1metric_bad")));
    assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 1, metric_name_validator_promstatsd("statsd.metric-1,a:b", strlen("statsd.metric-1,a:b")));

    strcpy(buf, "bad metric/name");
    metric_name_normalizer(buf, strlen(buf));
    assert_equal_string(__FILE__, __FUNCTION__, __LINE__, "bad_metric_name", buf);

    strcpy(buf, "http.requests/total");
    prometheus_metric_name_normalizer(buf, strlen(buf));
    assert_equal_string(__FILE__, __FUNCTION__, __LINE__, "http_requests_total", buf);

    assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 1, metric_label_validator("service.name-v2", strlen("service.name-v2")));
    assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 0, metric_label_validator("service name", strlen("service name")));

    strcpy(buf, "a\\b\"c'\t");
    metric_label_value_validator_normalizer(buf, strlen(buf));
    assert_equal_string(__FILE__, __FUNCTION__, __LINE__, "a_b_c_ ", buf);

    assert_equal_int(__FILE__, __FUNCTION__, __LINE__, DATATYPE_INT, metric_value_validator("42", strlen("42")));
    assert_equal_int(__FILE__, __FUNCTION__, __LINE__, DATATYPE_INT, metric_value_validator("-7", strlen("-7")));
    assert_equal_int(__FILE__, __FUNCTION__, __LINE__, DATATYPE_DOUBLE, metric_value_validator("3.14", strlen("3.14")));
    assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 0, metric_value_validator("abc", strlen("abc")));
    assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 0, metric_value_validator("1.2.3", strlen("1.2.3")));
    free(buf);
}
