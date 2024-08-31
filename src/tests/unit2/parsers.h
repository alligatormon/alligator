#include "parsers/multiparser.h"
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
    //fflush(stdout);

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

void api_test_parser_zookeeper_dont_work() {
    char *isro = "null";
    char *mntr = "This ZooKeeper instance is not currently serving requests\n";
    char *wchs = "This ZooKeeper instance is not currently serving requests\n";
    context_arg *carg = calloc(1, sizeof(*carg));
    zookeeper_isro_handler(isro, strlen(isro), carg);
    assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 0, carg->parser_status);

    carg->parser_status = 0;
    zookeeper_mntr_handler(mntr, strlen(mntr), carg);
    assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 0, carg->parser_status);

    carg->parser_status = 0;
    zookeeper_wchs_handler(wchs, strlen(wchs), carg);
    assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 0, carg->parser_status);
}

void api_test_parser_zookeeper() {
    char *isro = "rw";
    char *wchs = "2 connections watching 13 paths\nTotal watches:15\n";
    context_arg *carg = calloc(1, sizeof(*carg));
    zookeeper_isro_handler(isro, strlen(isro), carg);
    assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 1, carg->parser_status);

    metric_test_run(CMP_EQUAL, "zk_readwrite", "zk_readwrite", 1);

    //carg->parser_status = 0;
    //zookeeper_mntr_handler(mntr, strlen(mntr), carg);
    //assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 0, carg->parser_status);

    carg->parser_status = 0;
    zookeeper_wchs_handler(wchs, strlen(wchs), carg);
    assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 1, carg->parser_status);

    metric_test_run(CMP_EQUAL, "zk_mode", "zk_mode", 1);
}

void api_test_parser_memcached() {
    char *msg = "STAT pid 4875\r\nSTAT uptime 28\r\nSTAT time 1724863166\r\nSTAT version 1.4.15\r\nSTAT libevent 2.0.21-stable\r\nSTAT pointer_size 64\r\nSTAT rusage_user 0.001855\r\nSTAT rusage_system 0.001650\r\nSTAT curr_connections 10\r\nSTAT total_connections 12\r\nSTAT connection_structures 11\r\nSTAT reserved_fds 20\r\nSTAT cmd_get 0\r\nSTAT cmd_set 0\r\nSTAT cmd_flush 0\r\nSTAT cmd_touch 0\r\nSTAT get_hits 0\r\nSTAT get_misses 0\r\nSTAT delete_misses 0\r\nSTAT delete_hits 0\r\nSTAT incr_misses 0\r\nSTAT incr_hits 0\r\nSTAT decr_misses 0\r\nSTAT decr_hits 0\r\nSTAT cas_misses 0\r\nSTAT cas_hits 0\r\nSTAT cas_badval 0\r\nSTAT touch_hits 0\r\nSTAT touch_misses 0\r\nSTAT auth_cmds 0\r\nSTAT auth_errors 0\r\nSTAT bytes_read 12\r\nSTAT bytes_written 1023\r\nSTAT limit_maxbytes 67108864\r\nSTAT accepting_conns 1\r\nSTAT listen_disabled_num 0\r\nSTAT threads 4\r\nSTAT conn_yields 0\r\nSTAT hash_power_level 16\r\nSTAT hash_bytes 524288\r\nSTAT hash_is_expanding 0\r\nSTAT bytes 0\r\nSTAT curr_items 0\r\nSTAT total_items 0\r\nSTAT expired_unfetched 0\r\nSTAT evicted_unfetched 0\r\nSTAT evictions 0\r\nSTAT reclaimed 0\r\nEND\r\n";
    context_arg *carg = calloc(1, sizeof(*carg));
    memcached_handler(msg, strlen(msg), carg);
    assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 1, carg->parser_status);

    metric_test_run(CMP_EQUAL, "memcached_uptime", "memcached_uptime", 28);
    metric_test_run(CMP_GREATER, "memcached_time", "memcached_time", 0);
    metric_test_run(CMP_EQUAL, "memcached_version", "memcached_version", 0);
    metric_test_run(CMP_EQUAL, "memcached_libevent", "memcached_libevent", 0);
    metric_test_run(CMP_EQUAL, "memcached_pointer_size", "memcached_pointer_size", 64);
    metric_test_run(CMP_EQUAL, "memcached_rusage_user", "memcached_rusage_user", 0);
    metric_test_run(CMP_EQUAL, "memcached_rusage_system", "memcached_rusage_system", 0);
    metric_test_run(CMP_EQUAL, "memcached_curr_connections", "memcached_curr_connections", 10);
    metric_test_run(CMP_EQUAL, "memcached_total_connections", "memcached_total_connections", 12);
    metric_test_run(CMP_EQUAL, "memcached_connection_structures", "memcached_connection_structures", 11);
    metric_test_run(CMP_EQUAL, "memcached_reserved_fds", "memcached_reserved_fds", 20);
    metric_test_run(CMP_EQUAL, "memcached_cmd_get", "memcached_cmd_get", 0);
    metric_test_run(CMP_EQUAL, "memcached_cmd_set", "memcached_cmd_set", 0);
    metric_test_run(CMP_EQUAL, "memcached_cmd_flush", "memcached_cmd_flush", 0);
    metric_test_run(CMP_EQUAL, "memcached_cmd_touch", "memcached_cmd_touch", 0);
    metric_test_run(CMP_EQUAL, "memcached_get_hits", "memcached_get_hits", 0);
    metric_test_run(CMP_EQUAL, "memcached_get_misses", "memcached_get_misses", 0);
    metric_test_run(CMP_EQUAL, "memcached_delete_misses", "memcached_delete_misses", 0);
    metric_test_run(CMP_EQUAL, "memcached_delete_hits", "memcached_delete_hits", 0);
    metric_test_run(CMP_EQUAL, "memcached_incr_misses", "memcached_incr_misses", 0);
    metric_test_run(CMP_EQUAL, "memcached_incr_hits", "memcached_incr_hits", 0);
    metric_test_run(CMP_EQUAL, "memcached_decr_misses", "memcached_decr_misses", 0);
    metric_test_run(CMP_EQUAL, "memcached_decr_hits", "memcached_decr_hits", 0);
    metric_test_run(CMP_EQUAL, "memcached_cas_misses", "memcached_cas_misses", 0);
    metric_test_run(CMP_EQUAL, "memcached_cas_hits", "memcached_cas_hits", 0);
    metric_test_run(CMP_EQUAL, "memcached_cas_badval", "memcached_cas_badval", 0);
    metric_test_run(CMP_EQUAL, "memcached_touch_hits", "memcached_touch_hits", 0);
    metric_test_run(CMP_EQUAL, "memcached_touch_misses", "memcached_touch_misses", 0);
    metric_test_run(CMP_EQUAL, "memcached_auth_cmds", "memcached_auth_cmds", 0);
    metric_test_run(CMP_EQUAL, "memcached_auth_errors", "memcached_auth_errors", 0);
    metric_test_run(CMP_EQUAL, "memcached_bytes_read", "memcached_bytes_read", 12);
    metric_test_run(CMP_GREATER, "memcached_bytes_written", "memcached_bytes_written", 0);
    metric_test_run(CMP_GREATER, "memcached_limit_maxbytes", "memcached_limit_maxbytes", 0);
    metric_test_run(CMP_EQUAL, "memcached_accepting_conns", "memcached_accepting_conns", 1);
    metric_test_run(CMP_EQUAL, "memcached_listen_disabled_num", "memcached_listen_disabled_num", 0);
    metric_test_run(CMP_EQUAL, "memcached_threads", "memcached_threads", 4);
    metric_test_run(CMP_EQUAL, "memcached_conn_yields", "memcached_conn_yields", 0);
    metric_test_run(CMP_EQUAL, "memcached_hash_power_level", "memcached_hash_power_level", 16);
    metric_test_run(CMP_GREATER, "memcached_hash_bytes", "memcached_hash_bytes", 0);
    metric_test_run(CMP_EQUAL, "memcached_hash_is_expanding", "memcached_hash_is_expanding", 0);
    metric_test_run(CMP_EQUAL, "memcached_bytes", "memcached_bytes", 0);
    metric_test_run(CMP_EQUAL, "memcached_curr_items", "memcached_curr_items", 0);
    metric_test_run(CMP_EQUAL, "memcached_total_items", "memcached_total_items", 0);
    metric_test_run(CMP_EQUAL, "memcached_expired_unfetched", "memcached_expired_unfetched", 0);
    metric_test_run(CMP_EQUAL, "memcached_evicted_unfetched", "memcached_evicted_unfetched", 0);
    metric_test_run(CMP_EQUAL, "memcached_evictions", "memcached_evictions", 0);
    metric_test_run(CMP_EQUAL, "memcached_reclaimed", "memcached_reclaimed", 0);
}

void api_test_parser_beanstalkd() {
    char *msg = strdup("OK 906\r\n---\ncurrent-jobs-urgent: 0\ncurrent-jobs-ready: 0\ncurrent-jobs-reserved: 0\ncurrent-jobs-delayed: 0\ncurrent-jobs-buried: 0\ncmd-put: 0\ncmd-peek: 0\ncmd-peek-ready: 0\ncmd-peek-delayed: 0\ncmd-peek-buried: 0\ncmd-reserve: 0\ncmd-reserve-with-timeout: 0\ncmd-delete: 0\ncmd-release: 0\ncmd-use: 0\ncmd-watch: 0\ncmd-ignore: 0\ncmd-bury: 0\ncmd-kick: 0\ncmd-touch: 0\ncmd-stats: 1\ncmd-stats-job: 0\ncmd-stats-tube: 0\ncmd-list-tubes: 0\ncmd-list-tube-used: 0\ncmd-list-tubes-watched: 0\ncmd-pause-tube: 0\njob-timeouts: 0\ntotal-jobs: 0\nmax-job-size: 65535\ncurrent-tubes: 1\ncurrent-connections: 1\ncurrent-producers: 0\ncurrent-workers: 0\ncurrent-waiting: 0\ntotal-connections: 1\npid: 6838\nversion: 1.10\nrusage-utime: 0.001983\nrusage-stime: 0.000000\nuptime: 395\nbinlog-oldest-index: 0\nbinlog-current-index: 0\nbinlog-records-migrated: 0\nbinlog-records-written: 0\nbinlog-max-size: 10485760\nid: 0d4f90d5687f11fb\nhostname: test\n\r\n");
    context_arg *carg = calloc(1, sizeof(*carg));
    beanstalkd_handler(msg, strlen(msg), carg);
    assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 1, carg->parser_status);

    metric_test_run(CMP_EQUAL, "beanstalkd_current_jobs_urgent:", "beanstalkd_current_jobs_urgent:", 0);
    metric_test_run(CMP_EQUAL, "beanstalkd_current_jobs_ready", "beanstalkd_current_jobs_ready", 0);
    metric_test_run(CMP_EQUAL, "beanstalkd_current_jobs_reserved", "beanstalkd_current_jobs_reserved", 0);
    metric_test_run(CMP_EQUAL, "beanstalkd_current_jobs_delayed", "beanstalkd_current_jobs_delayed", 0);
    metric_test_run(CMP_EQUAL, "beanstalkd_current_jobs_buried", "beanstalkd_current_jobs_buried", 0);
    metric_test_run(CMP_EQUAL, "beanstalkd_cmd_put", "beanstalkd_cmd_put", 0);
    metric_test_run(CMP_EQUAL, "beanstalkd_cmd_peek", "beanstalkd_cmd_peek", 0);
    metric_test_run(CMP_EQUAL, "beanstalkd_cmd_peek_ready", "beanstalkd_cmd_peek_ready", 0);
    metric_test_run(CMP_EQUAL, "beanstalkd_cmd_peek_delayed", "beanstalkd_cmd_peek_delayed", 0);
    metric_test_run(CMP_EQUAL, "beanstalkd_cmd_peek_buried", "beanstalkd_cmd_peek_buried", 0);
    metric_test_run(CMP_EQUAL, "beanstalkd_cmd_reserve", "beanstalkd_cmd_reserve", 0);
    metric_test_run(CMP_EQUAL, "beanstalkd_cmd_reserve_with_timeout", "beanstalkd_cmd_reserve_with_timeout", 0);
    metric_test_run(CMP_EQUAL, "beanstalkd_cmd_delete", "beanstalkd_cmd_delete", 0);
    metric_test_run(CMP_EQUAL, "beanstalkd_cmd_release", "beanstalkd_cmd_release", 0);
    metric_test_run(CMP_EQUAL, "beanstalkd_cmd_use", "beanstalkd_cmd_use", 0);
    metric_test_run(CMP_EQUAL, "beanstalkd_cmd_watch", "beanstalkd_cmd_watch", 0);
    metric_test_run(CMP_EQUAL, "beanstalkd_cmd_ignore", "beanstalkd_cmd_ignore", 0);
    metric_test_run(CMP_EQUAL, "beanstalkd_cmd_bury", "beanstalkd_cmd_bury", 0);
    metric_test_run(CMP_EQUAL, "beanstalkd_cmd_kick", "beanstalkd_cmd_kick", 0);
    metric_test_run(CMP_EQUAL, "beanstalkd_cmd_touch", "beanstalkd_cmd_touch", 0);
    metric_test_run(CMP_EQUAL, "beanstalkd_cmd_stats", "beanstalkd_cmd_stats", 1);
    metric_test_run(CMP_EQUAL, "beanstalkd_cmd_stats_job", "beanstalkd_cmd_stats_job", 0);
    metric_test_run(CMP_EQUAL, "beanstalkd_cmd_stats_tube", "beanstalkd_cmd_stats_tube", 0);
    metric_test_run(CMP_EQUAL, "beanstalkd_cmd_list_tubes", "beanstalkd_cmd_list_tubes", 0);
    metric_test_run(CMP_EQUAL, "beanstalkd_cmd_list_tube_used", "beanstalkd_cmd_list_tube_used", 0);
    metric_test_run(CMP_EQUAL, "beanstalkd_cmd_list_tubes_watched", "beanstalkd_cmd_list_tubes_watched", 0);
    metric_test_run(CMP_EQUAL, "beanstalkd_cmd_pause_tube", "beanstalkd_cmd_pause_tube", 0);
    metric_test_run(CMP_EQUAL, "beanstalkd_job_timeouts", "beanstalkd_job_timeouts", 0);
    metric_test_run(CMP_EQUAL, "beanstalkd_total_jobs", "beanstalkd_total_jobs", 0);
    metric_test_run(CMP_EQUAL, "beanstalkd_max_job_size", "beanstalkd_max_job_size", 65535);
    metric_test_run(CMP_EQUAL, "beanstalkd_current_tubes", "beanstalkd_current_tubes", 1);
    metric_test_run(CMP_EQUAL, "beanstalkd_current_connections", "beanstalkd_current_connections", 1);
    metric_test_run(CMP_EQUAL, "beanstalkd_current_producers", "beanstalkd_current_producers", 0);
    metric_test_run(CMP_EQUAL, "beanstalkd_current_workers", "beanstalkd_current_workers", 0);
    metric_test_run(CMP_EQUAL, "beanstalkd_current_waiting", "beanstalkd_current_waiting", 0);
    metric_test_run(CMP_EQUAL, "beanstalkd_total_connections", "beanstalkd_total_connections", 1);
    metric_test_run(CMP_EQUAL, "beanstalkd_rusage_utime", "beanstalkd_rusage_utime", 0);
    metric_test_run(CMP_EQUAL, "beanstalkd_rusage_stime", "beanstalkd_rusage_stime", 0);
    metric_test_run(CMP_GREATER, "beanstalkd_uptime", "beanstalkd_uptime", 0);
    metric_test_run(CMP_EQUAL, "beanstalkd_binlog_oldest_index", "beanstalkd_binlog_oldest_index", 0);
    metric_test_run(CMP_EQUAL, "beanstalkd_binlog_current_index", "beanstalkd_binlog_current_index", 0);
    metric_test_run(CMP_EQUAL, "beanstalkd_binlog_records_migrated", "beanstalkd_binlog_records_migrated", 0);
    metric_test_run(CMP_EQUAL, "beanstalkd_binlog_records_written", "beanstalkd_binlog_records_written", 0);
    metric_test_run(CMP_GREATER, "beanstalkd_binlog_max_size", "beanstalkd_binlog_max_size", 0);
}

void api_test_parser_uwsgi() {
    context_arg *carg = calloc(1, sizeof(*carg));
    carg->is_http_query = 1;
    carg->full_body = string_init_dup("HTTP/1.0 200 OK\r\nConnection: close\r\nAccess-Control-Allow-Origin: *\r\nContent-Type: application/json\r\n\r\n{\n\t\"version\":\"2.0.18\",\n\t\"listen_queue\":0,\n\t\"listen_queue_errors\":0,\n\t\"signal_queue\":0,\n\t\"load\":0,\n\t\"pid\":7739,\n\t\"uid\":994,\n\t\"gid\":992,\n\t\"cwd\":\"/\",\n\t\"locks\":[\n\t\t{\n\t\t\t\"user 0\":0\n\t\t},\n\t\t{\n\t\t\t\"signal\":0\n\t\t},\n\t\t{\n\t\t\t\"filemon\":0\n\t\t},\n\t\t{\n\t\t\t\"timer\":0\n\t\t},\n\t\t{\n\t\t\t\"rbtimer\":0\n\t\t},\n\t\t{\n\t\t\t\"cron\":0\n\t\t},\n\t\t{\n\t\t\t\"rpc\":0\n\t\t},\n\t\t{\n\t\t\t\"snmp\":0\n\t\t}\n\t],\n\t\"sockets\":[\n\n\t],\n\t\"workers\":[\n\n\t]\n}");
    carg->parser_handler = uwsgi_handler;

    alligator_multiparser(carg->full_body->s, carg->full_body->l, carg->parser_handler, NULL, carg);

    assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 1, carg->parser_status);
    metric_test_run(CMP_EQUAL, "uwsgi_listen_queue", "uwsgi_listen_queue", 0);
    metric_test_run(CMP_EQUAL, "uwsgi_listen_queue_errors", "uwsgi_listen_queue_errors", 0);
    metric_test_run(CMP_EQUAL, "uwsgi_signal_queue", "uwsgi_signal_queue", 0);
    metric_test_run(CMP_EQUAL, "uwsgi_load", "uwsgi_load", 0);
    metric_test_run(CMP_EQUAL, "uwsgi_worker_accepting", "uwsgi_worker_accepting", 0);
}

void api_test_parser_lighttpd() {
    context_arg *carg = calloc(1, sizeof(*carg));
    carg->is_http_query = 1;
    carg->full_body = string_init_dup("HTTP/1.1 200 OK\r\nContent-Type: application/javascript\r\nContent-Length: 145\r\nDate: Fri, 30 Aug 2024 07:13:12 GMT\r\nServer: lighttpd/1.4.54\r\n\r\n{\n\t\"RequestsTotal\": 14,\n\t\"TrafficTotal\": 9,\n\t\"Uptime\": 78,\n\t\"BusyServers\": 1,\n\t\"IdleServers\": 127,\n\t\"RequestAverage5s\":0,\n\t\"TrafficAverage5s\":0\n}");
    carg->parser_handler = lighttpd_status_handler;

    alligator_multiparser(carg->full_body->s, carg->full_body->l, carg->parser_handler, NULL, carg);

    assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 1, carg->parser_status);
    metric_test_run(CMP_EQUAL, "lighttpd_RequestsTotal", "lighttpd_RequestsTotal", 14);
    metric_test_run(CMP_EQUAL, "lighttpd_TrafficAverage5s", "lighttpd_TrafficAverage5s", 0);
    metric_test_run(CMP_EQUAL, "lighttpd_Uptime", "lighttpd_Uptime", 78);
    metric_test_run(CMP_EQUAL, "lighttpd_TrafficTotal", "lighttpd_TrafficTotal", 9);
    metric_test_run(CMP_EQUAL, "lighttpd_RequestAverage5s", "lighttpd_RequestAverage5s", 0);
    metric_test_run(CMP_EQUAL, "lighttpd_IdleServers", "lighttpd_IdleServers", 127);

    carg->full_body = string_init_dup("HTTP/1.1 200 OK\r\nContent-Type: application/javascript\r\nContent-Length: 498\r\nDate: Fri, 30 Aug 2024 07:13:12 GMT\r\nServer: lighttpd/1.4.54\r\n\r\nfastcgi.active-requests: 0\n  fastcgi.backend.fcgi-php.0.connected: 10127\n  fastcgi.backend.fcgi-php.0.died: 0\n  fastcgi.backend.fcgi-php.0.disabled: 0\n  fastcgi.backend.fcgi-php.0.load: 0\n  fastcgi.backend.fcgi-php.0.overloaded: 0\n  fastcgi.backend.fcgi-php.1.connected: 93855\n  fastcgi.backend.fcgi-php.1.died: 0\n  fastcgi.backend.fcgi-php.1.disabled: 0\n  fastcgi.backend.fcgi-php.1.load: 0\n  fastcgi.backend.fcgi-php.1.overloaded: 0\n  fastcgi.backend.fcgi-php.load: 1\n  fastcgi.requests: 103982\n\n");
    carg->parser_handler = lighttpd_statistics_handler;

    alligator_multiparser(carg->full_body->s, carg->full_body->l, carg->parser_handler, NULL, carg);

    assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 1, carg->parser_status);
    metric_test_run(CMP_EQUAL, "lighttpd_died", "lighttpd_died", 0);
    metric_test_run(CMP_EQUAL, "lighttpd_disabled", "lighttpd_disabled", 0);
    metric_test_run(CMP_EQUAL, "lighttpd_load", "lighttpd_load", 1);
    metric_test_run(CMP_GREATER, "lighttpd_connected", "lighttpd_connected", 1);
    metric_test_run(CMP_EQUAL, "lighttpd_requests", "lighttpd_requests", 1);
}

void api_test_parser_httpd() {
    context_arg *carg = calloc(1, sizeof(*carg));
    carg->is_http_query = 1;
    carg->full_body = string_init_dup("HTTP/1.1 200 OK\r\nDate: Fri, 30 Aug 2024 11:14:04 GMT\r\nServer: Apache/2.4.6 (CentOS)\r\nContent-Length: 405\r\nContent-Type: text/plain; charset=ISO-8859-1\r\n\r\nTotal Accesses: 3\nTotal kBytes: 6\nUptime: 361\nReqPerSec: .00831025\nBytesPerSec: 17.0194\nBytesPerReq: 2048\nBusyWorkers: 1\nIdleWorkers: 4\nScoreboard: ___W_...........................................................................................................................................................................................................................................................\n");
    carg->parser_handler = httpd_status_handler;

    alligator_multiparser(carg->full_body->s, carg->full_body->l, carg->parser_handler, NULL, carg);

    assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 1, carg->parser_status);
    metric_test_run(CMP_EQUAL, "HTTPD_Total_Accesses", "HTTPD_Total_Accesses", 3);
    metric_test_run(CMP_EQUAL, "HTTPD_Total_kBytes", "HTTPD_Total_kBytes", 6);
    metric_test_run(CMP_EQUAL, "HTTPD_Uptime", "HTTPD_Uptime", 361);
    metric_test_run(CMP_EQUAL, "HTTPD_ReqPerSec", "HTTPD_ReqPerSec", 0.008310);
    metric_test_run(CMP_EQUAL, "HTTPD_BytesPerSec", "HTTPD_BytesPerSec", 17.019400);
    metric_test_run(CMP_EQUAL, "HTTPD_BytesPerReq", "HTTPD_BytesPerReq", 2048);
    metric_test_run(CMP_EQUAL, "HTTPD_BusyWorkers", "HTTPD_BusyWorkers", 1);
    metric_test_run(CMP_EQUAL, "HTTPD_IdleWorkers", "HTTPD_IdleWorkers", 4);
}

void api_test_parser_nats() {
    context_arg *carg = calloc(1, sizeof(*carg));
    carg->is_http_query = 1;
    carg->full_body = string_init_dup("HTTP/1.0 200 OK\r\nContent-Type: application/json\r\nDate: Thu, 29 Aug 2024 12:25:02 GMT\r\nContent-Length: 167\r\n\r\n{\n  \"num_subscriptions\": 0,\n  \"num_cache\": 0,\n  \"num_inserts\": 0,\n  \"num_removes\": 0,\n  \"num_matches\": 0,\n  \"cache_hit_rate\": 0,\n  \"max_fanout\": 0,\n  \"avg_fanout\": 0\n}");
    carg->parser_handler = nats_subsz_handler;

    alligator_multiparser(carg->full_body->s, carg->full_body->l, carg->parser_handler, NULL, carg);

    assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 1, carg->parser_status);

    carg = calloc(1, sizeof(*carg));
    carg->is_http_query = 1;
    carg->full_body = string_init_dup("HTTP/1.0 200 OK\r\nContent-Type: application/json\r\nDate: Thu, 29 Aug 2024 12:25:02 GMT\r\nContent-Length: 1221\r\n\r\n{\n  \"server_id\": \"NDAY27ZGD7NBXUKEZWSUOYTFMIKNCJTGAQPMOLVUNC2LS3VVCRAST3NT\",\n  \"server_name\": \"NDAY27ZGD7NBXUKEZWSUOYTFMIKNCJTGAQPMOLVUNC2LS3VVCRAST3NT\",\n  \"version\": \"2.1.9\",\n  \"proto\": 1,\n  \"git_commit\": \"7c76626\",\n  \"go\": \"go1.14.10\",\n  \"host\": \"0.0.0.0\",\n  \"port\": 4222,\n  \"max_connections\": 65536,\n  \"ping_interval\": 120000000000,\n  \"ping_max\": 2,\n  \"http_host\": \"0.0.0.0\",\n  \"http_port\": 8222,\n  \"http_base_path\": \"\",\n  \"https_port\": 0,\n  \"auth_timeout\": 1,\n  \"max_control_line\": 4096,\n  \"max_payload\": 1048576,\n  \"max_pending\": 67108864,\n  \"cluster\": {},\n  \"gateway\": {},\n  \"leaf\": {},\n  \"tls_timeout\": 0.5,\n  \"write_deadline\": 2000000000,\n  \"start\": \"2024-08-29T15:24:52.528670435+03:00\",\n  \"now\": \"2024-08-29T15:25:02.275602149+03:00\",\n  \"uptime\": \"9s\",\n  \"mem\": 4251648,\n  \"cores\": 4,\n  \"gomaxprocs\": 4,\n  \"cpu\": 0,\n  \"connections\": 0,\n  \"total_connections\": 0,\n  \"routes\": 0,\n  \"remotes\": 0,\n  \"leafnodes\": 0,\n  \"in_msgs\": 0,\n  \"out_msgs\": 0,\n  \"in_bytes\": 0,\n  \"out_bytes\": 0,\n  \"slow_consumers\": 0,\n  \"subscriptions\": 0,\n  \"http_req_stats\": {\n    \"/\": 0,\n    \"/connz\": 0,\n    \"/gatewayz\": 0,\n    \"/routez\": 0,\n    \"/subsz\": 1,\n    \"/varz\": 1\n  },\n  \"config_load_time\": \"2024-08-29T15:24:52.528670435+03:00\"\n}");
    carg->parser_handler = nats_varz_handler;

    alligator_multiparser(carg->full_body->s, carg->full_body->l, carg->parser_handler, NULL, carg);

    assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 1, carg->parser_status);
    metric_test_run(CMP_EQUAL, "nats_varz_server_id", "nats_varz_server_id", 1);
    metric_test_run(CMP_EQUAL, "nats_varz_server_name", "nats_varz_server_name", 1);
    metric_test_run(CMP_EQUAL, "nats_varz_version", "nats_varz_version", 1);
    metric_test_run(CMP_EQUAL, "nats_varz_max_connections", "nats_varz_max_connections", 65536);
    metric_test_run(CMP_EQUAL, "nats_varz_ping_max", "nats_varz_ping_max", 2);
    metric_test_run(CMP_EQUAL, "nats_varz", "nats_varz", 4251648);
    metric_test_run(CMP_EQUAL, "nats_varz_http_req_stats", "nats_varz_http_req_stats", 0);


    carg = calloc(1, sizeof(*carg));
    carg->is_http_query = 1;
    carg->full_body = string_init_dup("HTTP/1.0 200 OK\r\nContent-Type: application/json\r\nDate: Fri, 30 Aug 2024 13:09:07 GMT\r\nContent-Length: 160\r\n\r\n{\n  \"server_id\": \"NDAY27ZGD7NBXUKEZWSUOYTFMIKNCJTGAQPMOLVUNC2LS3VVCRAST3NT\",\n  \"now\": \"2024-08-30T16:09:07.546824709+03:00\",\n  \"num_routes\": 1,\n  \"routes\": [ { \"rid\": 1, \"remote_id\": \"de475c0041418afc799bccf0fdd61b47\", \"did_solicit\": true, \"ip\": \"127.0.0.1\", \"port\": 61791, \"pending_size\": 0, \"in_msgs\": 0, \"out_msgs\": 0, \"in_bytes\": 0, \"out_bytes\": 0, \"subscriptions\": 0 } ]\n}");

    carg->parser_handler = nats_routez_handler;

    alligator_multiparser(carg->full_body->s, carg->full_body->l, carg->parser_handler, NULL, carg);

    assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 1, carg->parser_status);
    metric_test_run(CMP_EQUAL, "nats_routez_server_id", "nats_routez_server_id", 1);
    metric_test_run(CMP_EQUAL, "nats_routez", "nats_routez", 1);
    metric_test_run(CMP_EQUAL, "nats_routez_now", "nats_routez_now", 1);
    metric_test_run(CMP_EQUAL, "nats_routez_num_routes", "nats_routez_num_routes", 1);
    metric_test_run(CMP_EQUAL, "nats_routez_routes_remote_id", "nats_routez_routes_remote_id", 1);
    metric_test_run(CMP_EQUAL, "nats_routez_routes_ip", "nats_routez_routes_ip", 1);
    metric_test_run(CMP_EQUAL, "nats_routez_routes_port", "nats_routez_routes_port", 61791);
    metric_test_run(CMP_EQUAL, "nats_routez_routes_in_msgs", "nats_routez_routes_in_msgs", 0);
    metric_test_run(CMP_EQUAL, "nats_routez_routes_out_msgs", "nats_routez_routes_out_msgs", 0);
    metric_test_run(CMP_EQUAL, "nats_routez_routes_in_bytes", "nats_routez_routes_in_bytes", 0);
    metric_test_run(CMP_EQUAL, "nats_routez_routes_out_bytes", "nats_routez_routes_out_bytes", 0);
    metric_test_run(CMP_EQUAL, "nats_routez_routes_subscriptions", "nats_routez_routes_subscriptions", 0);


    carg = calloc(1, sizeof(*carg));
    carg->is_http_query = 1;
    carg->full_body = string_init_dup("HTTP/1.0 200 OK\r\nContent-Type: application/json\r\nDate: Fri, 30 Aug 2024 13:09:07 GMT\r\nContent-Length: 215\r\n\r\n{\n  \"server_id\": \"NDAY27ZGD7NBXUKEZWSUOYTFMIKNCJTGAQPMOLVUNC2LS3VVCRAST3NT\",\n  \"now\": \"2024-08-30T16:09:07.54696449+03:00\",\n  \"num_connections\": 1,\n  \"total\": 1,\n  \"offset\": 0,\n  \"limit\": 1024,\n  \"connections\": [    { \"cid\": 638, \"kind\": \"Client\", \"type\": \"nats\", \"ip\": \"35.203.112.31\", \"port\": 1539, \"start\": \"2024-08-29T22:26:57.891082495Z\", \"last_activity\": \"2024-08-29T22:26:58.036462427Z\", \"rtt\": \"41.395769ms\", \"uptime\": \"15h27m52s\", \"idle\": \"15h27m52s\", \"pending_bytes\": 0, \"in_msgs\": 0, \"out_msgs\": 0, \"in_bytes\": 0, \"out_bytes\": 0, \"subscriptions\": 1, \"lang\": \"nats.js\", \"version\": \"2.12.1\", \"tls_version\": \"1.3\", \"tls_cipher_suite\": \"TLS_AES_128_GCM_SHA256\" }]\n}");
    carg->parser_handler = nats_connz_handler;

    alligator_multiparser(carg->full_body->s, carg->full_body->l, carg->parser_handler, NULL, carg);

    assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 1, carg->parser_status);
    metric_test_run(CMP_EQUAL, "nats_connz_connections_lang", "nats_connz_connections_lang", 1);
    metric_test_run(CMP_EQUAL, "nats_connz_connections_version", "nats_connz_connections_version", 1);
    metric_test_run(CMP_EQUAL, "nats_connz_connections", "nats_connz_connections", 1);
    metric_test_run(CMP_EQUAL, "nats_connz_connections_tls_version", "nats_connz_connections_tls_version", 1);
    metric_test_run(CMP_EQUAL, "nats_connz_connections_tls_cipher_suite", "nats_connz_connections_tls_cipher_suite", 1);
    metric_test_run(CMP_EQUAL, "nats_connz_connections_subscriptions", "nats_connz_connections_subscriptions", 1);
    metric_test_run(CMP_EQUAL, "nats_connz_connections_out_bytes", "nats_connz_connections_out_bytes", 0);
    metric_test_run(CMP_EQUAL, "nats_connz_connections_in_bytes", "nats_connz_connections_in_bytes", 0);
    metric_test_run(CMP_EQUAL, "nats_connz_connections_out_msgs", "nats_connz_connections_out_msgs", 0);
    metric_test_run(CMP_EQUAL, "nats_connz_connections_in_msgs", "nats_connz_connections_in_msgs", 0);
    metric_test_run(CMP_EQUAL, "nats_connz_connections_pending_bytes", "nats_connz_connections_pending_bytes", 0);
    metric_test_run(CMP_EQUAL, "nats_connz_connections_idle", "nats_connz_connections_idle", 1);
    metric_test_run(CMP_EQUAL, "nats_connz_connections_rtt", "nats_connz_connections_rtt", 1);
    metric_test_run(CMP_EQUAL, "nats_connz_connections_last_activity", "nats_connz_connections_last_activity", 1);
    metric_test_run(CMP_EQUAL, "nats_connz_connections_kind", "nats_connz_connections_kind", 1);
    metric_test_run(CMP_EQUAL, "nats_connz", "nats_connz", 1);
    metric_test_run(CMP_EQUAL, "nats_connz_server_id", "nats_connz_server_id", 1);
    metric_test_run(CMP_EQUAL, "nats_connz", "nats_connz", 1);
    metric_test_run(CMP_EQUAL, "nats_connz_now", "nats_connz_now", 1);
    metric_test_run(CMP_EQUAL, "nats_connz_total", "nats_connz_total", 1);
    metric_test_run(CMP_EQUAL, "nats_connz_offset", "nats_connz_offset", 0);
    metric_test_run(CMP_EQUAL, "nats_connz_limit", "nats_connz_limit", 1024);
    fflush(stdout);
}
