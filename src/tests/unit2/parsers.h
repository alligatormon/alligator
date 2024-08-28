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
    //printf("body s is '%s'\n", body->s);

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

                    int rc = 0;
                    if (cmp_type == CMP_EQUAL)
                        rc = assert_equal_int(__FILE__, __FUNCTION__, __LINE__, expected_val, dsample_value);
                    else if (cmp_type == CMP_GREATER)
                        rc = assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 1, expected_val < dsample_value);
                    else if (cmp_type == CMP_LESSER)
                        rc = assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 1, expected_val > dsample_value);

                    if (!rc) {
                        printf("checked metric '%s': expected %lf, sample %lf, rc: %d\n", metric_name, expected_val, dsample_value, rc);
                        fflush(stdout);
                    }
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

void api_test_parser_nsd() {
    char *msg = "server0.queries=0\nnum.queries=0\ntime.boot=58.714724\ntime.elapsed=58.714724\nsize.db.disk=0\nsize.db.mem=903\nsize.xfrd.mem=83950728\nsize.config.disk=0\nsize.config.mem=948\nnum.type.A=0\nnum.type.NS=0\nnum.type.MD=0\nnum.type.MF=0\nnum.type.CNAME=0\nnum.type.SOA=0\nnum.type.MB=0\nnum.type.MG=0\nnum.type.MR=0\nnum.type.NULL=0\nnum.type.WKS=0\nnum.type.PTR=0\nnum.type.HINFO=0\nnum.type.MINFO=0\nnum.type.MX=0\nnum.type.TXT=0\nnum.type.RP=0\nnum.type.AFSDB=0\nnum.type.X25=0\nnum.type.ISDN=0\nnum.type.RT=0\nnum.type.NSAP=0\nnum.type.SIG=0\nnum.type.KEY=0\nnum.type.PX=0\nnum.type.AAAA=0\nnum.type.LOC=0\nnum.type.NXT=0\nnum.type.SRV=0\nnum.type.NAPTR=0\nnum.type.KX=0\nnum.type.CERT=0\nnum.type.DNAME=0\nnum.type.OPT=0\nnum.type.APL=0\nnum.type.DS=0\nnum.type.SSHFP=0\nnum.type.IPSECKEY=0\nnum.type.RRSIG=0\nnum.type.NSEC=0\nnum.type.DNSKEY=0\nnum.type.DHCID=0\nnum.type.NSEC3=0\nnum.type.NSEC3PARAM=0\nnum.type.TLSA=0\nnum.type.SMIMEA=0\nnum.type.CDS=0\nnum.type.CDNSKEY=0\nnum.type.OPENPGPKEY=0\nnum.type.CSYNC=0\nnum.type.SPF=0\nnum.type.NID=0\nnum.type.L32=0\nnum.type.L64=0\nnum.type.LP=0\nnum.type.EUI48=0\nnum.type.EUI64=0\nnum.opcode.QUERY=0\nnum.class.IN=0\nnum.rcode.NOERROR=0\nnum.rcode.FORMERR=0\nnum.rcode.SERVFAIL=0\nnum.rcode.NXDOMAIN=0\nnum.rcode.NOTIMP=0\nnum.rcode.REFUSED=0\nnum.rcode.YXDOMAIN=0\nnum.edns=0\nnum.ednserr=0\nnum.udp=0\nnum.udp6=0\nnum.tcp=0\nnum.tcp6=0\nnum.tls=0\nnum.tls6=0\nnum.answer_wo_aa=0\nnum.rxerr=0\nnum.txerr=0\nnum.raxfr=0\nnum.truncated=0\nnum.dropped=0\nzone.master=0\nzone.slave=0\n";
    context_arg *carg = calloc(1, sizeof(*carg));
    nsd_handler(msg, strlen(msg), carg);

    assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 1, carg->parser_status);

    metric_test_run(CMP_EQUAL, "nsd_queries", "nsd_queries", 0);
    metric_test_run(CMP_EQUAL, "nsd_num", "nsd_num", 0);
    metric_test_run(CMP_EQUAL, "nsd_num_rcode", "nsd_num_rcode", 0);
    metric_test_run(CMP_EQUAL, "nsd_server0", "nsd_server0", 0);
    metric_test_run(CMP_GREATER, "nsd_size_config", "nsd_size_config", -1);
    metric_test_run(CMP_GREATER, "nsd_size_db", "nsd_size_db", -1);
    metric_test_run(CMP_GREATER, "nsd_size_xfrd", "nsd_size_xfrd", 0);
    metric_test_run(CMP_GREATER, "nsd_time", "nsd_time", 0);
    metric_test_run(CMP_EQUAL, "nsd_zone", "nsd_zone", 0);
}

void api_test_parser_syslogng() {
    char *msg = "SourceName;SourceId;SourceInstance;State;Type;Number\ndst.http;d_elasticproj_S_http_proj_K#0;http,http://srv1.es-example.com:80/_bulk;a;dropped;0\ndst.http;d_elasticproj_S_http_proj_K#0;http,http://srv1.es-example.com:80/_bulk;a;processed;39615830\ndst.http;d_elasticproj_S_http_proj_K#0;http,http://srv1.es-example.com:80/_bulk;a;queued;0\ndst.http;d_elasticproj_S_http_proj_K#0;http,http://srv1.es-example.com:80/_bulk;a;memory_usage;0\ndst.http;d_elasticproj_S_http_proj_K#0;http,http://srv1.es-example.com:80/_bulk;a;written;39615830\ndst.file;d_mesg#0;/var/log/messages;a;dropped;0\ndst.file;d_mesg#0;/var/log/messages;a;processed;43178060\ndst.file;d_mesg#0;/var/log/messages;a;queued;0\ndst.file;d_mesg#0;/var/log/messages;a;memory_usage;0\ndst.file;d_mesg#0;/var/log/messages;a;written;43178060\ndestination;d_proj_Y;;a;processed;0\nsrc.journald;s_sys#0;journal;a;processed;43223578\nsrc.journald;s_sys#0;journal;a;stamp;1724824846\ndestination;d_elasticproj_S_http_proj_Z;;a;processed;39615830\ndestination;d_kern;;a;processed;0\ndestination;d_mlal;;a;processed;0\ndestination;d_elasticproj_S_http_proj_G;;a;processed;39615830\nglobal;msg_allocated_bytes;;a;value;18446744073709550560\ndst.http;d_elasticproj_S_http_proj_H#0;http,http://srv1.es-example.com:80/_bulk;a;dropped;0\ndst.http;d_elasticproj_S_http_proj_H#0;http,http://srv1.es-example.com:80/_bulk;a;processed;39615830\ndst.http;d_elasticproj_S_http_proj_H#0;http,http://srv1.es-example.com:80/_bulk;a;queued;0\ndst.http;d_elasticproj_S_http_proj_H#0;http,http://srv1.es-example.com:80/_bulk;a;memory_usage;0\ndst.http;d_elasticproj_S_http_proj_H#0;http,http://srv1.es-example.com:80/_bulk;a;written;39615830\ndst.http;d_elasticproj_S_http_proj_S#0;http,http://srv1.es-example.com:80/_bulk;a;dropped;0\ndst.http;d_elasticproj_S_http_proj_S#0;http,http://srv1.es-example.com:80/_bulk;a;processed;39615830\ndst.http;d_elasticproj_S_http_proj_S#0;http,http://srv1.es-example.com:80/_bulk;a;queued;0\ndst.http;d_elasticproj_S_http_proj_S#0;http,http://srv1.es-example.com:80/_bulk;a;memory_usage;0\ndst.http;d_elasticproj_S_http_proj_S#0;http,http://srv1.es-example.com:80/_bulk;a;written;39615830\ndst.http;d_elasticproj_S_http_proj_H#0;http,http://srv1.es-example.com:80/_bulk;a;dropped;0\ndst.http;d_elasticproj_S_http_proj_H#0;http,http://srv1.es-example.com:80/_bulk;a;processed;39615830\ndst.http;d_elasticproj_S_http_proj_H#0;http,http://srv1.es-example.com:80/_bulk;a;queued;0\ndst.http;d_elasticproj_S_http_proj_H#0;http,http://srv1.es-example.com:80/_bulk;a;memory_usage;0\ndst.http;d_elasticproj_S_http_proj_H#0;http,http://srv1.es-example.com:80/_bulk;a;written;39615830\nparser;#anon-parser0;;a;discarded;43223578\nsrc.internal;s_sys#1;;a;processed;5377\nsrc.internal;s_sys#1;;a;stamp;1724824645\nfilter;#anon-filter0;;a;matched;0\nfilter;#anon-filter0;;a;not_matched;43223578\nfilter;#anon-filter1;;a;matched;0\nfilter;#anon-filter1;;a;not_matched;43223578\nfilter;#anon-filter2;;a;matched;17\nfilter;#anon-filter2;;a;not_matched;43223561\nfilter;f_boot;;a;matched;0\nfilter;f_boot;;a;not_matched;43228955\nfilter;#anon-filter4;;a;matched;0\nfilter;#anon-filter4;;a;not_matched;43223561\ndestination;d_elasticproj_S_http_proj_S;;a;processed;39615830\ndst.http;d_elasticproj_S_http_proj_X#0;http,http://srv1.es-example.com:80/_bulk;a;dropped;0\ndst.http;d_elasticproj_S_http_proj_X#0;http,http://srv1.es-example.com:80/_bulk;a;processed;39615830\ndst.http;d_elasticproj_S_http_proj_X#0;http,http://srv1.es-example.com:80/_bulk;a;queued;0\ndst.http;d_elasticproj_S_http_proj_X#0;http,http://srv1.es-example.com:80/_bulk;a;memory_usage;0\ndst.http;d_elasticproj_S_http_proj_X#0;http,http://srv1.es-example.com:80/_bulk;a;written;39615830\ncenter;;queued;a;processed;360155435\nfilter;#anon-filter3;;a;matched;17\nfilter;#anon-filter3;;a;not_matched;0\nfilter;f_kernel;;a;matched;0\nfilter;f_kernel;;a;not_matched;43228955\ndestination;d_auth;;a;processed;36815\nfilter;f_proj_Y;;a;matched;0\nfilter;f_proj_Y;;a;not_matched;43228955\ndestination;d_elasticproj_S_http_proj_A;;a;processed;39615830\ndst.http;d_elasticproj_S_http_proj_A#0;http,http://srv1.es-example.com:80/_bulk;a;dropped;0\ndst.http;d_elasticproj_S_http_proj_A#0;http,http://srv1.es-example.com:80/_bulk;a;processed;39615830\ndst.http;d_elasticproj_S_http_proj_A#0;http,http://srv1.es-example.com:80/_bulk;a;queued;0\ndst.http;d_elasticproj_S_http_proj_A#0;http,http://srv1.es-example.com:80/_bulk;a;memory_usage;0\ndst.http;d_elasticproj_S_http_proj_A#0;http,http://srv1.es-example.com:80/_bulk;a;written;39615830\ndestination;d_elasticproj_S_http_proj_X;;a;processed;39615830\ndestination;d_cron;;a;processed;13920\ndst.file;d_cron#0;/var/log/cron;a;dropped;0\ndst.file;d_cron#0;/var/log/cron;a;processed;13920\ndst.file;d_cron#0;/var/log/cron;a;queued;0\ndst.file;d_cron#0;/var/log/cron;a;memory_usage;0\ndst.file;d_cron#0;/var/log/cron;a;written;13920\nparser;p_docker_metadata;;a;discarded;0\ndestination;d_elasticproj_S_http_proj_H;;a;processed;39615830\ndestination;d_elasticproj_S_http_proj_K;;a;processed;39615830\nglobal;internal_queue_length;;a;processed;0\nglobal;scratch_buffers_count;;a;queued;614949122474534\nfilter;f_dockerd;;a;matched;316926640\nfilter;f_dockerd;;a;not_matched;28905000\nglobal;sdata_updates;;a;processed;0\nfilter;f_news;;a;matched;0\nfilter;f_news;;a;not_matched;43228955\nglobal;scratch_buffers_bytes;;a;queued;5632\nfilter;f_emergency;;a;matched;0\nfilter;f_emergency;;a;not_matched;43228955\ndst.file;d_auth#0;/var/log/secure;a;dropped;0\ndst.file;d_auth#0;/var/log/secure;a;processed;36815\ndst.file;d_auth#0;/var/log/secure;a;queued;0\ndst.file;d_auth#0;/var/log/secure;a;memory_usage;0\ndst.file;d_auth#0;/var/log/secure;a;written;36815\nfilter;f_default;;a;matched;43178060\nfilter;f_default;;a;not_matched;50895\ndestination;d_mesg;;a;processed;43178060\nfilter;f_auth;;a;matched;36815\nfilter;f_auth;;a;not_matched;43192140\ndst.http;d_elasticproj_S_http_proj_Z#0;http,http://srv1.es-example.com:80/_bulk;a;dropped;0\ndst.http;d_elasticproj_S_http_proj_Z#0;http,http://srv1.es-example.com:80/_bulk;a;processed;39615830\ndst.http;d_elasticproj_S_http_proj_Z#0;http,http://srv1.es-example.com:80/_bulk;a;queued;0\ndst.http;d_elasticproj_S_http_proj_Z#0;http,http://srv1.es-example.com:80/_bulk;a;memory_usage;0\ndst.http;d_elasticproj_S_http_proj_Z#0;http,http://srv1.es-example.com:80/_bulk;a;written;39615830\nsource;s_sys;;a;processed;43228955\ndestination;d_spol;;a;processed;0\ncenter;;received;a;processed;43228955\nfilter;f_cron;;a;matched;13920\nfilter;f_cron;;a;not_matched;43215035\ndestination;d_boot;;a;processed;0\ndestination;d_elasticproj_S_http_proj_H;;a;processed;39615830\nglobal;msg_clones;;a;processed;316926657\ndst.http;d_elasticproj_S_http_proj_G#0;http,http://srv1.es-example.com:80/_bulk;a;dropped;0\ndst.http;d_elasticproj_S_http_proj_G#0;http,http://srv1.es-example.com:80/_bulk;a;processed;39615830\ndst.http;d_elasticproj_S_http_proj_G#0;http,http://srv1.es-example.com:80/_bulk;a;queued;0\ndst.http;d_elasticproj_S_http_proj_G#0;http,http://srv1.es-example.com:80/_bulk;a;memory_usage;0\ndst.http;d_elasticproj_S_http_proj_G#0;http,http://srv1.es-example.com:80/_bulk;a;written;39615830\nglobal;payload_reallocs;;a;processed;127480221\n.\n";
    context_arg *carg = calloc(1, sizeof(*carg));
    nsd_handler(msg, strlen(msg), carg);

    assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 1, carg->parser_status);

    metric_test_run(CMP_EQUAL, "syslogng_stats", "syslogng_stats", 0);
}
