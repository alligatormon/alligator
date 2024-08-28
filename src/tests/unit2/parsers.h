#define CMP_EQUAL 0
#define CMP_GREATER 1
#define CMP_LESSER 2
void metric_test_run(int cmp_type, char *query, char *metric_name, double expected_val) {
    metric_query_context *mqc = promql_parser(NULL, query, strlen(query));
    string *body = metric_query_deserialize(1024, mqc, METRIC_SERIALIZER_JSON, 0, NULL, NULL, NULL, NULL, NULL);
    assert_ptr_notnull(__FILE__, __FUNCTION__, __LINE__, body);

    json_error_t error;
    json_t *root = json_loads(body->s, 0, &error);
    assert_ptr_notnull(__FILE__, __FUNCTION__, __LINE__, root);
    if (!root)
    {
        fprintf(stderr, "json error on line %d: %s\n'%s'\n", error.line, error.text, body->s);
        return;
    }

    uint64_t obj_size = json_array_size(root);
    for (uint64_t i = 0; i < obj_size; i++)
    {
        json_t *metric = json_array_get(root, i);
        assert_ptr_notnull(__FILE__, __FUNCTION__, __LINE__, metric);

        json_t *labels = json_object_get(metric, "labels");
        assert_ptr_notnull(__FILE__, __FUNCTION__, __LINE__, labels);
        uint64_t labels_size = json_array_size(labels);
        for (uint64_t j = 0; j < labels_size; j++)
        {
            json_t *label = json_array_get(labels, j);
            assert_ptr_notnull(__FILE__, __FUNCTION__, __LINE__, label);

            json_t *name = json_object_get(label, "name");
            assert_ptr_notnull(__FILE__, __FUNCTION__, __LINE__, name);
            const char *sname = json_string_value(name);
            assert_ptr_notnull(__FILE__, __FUNCTION__, __LINE__, sname);

            json_t *value = json_object_get(label, "value");
            assert_ptr_notnull(__FILE__, __FUNCTION__, __LINE__, value);
            const char *svalue = json_string_value(value);
            assert_ptr_notnull(__FILE__, __FUNCTION__, __LINE__, svalue);

            if (!strcmp(sname, "__name__")) {
                if (strcmp(metric_name, svalue))
                    continue;

                assert_equal_string(__FILE__, __FUNCTION__, __LINE__, metric_name, svalue);
                json_t *samples = json_object_get(metric, "samples");
                assert_ptr_notnull(__FILE__, __FUNCTION__, __LINE__, samples);

                uint64_t samples_size = json_array_size(samples);
                for (uint64_t k = 0; k < samples_size; k++)
                {
                    json_t *sample = json_array_get(samples, k);
                    assert_ptr_notnull(__FILE__, __FUNCTION__, __LINE__, sample);

                    json_t *sample_value = json_object_get(sample, "value");
                    assert_ptr_notnull(__FILE__, __FUNCTION__, __LINE__, sample_value);
                    double dsample_value = 0;
                    if (json_typeof(sample_value) == JSON_INTEGER)
                        dsample_value = json_integer_value(sample_value);
                    else if (json_typeof(sample_value) == JSON_REAL)
                        dsample_value = json_real_value(sample_value);

                    printf("check metric '%s': expected %lf, sample %lf\n", metric_name, expected_val, dsample_value);
                    fflush(stdout);
                    if (cmp_type == CMP_EQUAL)
                        assert_equal_int(__FILE__, __FUNCTION__, __LINE__, expected_val, dsample_value);
                    else if (cmp_type == CMP_GREATER)
                        assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 1, expected_val < dsample_value);
                    else if (cmp_type == CMP_LESSER)
                        assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 1, expected_val > dsample_value);
                }
            }
        }
    }
}

void api_test_parser_ntp() {
    char *msg = "\34\3\3\350\0\0j\243\0\0\22\202\n\3464#\352y\33\263\16\25\"$\0\0\0\0\0\0\0\0\352y Qo\353\277d\352y Qo\355\2z";
    context_arg *carg = calloc(1, sizeof(*carg));
    ntp_handler(msg, 48, carg);

    assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 1, carg->parser_status);

    metric_test_run(CMP_EQUAL, "ntp_stratum", "ntp_stratum", 3);
    metric_test_run(CMP_EQUAL, "ntp_leap", "ntp_leap", 0);
    metric_test_run(CMP_GREATER, "ntp_root_dispersion_seconds", "ntp_root_dispersion_seconds", 0);
    metric_test_run(CMP_GREATER, "ntp_root_delay_seconds", "ntp_root_delay_seconds", 0);
    metric_test_run(CMP_GREATER, "ntp_precision_miliseconds", "ntp_precision_miliseconds", 0);
    metric_test_run(CMP_GREATER, "ntp_reference_timestamp_seconds", "ntp_reference_timestamp_seconds", 1000);
    metric_test_run(CMP_GREATER, "ntp_rtt_seconds", "ntp_rtt_seconds", 1000);
    metric_test_run(CMP_GREATER, "ntp_root_distance_seconds", "ntp_root_distance_seconds", 1000);
}
