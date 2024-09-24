#define CMP_EQUAL 0
#define CMP_GREATER 1
#define CMP_LESSER 2
#include "parsers/elasticsearch.h"

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

void api_test_parser_flower() {
    context_arg *carg = calloc(1, sizeof(*carg));
    carg->is_http_query = 1;
    carg->full_body = string_init_dup("HTTP/1.1 200 OK\r\nServer: TornadoServer/5.1.1\r\nContent-Type: text/html; charset=UTF-8\r\nDate: Sat, 31 Aug 2024 12:39:30 GMT\r\nEtag: \"ebe485c17ba96896f2b221a496312793930f68cc\"\r\nContent-Length: 8024\r\n\r\n\n<!DOCTYPE html>\n<html lang=\"en\">\n<head>\n<meta charset=\"utf-8\">\n<title>Flower</title>\n<meta name=\"viewport\" content=\"width=device-width, initial-scale=1.0\">\n<meta name=\"description\" content=\"\">\n<meta name=\"author\" content=\"\">\n<!-- bootstap overwritable styles -->\n<style type=\"text/css\">\nbody { padding-top: 60px; }\n</style>\n<link href=\"/flower/static/css/bootstrap.css?v=7b4cd90ae3b1616f1cf0b93bf5a7769b\" rel=\"stylesheet\">\n<link href=\"/flower/static/css/bootstrap-responsive.css?v=702e8485242b3ae5b4ce75a5ede13acb\" rel=\"stylesheet\">\n<link href=\"/flower/static/css/rickshaw.min.css?v=48a108292e153ef3cfb53c32283b3d2c\" rel=\"stylesheet\">\n<link href=\"/flower/static/css/jquery-ui.css?v=11ba5be990454fbbc957fcdf55e339ca\" rel=\"stylesheet\">\n<link href=\"/flower/static/css/bootstrap-datetimepicker.min.css?v=7468d8a6368bc2a64ad76bce62b9beb2\" rel=\"stylesheet\">\n<link href=\"/flower/static/css/flower.css?v=676cf30708e950453926054a5af05bbe\" rel=\"stylesheet\">\n<link href=\"/flower/static/css/jquery.dataTables.select.min.css?v=fa68b752ad69e703d08bbbe796f25fdf\" rel=\"stylesheet\">\n<link href=\"/flower/static/css/jquery.dataTables.buttons.min.css?v=01003ffa467d21831919dc93f51404c9\" rel=\"stylesheet\">\n<link href=\"/flower/static/css/jquery.dataTables.colReorder.min.css?v=e8038cbcbc4aadc9d8aeb58bb794b87a\" rel=\"stylesheet\">\n<link href=\"/flower/static/css/jquery.dataTables.css?v=907cd83f576172fa694ca8c6250fe031\" rel=\"stylesheet\">\n\n\n<!--[if lt IE 9]>\n<script src=\"http://html5shim.googlecode.com/svn/trunk/html5.js\"></script>\n<![endif]-->\n<!-- Le fav and touch icons -->\n<link rel=\"shortcut icon\" href=\"/flower/static/favicon.ico?v=fcb9ede4815b4949f75cbead02450a72\">\n<link rel=\"apple-touch-icon-precomposed\" sizes=\"144x144\" href=\"/flower/static/img/apple-touch-icon-144-precomposed.png?v=e0359104c17d82f80853ad885bb4b639\">\n<link rel=\"apple-touch-icon-precomposed\" sizes=\"114x114\" href=\"/flower/static/img/apple-touch-icon-114-precomposed.png?v=70ad8b7557de8b38434cf48741358c32\">\n<link rel=\"apple-touch-icon-precomposed\" sizes=\"72x72\" href=\"/flower/static/img/apple-touch-icon-72-precomposed.png?v=38f0dd9f645b0d61f4a75ded55ee4270\">\n<link rel=\"apple-touch-icon-precomposed\" href=\"/flower/static/img/apple-touch-icon-57-precomposed.png?v=8657301384d4507abd75822c4a197c19\">\n</head>\n<body>\n\n<div class=\"navbar navbar-fixed-top\">\n<div class=\"navbar-inner\">\n<div class=\"container-fluid\">\n<a class=\"btn btn-navbar\" data-toggle=\"collapse\" data-target=\".nav-collapse\">\n<span class=\"icon-bar\"></span>\n<span class=\"icon-bar\"></span>\n<span class=\"icon-bar\"></span>\n</a>\n<a class=\"brand\" href=\"/flower/\">Flower</a>\n<div class=\"nav-collapse collapse\">\n<ul class=\"nav\">\n<li class=\"active\"><a href=\"/flower/dashboard\">Dashboard</a></li>\n<li ><a href=\"/flower/tasks\">Tasks</a></li>\n<li ><a href=\"/flower/broker\">Broker</a></li>\n<li ><a href=\"/flower/monitor\">Monitor</a></li>\n</ul>\n<ul class=\"nav pull-right\">\n<li><a href=\"/flower/logout\">Logout</a></li>\n<li><a href=\"https://flower.readthedocs.io\" target=\"_blank\">Docs</a></li>\n<li><a href=\"https://github.com/mher/flower\" target=\"_blank\">Code</a></li>\n</ul>\n</div>\n</div>\n</div>\n</div>\n\n\n<div class=\"container-fluid\">\n<div id=\"alert\" class=\"alert alert-success hide\">\n<a class=\"close\" onclick=\"flower.on_alert_close(event)\">\303\227</a>\n<p id=\"alert-message\"></p>\n</div>\n<input type=\"hidden\" value=\"flower\" id='url_prefix'>\n</div>\n\n<div class=\"container-fluid\">\n<div class=\"btn-group btn-group-justified\">\n<a id=\"btn-active\" class=\"btn btn-default btn-large\" href=\"/flower/tasks?state=STARTED\">Active: 0</a>\n<a id=\"btn-processed\" class=\"btn btn-default btn-large\" href=\"/flower/tasks\">Processed: 638229</a>\n<a id=\"btn-failed\" class=\"btn btn-default btn-large\" href=\"/flower/tasks?state=FAILURE\">Failed: 151</a>\n<a id=\"btn-succeeded\" class=\"btn btn-default btn-large\" href=\"/flower/tasks?state=SUCCESS\">Succeeded: 636849</a>\n<a id=\"btn-retried\" class=\"btn btn-default btn-large\" href=\"/flower/tasks?state=RETRY\">Retried: 0</a>\n</div>\n<div class=\"panel panel-default\">\n<div class=\"panel-body\">\n<table id=\"workers-table\" class=\"table table-bordered table-striped\">\n<thead>\n<tr>\n<th>Worker Name</th>\n<th>Status</th>\n<th>Active</th>\n<th>Processed</th>\n<th>Failed</th>\n<th>Succeeded</th>\n<th>Retried</th>\n<th>Load Average</th>\n</tr>\n</thead>\n<tbody>\n\n<tr id=\"celery%40low.2c6025d7b72f\">\n<td>celery@low.2c6025d7b72f</td>\n<td>True</td>\n<td>0</td>\n<td>160451</td>\n<td>0</td>\n<td>160386</td>\n<td>0</td>\n<td>3.2, 2.92, 2.96</td>\n</tr>\n\n<tr id=\"celery%40normal.2c6025d7b72f\">\n<td>celery@normal.2c6025d7b72f</td>\n<td>True</td>\n<td>0</td>\n<td>159460</td>\n<td>79</td>\n<td>158711</td>\n<td>0</td>\n<td>3.2, 2.92, 2.96</td>\n</tr>\n\n<tr id=\"celery%40high.2c6025d7b72f\">\n<td>celery@high.2c6025d7b72f</td>\n<td>True</td>\n<td>0</td>\n<td>4703</td>\n<td>3</td>\n<td>4700</td>\n<td>0</td>\n<td>3.2, 2.92, 2.96</td>\n</tr>\n\n<tr id=\"celery%40high.f4d1f082914f\">\n<td>celery@high.f4d1f082914f</td>\n<td>True</td>\n<td>0</td>\n<td>5020</td>\n<td>7</td>\n<td>5013</td>\n<td>0</td>\n<td>2.37, 2.68, 2.39</td>\n</tr>\n\n<tr id=\"celery%40low.f4d1f082914f\">\n<td>celery@low.f4d1f082914f</td>\n<td>True</td>\n<td>0</td>\n<td>155121</td>\n<td>2</td>\n<td>155054</td>\n<td>0</td>\n<td>2.37, 2.68, 2.39</td>\n</tr>\n\n<tr id=\"celery%40normal.f4d1f082914f\">\n<td>celery@normal.f4d1f082914f</td>\n<td>True</td>\n<td>0</td>\n<td>153474</td>\n<td>60</td>\n<td>152985</td>\n<td>0</td>\n<td>2.37, 2.68, 2.39</td>\n</tr>\n\n</tbody>\n</table>\n</div>\n</div>\n\n<!-- Le javascript\n================================================== -->\n<!-- Placed at the end of the document so the pages load faster -->\n<script src=\"/flower/static/js/jquery-1.7.2.min.js?v=b8d64d0bc142b3f670cc0611b0aebcae\"></script>\n<script src=\"/flower/static/js/jquery-ui-1-8-15.min.js?v=88d9f574687d11e3ee3c36b97ac37ffc\"></script>\n<script src=\"/flower/static/js/jquery.dataTables.min.js?v=892dc928d97d1288cc565627f9ccd8ab\"></script>\n<script src=\"/flower/static/js/jquery.dataTables.select.min.js?v=a309458cd0106b835b1535fcc3a00e44\"></script>\n<script src=\"/flower/static/js/jquery.dataTables.buttons.min.js?v=643600aade0c3622c3a0a3ab222b8d1b\"></script>\n<script src=\"/flower/static/js/jquery.dataTables.colReorder.min.js?v=4c00a0a570998b4075456d251456ea76\"></script>\n<script src=\"/flower/static/js/bootstrap-transition.js?v=871f492dffbee74e6a7134159ac6022b\"></script>\n<script src=\"/flower/static/js/bootstrap-alert.js?v=12586670237d66f7ddc0ba3c6565faff\"></script>\n<script src=\"/flower/static/js/bootstrap-modal.js?v=5fe4c14f9cbba0a03dfd2cf0a74bb812\"></script>\n<script src=\"/flower/static/js/bootstrap-dropdown.js?v=f0a761d953362eb3c150f6ac876a4638\"></script>\n<script src=\"/flower/static/js/bootstrap-scrollspy.js?v=f7f5435ab136c477b98c1cff0a09c749\"></script>\n<script src=\"/flower/static/js/bootstrap-tab.js?v=ca0b95948cc93f4ec18cc3013726a220\"></script>\n<script src=\"/flower/static/js/bootstrap-tooltip.js?v=cbba0d4d7ed3f007b8d287f1b5eee7e8\"></script>\n<script src=\"/flower/static/js/bootstrap-popover.js?v=69df927a7b524b87ca3badade4fa4e09\"></script>\n<script src=\"/flower/static/js/bootstrap-button.js?v=8b493affaa8e27831d3162d46807b624\"></script>\n<script src=\"/flower/static/js/bootstrap-collapse.js?v=e8ddac0b5dd49cfbcf7d3ca8b0098d7b\"></script>\n<script src=\"/flower/static/js/bootstrap-carousel.js?v=fc8cbc40f39316b8b567b3b96efe9044\"></script>\n<script src=\"/flower/static/js/bootstrap-typeahead.js?v=a71319e43efd22bf29161d0e75b892b7\"></script>\n<script src=\"/flower/static/js/d3.min.js?v=eb68d3d1035789d336b285373046b550\"></script>\n<script src=\"/flower/static/js/d3.layout.min.js?v=4d73dea16077b0d7d128ecf7a4c20752\"></script>\n<script src=\"/flower/static/js/rickshaw.min.js?v=fc927b6dd64118caa563b711bcb2f130\"></script>\n<script src=\"/flower/static/js/bootstrap-datetimepicker.min.js?v=8880b6a34ee02b5cb6a75f92b3a7ddc9\"></script>\n<script src=\"/flower/static/js/moment.min.js?v=677846fe11eefd33014c1ab6ba7d6e68\"></script>\n<script src=\"/flower/static/js/moment-timezone-with-data.min.js?v=86331f4c13096151a4d330320cbc1deb\"></script>\n<script src=\"/flower/static/js/flower.js?v=24b9764780721d1eed46fa6e344d7997\"></script>\n\n\n</body>\n</html>\n");
    carg->parser_handler = flower_handler;

    alligator_multiparser(carg->full_body->s, carg->full_body->l, carg->parser_handler, NULL, carg);

    assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 1, carg->parser_status);
    metric_test_run(CMP_GREATER, "flower_tasks_active", "flower_tasks_active", -1);
    metric_test_run(CMP_GREATER, "flower_tasks_failed", "flower_tasks_failed", -1);
    metric_test_run(CMP_GREATER, "flower_tasks_processed", "flower_tasks_processed", -1);
    metric_test_run(CMP_GREATER, "flower_tasks_retried", "flower_tasks_retried", -1);
    metric_test_run(CMP_GREATER, "flower_tasks_successed", "flower_tasks_successed", -1);
    metric_test_run(CMP_GREATER, "flower_tasks_total_active", "flower_tasks_total_active", -1);
    metric_test_run(CMP_GREATER, "flower_tasks_total_failed", "flower_tasks_total_failed", -1);
    metric_test_run(CMP_EQUAL, "flower_tasks_total_processed", "flower_tasks_total_processed", 638229);
    metric_test_run(CMP_EQUAL, "flower_tasks_total_retried", "flower_tasks_total_retried", 0);
    metric_test_run(CMP_EQUAL, "flower_tasks_total_successed", "flower_tasks_total_successed", 636849);
    metric_test_run(CMP_EQUAL, "flower_worker_status", "flower_worker_status", 1);
}

void api_test_parser_rabbitmq() {
    context_arg *carg = calloc(1, sizeof(*carg));
    carg->is_http_query = 1;
    carg->full_body = string_init_dup("HTTP/1.1 200 OK\r\ncache-control: no-cache\r\nconnection: close\r\ncontent-length: 12586\r\ncontent-security-policy: script-src 'self' 'unsafe-eval' 'unsafe-inline'; object-src 'self'\r\ncontent-type: application/json\r\ndate: Thu, 12 Sep 2024 10:05:52 GMT\r\nserver: Cowboy\r\nvary: accept, accept-encoding, origin\r\n\r\n[{\"auth_mechanism\":\"PLAIN\",\"channel_max\":2047,\"channels\":1,\"client_properties\":{\"capabilities\":{\"authentication_failure_close\":true,\"basic.nack\":true,\"connection.blocked\":true,\"consumer_cancel_notify\":true,\"publisher_confirms\":true},\"information\":\"See https://github.com/mosquito/aiormq/\",\"platform\":\"CPython 3.12.5 (main build Sep  4 2024 23:14:26)\",\"product\":\"aiormq\",\"version\":\"6.8.0\"},\"connected_at\":1725890129747,\"frame_max\":131072,\"garbage_collection\":{\"fullsweep_after\":65535,\"max_heap_size\":0,\"min_bin_vheap_size\":46422,\"min_heap_size\":233,\"minor_gcs\":97},\"host\":\"192.168.0.137\",\"name\":\"192.168.0.130:53270 -> 192.168.0.137:5672\",\"node\":\"rabbit@srv1.example.com\",\"peer_cert_issuer\":null,\"peer_cert_subject\":null,\"peer_cert_validity\":null,\"peer_host\":\"192.168.0.130\",\"peer_port\":53270,\"port\":5672,\"protocol\":\"AMQP 0-9-1\",\"recv_cnt\":8324,\"recv_oct\":142334,\"recv_oct_details\":{\"rate\":1.6},\"reductions\":22233455,\"reductions_details\":{\"rate\":196.4},\"send_cnt\":8219,\"send_oct\":68101,\"send_oct_details\":{\"rate\":1.6},\"send_pend\":0,\"ssl\":false,\"ssl_cipher\":null,\"ssl_hash\":null,\"ssl_key_exchange\":null,\"ssl_protocol\":null,\"state\":\"running\",\"timeout\":60,\"type\":\"network\",\"user\":\"toolbox\",\"user_who_performed_action\":\"toolbox\",\"vhost\":\"editor\"}]");
    carg->parser_handler = rabbitmq_connections_handler;

    alligator_multiparser(carg->full_body->s, carg->full_body->l, carg->parser_handler, NULL, carg);
    assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 1, carg->parser_status);

    carg->full_body = string_init_dup("HTTP/1.1 200 OK\r\ncache-control: no-cache\r\nconnection: close\r\ncontent-length: 1340\r\ncontent-security-policy: script-src 'self' 'unsafe-eval' 'unsafe-inline'; object-src 'self'\r\ncontent-type: application/json\r\ndate: Thu, 12 Sep 2024 10:05:52 GMT\r\nserver: Cowboy\r\nvary: accept, accept-encoding, origin\r\n\r\n[{\"cluster_state\":{\"rabbit@srv1.example.com\":\"running\"},\"default_queue_type\":\"undefined\",\"description\":\"Default virtual host\",\"metadata\":{\"description\":\"Default virtual host\",\"tags\":[]},\"name\":\"/\",\"tags\":[],\"tracing\":false},{\"cluster_state\":{\"rabbit@srv1.example.com\":\"running\"},\"default_queue_type\":\"undefined\",\"description\":\"\",\"message_stats\":{\"ack\":45986,\"ack_details\":{\"rate\":0.0},\"confirm\":55483,\"confirm_details\":{\"rate\":0.0},\"deliver\":47510,\"deliver_details\":{\"rate\":0.0},\"deliver_get\":47581,\"deliver_get_details\":{\"rate\":0.0},\"deliver_no_ack\":0,\"deliver_no_ack_details\":{\"rate\":0.0},\"drop_unroutable\":0,\"drop_unroutable_details\":{\"rate\":0.0},\"get\":71,\"get_details\":{\"rate\":0.0},\"get_empty\":6,\"get_empty_details\":{\"rate\":0.0},\"get_no_ack\":0,\"get_no_ack_details\":{\"rate\":0.0},\"publish\":55484,\"publish_details\":{\"rate\":0.0},\"redeliver\":1465,\"redeliver_details\":{\"rate\":0.0},\"return_unroutable\":23,\"return_unroutable_details\":{\"rate\":0.0}},\"messages\":0,\"messages_details\":{\"rate\":0.0},\"messages_ready\":0,\"messages_ready_details\":{\"rate\":0.0},\"messages_unacknowledged\":0,\"messages_unacknowledged_details\":{\"rate\":0.0},\"metadata\":{\"description\":\"\",\"tags\":[]},\"name\":\"editor\",\"recv_oct\":406943686,\"recv_oct_details\":{\"rate\":8.0},\"send_oct\":333187900,\"send_oct_details\":{\"rate\":8.0},\"tags\":[],\"tracing\":false}]");
    carg->parser_handler = rabbitmq_vhosts_handler;

    alligator_multiparser(carg->full_body->s, carg->full_body->l, carg->parser_handler, NULL, carg);
    assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 1, carg->parser_status);

    carg->full_body = string_init_dup("HTTP/1.1 200 OK\r\ncache-control: no-cache\r\nconnection: close\r\ncontent-length: 1804\r\ncontent-security-policy: script-src 'self' 'unsafe-eval' 'unsafe-inline'; object-src 'self'\r\ncontent-type: application/json\r\ndate: Thu, 12 Sep 2024 10:05:52 GMT\r\nserver: Cowboy\r\nvary: accept, accept-encoding, origin\r\n\r\n[{\"arguments\":{\"x-dead-letter-exchange\":\"\",\"x-dead-letter-routing-key\":\"test1.dead_letter\"},\"auto_delete\":false,\"consumer_capacity\":0,\"consumer_utilisation\":0,\"consumers\":0,\"durable\":false,\"effective_policy_definition\":{},\"exclusive\":false,\"exclusive_consumer_tag\":null,\"garbage_collection\":{\"fullsweep_after\":65535,\"max_heap_size\":0,\"min_bin_vheap_size\":46422,\"min_heap_size\":233,\"minor_gcs\":133},\"head_message_timestamp\":null,\"idle_since\":\"2024-09-06T13:53:21.992+03:00\",\"memory\":10768,\"message_bytes\":0,\"message_bytes_paged_out\":0,\"message_bytes_persistent\":0,\"message_bytes_ram\":0,\"message_bytes_ready\":0,\"message_bytes_unacknowledged\":0,\"message_stats\":{\"ack\":3989,\"ack_details\":{\"rate\":0.0},\"deliver\":3989,\"deliver_details\":{\"rate\":0.0},\"deliver_get\":3989,\"deliver_get_details\":{\"rate\":0.0},\"deliver_no_ack\":0,\"deliver_no_ack_details\":{\"rate\":0.0},\"get\":0,\"get_details\":{\"rate\":0.0},\"get_empty\":0,\"get_empty_details\":{\"rate\":0.0},\"get_no_ack\":0,\"get_no_ack_details\":{\"rate\":0.0},\"publish\":3989,\"publish_details\":{\"rate\":0.0},\"redeliver\":0,\"redeliver_details\":{\"rate\":0.0}},\"messages\":0,\"messages_details\":{\"rate\":0.0},\"messages_paged_out\":0,\"messages_persistent\":0,\"messages_ram\":0,\"messages_ready\":0,\"messages_ready_details\":{\"rate\":0.0},\"messages_ready_ram\":0,\"messages_unacknowledged\":0,\"messages_unacknowledged_details\":{\"rate\":0.0},\"messages_unacknowledged_ram\":0,\"name\":\"test1\",\"node\":\"rabbit@srv1.example.com\",\"operator_policy\":null,\"policy\":null,\"recoverable_slaves\":null,\"reductions\":7477221,\"reductions_details\":{\"rate\":0.0},\"single_active_consumer_tag\":null,\"state\":\"running\",\"type\":\"classic\",\"vhost\":\"editor\"}]");
    carg->parser_handler = rabbitmq_queues_handler;

    alligator_multiparser(carg->full_body->s, carg->full_body->l, carg->parser_handler, NULL, carg);
    assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 1, carg->parser_status);

    carg->full_body = string_init_dup("HTTP/1.1 200 OK\r\ncache-control: no-cache\r\nconnection: close\r\ncontent-length: 3494\r\ncontent-security-policy: script-src 'self' 'unsafe-eval' 'unsafe-inline'; object-src 'self'\r\ncontent-type: application/json\r\ndate: Thu, 12 Sep 2024 10:05:52 GMT\r\nserver: Cowboy\r\nvary: accept, accept-encoding, origin\r\n\r\n{\"management_version\":\"3.12.13\",\"rates_mode\":\"basic\",\"sample_retention_policies\":{\"global\":[600,3600,28800,86400],\"basic\":[600,3600],\"detailed\":[600]},\"exchange_types\":[{\"name\":\"direct\",\"description\":\"AMQP direct exchange, as per the AMQP specification\",\"enabled\":true},{\"name\":\"fanout\",\"description\":\"AMQP fanout exchange, as per the AMQP specification\",\"enabled\":true},{\"name\":\"headers\",\"description\":\"AMQP headers exchange, as per the AMQP specification\",\"enabled\":true},{\"name\":\"topic\",\"description\":\"AMQP topic exchange, as per the AMQP specification\",\"enabled\":true}],\"product_version\":\"3.12.13\",\"product_name\":\"RabbitMQ\",\"rabbitmq_version\":\"3.12.13\",\"cluster_name\":\"rabbit@rabbit01\",\"erlang_version\":\"25.3.2.9\",\"erlang_full_version\":\"Erlang/OTP 25 [erts-13.2.2.6] [source] [64-bit] [smp:24:24] [ds:24:24:10] [async-threads:1]\",\"release_series_support_status\":\"supported\",\"disable_stats\":false,\"is_op_policy_updating_enabled\":true,\"enable_queue_totals\":false,\"message_stats\":{\"ack\":45986,\"ack_details\":{\"rate\":0.0},\"confirm\":55483,\"confirm_details\":{\"rate\":0.0},\"deliver\":47510,\"deliver_details\":{\"rate\":0.0},\"deliver_get\":47581,\"deliver_get_details\":{\"rate\":0.0},\"deliver_no_ack\":0,\"deliver_no_ack_details\":{\"rate\":0.0},\"disk_reads\":33627,\"disk_reads_details\":{\"rate\":0.0},\"disk_writes\":44208,\"disk_writes_details\":{\"rate\":0.0},\"drop_unroutable\":0,\"drop_unroutable_details\":{\"rate\":0.0},\"get\":71,\"get_details\":{\"rate\":0.0},\"get_empty\":6,\"get_empty_details\":{\"rate\":0.0},\"get_no_ack\":0,\"get_no_ack_details\":{\"rate\":0.0},\"publish\":55484,\"publish_details\":{\"rate\":0.0},\"redeliver\":1465,\"redeliver_details\":{\"rate\":0.0},\"return_unroutable\":23,\"return_unroutable_details\":{\"rate\":0.0}},\"churn_rates\":{\"channel_closed\":34237,\"channel_closed_details\":{\"rate\":0.0},\"channel_created\":34247,\"channel_created_details\":{\"rate\":0.0},\"connection_closed\":35511,\"connection_closed_details\":{\"rate\":0.0},\"connection_created\":34169,\"connection_created_details\":{\"rate\":0.0},\"queue_created\":51,\"queue_created_details\":{\"rate\":0.0},\"queue_declared\":1893,\"queue_declared_details\":{\"rate\":0.0},\"queue_deleted\":39,\"queue_deleted_details\":{\"rate\":0.0}},\"queue_totals\":{\"messages\":0,\"messages_details\":{\"rate\":0.0},\"messages_ready\":0,\"messages_ready_details\":{\"rate\":0.0},\"messages_unacknowledged\":0,\"messages_unacknowledged_details\":{\"rate\":0.0}},\"object_totals\":{\"channels\":10,\"connections\":10,\"consumers\":4,\"exchanges\":18,\"queues\":12},\"statistics_db_event_queue\":0,\"node\":\"rabbit@rabbit01.example.com\",\"listeners\":[{\"node\":\"rabbit@rabbit01.example.com\",\"protocol\":\"amqp\",\"ip_address\":\"::\",\"port\":5672,\"socket_opts\":{\"send_timeout\":15000}},{\"node\":\"rabbit@rabbit01.example.com\",\"protocol\":\"clustering\",\"ip_address\":\"::\",\"port\":25672,\"socket_opts\":[]},{\"node\":\"rabbit@rabbit01.example.com\",\"protocol\":\"http\",\"ip_address\":\"::\",\"port\":8080,\"socket_opts\":{\"cowboy_opts\":{\"sendfile\":false},\"port\":8080}},{\"node\":\"rabbit@rabbit01.example.com\",\"protocol\":\"http/prometheus\",\"ip_address\":\"::\",\"port\":15692,\"socket_opts\":{\"cowboy_opts\":{\"sendfile\":false},\"port\":15692,\"protocol\":\"http/prometheus\"}}],\"contexts\":[{\"ssl_opts\":[],\"node\":\"rabbit@rabbit01.example.com\",\"description\":\"RabbitMQ Management\",\"path\":\"/\",\"cowboy_opts\":\"[{sendfile,false}]\",\"port\":\"8080\"},{\"ssl_opts\":[],\"node\":\"rabbit@rabbit01.example.com\",\"description\":\"RabbitMQ Prometheus\",\"path\":\"/\",\"cowboy_opts\":\"[{sendfile,false}]\",\"port\":\"15692\",\"protocol\":\"'http/prometheus'\"}]}");
    carg->parser_handler = rabbitmq_overview_handler;

    alligator_multiparser(carg->full_body->s, carg->full_body->l, carg->parser_handler, NULL, carg);
    assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 1, carg->parser_status);

    carg->full_body = string_init_dup("HTTP/1.1 200 OK\r\ncache-control: no-cache\r\nconnection: close\r\ncontent-length: 7886\r\ncontent-security-policy: script-src 'self' 'unsafe-eval' 'unsafe-inline'; object-src 'self'\r\ncontent-type: application/json\r\ndate: Thu, 12 Sep 2024 10:05:52 GMT\r\nserver: Cowboy\r\nvary: accept, accept-encoding, origin\r\n\r\n[{\"partitions\":[],\"os_pid\":\"13246\",\"fd_total\":32768,\"sockets_total\":29401,\"mem_limit\":3000000000,\"mem_alarm\":false,\"disk_free_limit\":50000000,\"disk_free_alarm\":false,\"proc_total\":1048576,\"rates_mode\":\"basic\",\"uptime\":1967711955,\"run_queue\":1,\"processors\":24,\"exchange_types\":[{\"name\":\"headers\",\"description\":\"AMQP headers exchange, as per the AMQP specification\",\"enabled\":true},{\"name\":\"direct\",\"description\":\"AMQP direct exchange, as per the AMQP specification\",\"enabled\":true},{\"name\":\"topic\",\"description\":\"AMQP topic exchange, as per the AMQP specification\",\"enabled\":true},{\"name\":\"fanout\",\"description\":\"AMQP fanout exchange, as per the AMQP specification\",\"enabled\":true}],\"auth_mechanisms\":[{\"name\":\"RABBIT-CR-DEMO\",\"description\":\"RabbitMQ Demo challenge-response authentication mechanism\",\"enabled\":false},{\"name\":\"AMQPLAIN\",\"description\":\"QPid AMQPLAIN mechanism\",\"enabled\":true},{\"name\":\"PLAIN\",\"description\":\"SASL PLAIN authentication mechanism\",\"enabled\":true}],\"applications\":[{\"name\":\"accept\",\"description\":\"Accept header(s) for Erlang/Elixir\",\"version\":\"0.3.5\"},{\"name\":\"amqp10_common\",\"description\":\"Modules shared by rabbitmq-amqp1.0 and rabbitmq-amqp1.0-client\",\"version\":\"3.12.13\"},{\"name\":\"amqp_client\",\"description\":\"RabbitMQ AMQP Client\",\"version\":\"3.12.13\"},{\"name\":\"asn1\",\"description\":\"The Erlang ASN1 compiler version 5.0.21.1\",\"version\":\"5.0.21.1\"},{\"name\":\"aten\",\"description\":\"Erlang node failure detector\",\"version\":\"0.5.8\"},{\"name\":\"compiler\",\"description\":\"ERTS  CXC 138 10\",\"version\":\"8.2.6.3\"},{\"name\":\"cowboy\",\"description\":\"Small, fast, modern HTTP server.\",\"version\":\"2.11.0\"},{\"name\":\"cowlib\",\"description\":\"Support library for manipulating Web protocols.\",\"version\":\"2.12.1\"},{\"name\":\"credentials_obfuscation\",\"description\":\"Helper library that obfuscates sensitive values in process state\",\"version\":\"3.4.0\"},{\"name\":\"crypto\",\"description\":\"CRYPTO\",\"version\":\"5.1.4.1\"},{\"name\":\"cuttlefish\",\"description\":\"cuttlefish configuration abstraction\",\"version\":\"3.1.0\"},{\"name\":\"enough\",\"description\":\"A gen_server implementation with additional, overload-protected call type\",\"version\":\"0.1.0\"},{\"name\":\"gen_batch_server\",\"description\":\"Generic batching server\",\"version\":\"0.8.8\"},{\"name\":\"inets\",\"description\":\"INETS  CXC 138 49\",\"version\":\"8.3.1.2\"},{\"name\":\"kernel\",\"description\":\"ERTS  CXC 138 10\",\"version\":\"8.5.4.2\"},{\"name\":\"mnesia\",\"description\":\"MNESIA  CXC 138 12\",\"version\":\"4.21.4.2\"},{\"name\":\"observer_cli\",\"description\":\"Visualize Erlang Nodes On The Command Line\",\"version\":\"1.7.3\"},{\"name\":\"os_mon\",\"description\":\"CPO  CXC 138 46\",\"version\":\"2.8.2\"},{\"name\":\"osiris\",\"description\":\"Foundation of the log-based streaming subsystem for RabbitMQ\",\"version\":\"1.7.2\"},{\"name\":\"prometheus\",\"description\":\"Prometheus.io client in Erlang\",\"version\":\"4.11.0\"},{\"name\":\"public_key\",\"description\":\"Public key infrastructure\",\"version\":\"1.13.3.2\"},{\"name\":\"ra\",\"description\":\"Raft library\",\"version\":\"2.6.3\"},{\"name\":\"rabbit\",\"description\":\"RabbitMQ\",\"version\":\"3.12.13\"},{\"name\":\"rabbit_common\",\"description\":\"Modules shared by rabbitmq-server and rabbitmq-erlang-client\",\"version\":\"3.12.13\"},{\"name\":\"rabbitmq_management\",\"description\":\"RabbitMQ Management Console\",\"version\":\"3.12.13\"},{\"name\":\"rabbitmq_management_agent\",\"description\":\"RabbitMQ Management Agent\",\"version\":\"3.12.13\"},{\"name\":\"rabbitmq_prelaunch\",\"description\":\"RabbitMQ prelaunch setup\",\"version\":\"3.12.13\"},{\"name\":\"rabbitmq_prometheus\",\"description\":\"Prometheus metrics for RabbitMQ\",\"version\":\"3.12.13\"},{\"name\":\"rabbitmq_web_dispatch\",\"description\":\"RabbitMQ Web Dispatcher\",\"version\":\"3.12.13\"},{\"name\":\"ranch\",\"description\":\"Socket acceptor pool for TCP protocols.\",\"version\":\"2.1.0\"},{\"name\":\"recon\",\"description\":\"Diagnostic tools for production use\",\"version\":\"2.5.3\"},{\"name\":\"redbug\",\"description\":\"Erlang Tracing Debugger\",\"version\":\"2.0.7\"},{\"name\":\"runtime_tools\",\"description\":\"RUNTIME_TOOLS\",\"version\":\"1.19\"},{\"name\":\"sasl\",\"description\":\"SASL  CXC 138 11\",\"version\":\"4.2\"},{\"name\":\"seshat\",\"description\":\"Counters registry\",\"version\":\"0.6.1\"},{\"name\":\"ssl\",\"description\":\"Erlang/OTP SSL application\",\"version\":\"10.9.1.3\"},{\"name\":\"stdlib\",\"description\":\"ERTS  CXC 138 10\",\"version\":\"4.3.1.3\"},{\"name\":\"stdout_formatter\",\"description\":\"Tools to format paragraphs, lists and tables as plain text\",\"version\":\"0.2.4\"},{\"name\":\"syntax_tools\",\"description\":\"Syntax tools\",\"version\":\"3.0.1\"},{\"name\":\"sysmon_handler\",\"description\":\"Rate-limiting system_monitor event handler\",\"version\":\"1.3.0\"},{\"name\":\"systemd\",\"description\":\"systemd integration for Erlang applications\",\"version\":\"0.6.1\"},{\"name\":\"thoas\",\"description\":\"A blazing fast JSON parser and generator in pure Erlang.\",\"version\":\"1.0.0\"},{\"name\":\"tools\",\"description\":\"DEVTOOLS  CXC 138 16\",\"version\":\"3.5.3\"},{\"name\":\"xmerl\",\"description\":\"XML parser\",\"version\":\"1.3.31.1\"}],\"contexts\":[{\"description\":\"RabbitMQ Prometheus\",\"path\":\"/\",\"cowboy_opts\":\"[{sendfile,false}]\",\"port\":\"15692\",\"protocol\":\"'http/prometheus'\"},{\"description\":\"RabbitMQ Management\",\"path\":\"/\",\"cowboy_opts\":\"[{sendfile,false}]\",\"port\":\"8080\"}],\"log_files\":[\"/var/log/rabbitmq/rabbit@rabbit01.example.com.log\",\"<stdout>\"],\"db_dir\":\"/var/lib/rabbitmq/mnesia/rabbit@rabbit01.example.com\",\"config_files\":[\"/etc/rabbitmq/rabbitmq.conf\"],\"net_ticktime\":60,\"enabled_plugins\":[\"rabbitmq_management\",\"rabbitmq_prometheus\"],\"mem_calculation_strategy\":\"rss\",\"ra_open_file_metrics\":{\"ra_log_wal\":0,\"ra_log_segment_writer\":0},\"name\":\"rabbit@rabbit01.example.com\",\"type\":\"disc\",\"running\":true,\"being_drained\":false,\"mem_used\":136110080,\"mem_used_details\":{\"rate\":38502.4},\"fd_used\":56,\"fd_used_details\":{\"rate\":0.0},\"sockets_used\":10,\"sockets_used_details\":{\"rate\":0.0},\"proc_used\":580,\"proc_used_details\":{\"rate\":0.0},\"disk_free\":96644370432,\"disk_free_details\":{\"rate\":0.0},\"gc_num\":52944994,\"gc_num_details\":{\"rate\":92.4},\"gc_bytes_reclaimed\":1017637643424,\"gc_bytes_reclaimed_details\":{\"rate\":2470484.8},\"context_switches\":131033404,\"context_switches_details\":{\"rate\":162.6},\"io_read_count\":25381,\"io_read_count_details\":{\"rate\":0.0},\"io_read_bytes\":628662853,\"io_read_bytes_details\":{\"rate\":0.0},\"io_read_avg_time\":0.15971782041684723,\"io_read_avg_time_details\":{\"rate\":0.0},\"io_write_count\":17914,\"io_write_count_details\":{\"rate\":0.0},\"io_write_bytes\":389663220,\"io_write_bytes_details\":{\"rate\":0.0},\"io_write_avg_time\":0.34728860109411636,\"io_write_avg_time_details\":{\"rate\":0.0},\"io_sync_count\":17584,\"io_sync_count_details\":{\"rate\":0.0},\"io_sync_avg_time\":69.21168027752502,\"io_sync_avg_time_details\":{\"rate\":0.0},\"io_seek_count\":4138,\"io_seek_count_details\":{\"rate\":0.0},\"io_seek_avg_time\":0.11539318511358145,\"io_seek_avg_time_details\":{\"rate\":0.0},\"io_reopen_count\":0,\"io_reopen_count_details\":{\"rate\":0.0},\"mnesia_ram_tx_count\":180531,\"mnesia_ram_tx_count_details\":{\"rate\":0.0},\"mnesia_disk_tx_count\":91,\"mnesia_disk_tx_count_details\":{\"rate\":0.0},\"msg_store_read_count\":31564,\"msg_store_read_count_details\":{\"rate\":0.0},\"msg_store_write_count\":41671,\"msg_store_write_count_details\":{\"rate\":0.0},\"queue_index_write_count\":327,\"queue_index_write_count_details\":{\"rate\":0.0},\"queue_index_read_count\":97,\"queue_index_read_count_details\":{\"rate\":0.0},\"connection_created\":34169,\"connection_created_details\":{\"rate\":0.0},\"connection_closed\":35511,\"connection_closed_details\":{\"rate\":0.0},\"channel_created\":34247,\"channel_created_details\":{\"rate\":0.0},\"channel_closed\":34237,\"channel_closed_details\":{\"rate\":0.0},\"queue_declared\":1893,\"queue_declared_details\":{\"rate\":0.0},\"queue_created\":51,\"queue_created_details\":{\"rate\":0.0},\"queue_deleted\":39,\"queue_deleted_details\":{\"rate\":0.0},\"cluster_links\":[],\"metrics_gc_queue_length\":{\"connection_closed\":0,\"channel_closed\":0,\"consumer_deleted\":0,\"exchange_deleted\":0,\"queue_deleted\":0,\"vhost_deleted\":0,\"node_node_deleted\":0,\"channel_consumer_deleted\":0}}]");
    carg->parser_handler = rabbitmq_nodes_handler;

    alligator_multiparser(carg->full_body->s, carg->full_body->l, carg->parser_handler, NULL, carg);
    assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 1, carg->parser_status);

    carg->full_body = string_init_dup( "HTTP/1.1 200 OK\r\ncache-control: no-cache\r\nconnection: close\r\ncontent-length: 2269\r\ncontent-security-policy: script-src 'self' 'unsafe-eval' 'unsafe-inline'; object-src 'self'\r\ncontent-type: application/json\r\ndate: Thu, 12 Sep 2024 10:05:52 GMT\r\nserver: Cowboy\r\nvary: accept, accept-encoding, origin\r\n\r\n[{\"arguments\":{},\"auto_delete\":false,\"durable\":true,\"internal\":false,\"name\":\"\",\"type\":\"direct\",\"user_who_performed_action\":\"rmq-cli\",\"vhost\":\"editor\"},{\"arguments\":{},\"auto_delete\":false,\"durable\":true,\"internal\":false,\"name\":\"amq.direct\",\"type\":\"direct\",\"user_who_performed_action\":\"rmq-cli\",\"vhost\":\"editor\"},{\"arguments\":{},\"auto_delete\":false,\"durable\":true,\"internal\":false,\"name\":\"amq.fanout\",\"type\":\"fanout\",\"user_who_performed_action\":\"rmq-cli\",\"vhost\":\"editor\"},{\"arguments\":{},\"auto_delete\":false,\"durable\":true,\"internal\":false,\"name\":\"amq.headers\",\"type\":\"headers\",\"user_who_performed_action\":\"rmq-cli\",\"vhost\":\"editor\"},{\"arguments\":{},\"auto_delete\":false,\"durable\":true,\"internal\":false,\"name\":\"amq.match\",\"type\":\"headers\",\"user_who_performed_action\":\"rmq-cli\",\"vhost\":\"editor\"},{\"arguments\":{},\"auto_delete\":false,\"durable\":true,\"internal\":true,\"name\":\"amq.rabbitmq.trace\",\"type\":\"topic\",\"user_who_performed_action\":\"rmq-cli\",\"vhost\":\"editor\"},{\"arguments\":{},\"auto_delete\":false,\"durable\":true,\"internal\":false,\"name\":\"amq.topic\",\"type\":\"topic\",\"user_who_performed_action\":\"rmq-cli\",\"vhost\":\"editor\"},{\"arguments\":{},\"auto_delete\":false,\"durable\":false,\"internal\":false,\"message_stats\":{\"publish_in\":7455,\"publish_in_details\":{\"rate\":0.0},\"publish_out\":7445,\"publish_out_details\":{\"rate\":0.0}},\"name\":\"test.local\",\"type\":\"topic\",\"user_who_performed_action\":\"test\",\"vhost\":\"editor\"},{\"arguments\":{},\"auto_delete\":false,\"durable\":false,\"internal\":false,\"message_stats\":{\"publish_in\":11473,\"publish_in_details\":{\"rate\":0.0},\"publish_out\":11473,\"publish_out_details\":{\"rate\":0.0}},\"name\":\"test.stage\",\"type\":\"topic\",\"user_who_performed_action\":\"test\",\"vhost\":\"editor\"},{\"arguments\":{},\"auto_delete\":false,\"durable\":false,\"internal\":false,\"message_stats\":{\"publish_in\":4816,\"publish_in_details\":{\"rate\":0.0},\"publish_out\":4816,\"publish_out_details\":{\"rate\":0.0}},\"name\":\"test.stage1\",\"type\":\"topic\",\"user_who_performed_action\":\"test\",\"vhost\":\"editor\"},{\"arguments\":{},\"auto_delete\":false,\"durable\":false,\"internal\":false,\"message_stats\":{\"publish_in\":4804,\"publish_in_details\":{\"rate\":0.0},\"publish_out\":4804,\"publish_out_details\":{\"rate\":0.0}},\"name\":\"test.stage2\",\"type\":\"topic\",\"user_who_performed_action\":\"test\",\"vhost\":\"editor\"}]");
    carg->parser_handler = rabbitmq_exchanges_handler;

    alligator_multiparser(carg->full_body->s, carg->full_body->l, carg->parser_handler, NULL, carg);
    assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 1, carg->parser_status);


    metric_test_run(CMP_EQUAL, "rabbitmq_churn_rates_channel_closed", "rabbitmq_churn_rates_channel_closed", 34237);
    metric_test_run(CMP_EQUAL, "rabbitmq_churn_rates_channel_closed_details_rate", "rabbitmq_churn_rates_channel_closed_details_rate", 0);
    metric_test_run(CMP_EQUAL, "rabbitmq_churn_rates_channel_created", "rabbitmq_churn_rates_channel_created", 34247);
    metric_test_run(CMP_EQUAL, "rabbitmq_churn_rates_channel_created_details_rate", "rabbitmq_churn_rates_channel_created_details_rate", 0);
    metric_test_run(CMP_EQUAL, "rabbitmq_churn_rates_connection_closed", "rabbitmq_churn_rates_connection_closed", 35511);
    metric_test_run(CMP_EQUAL, "rabbitmq_churn_rates_connection_closed_details_rate", "rabbitmq_churn_rates_connection_closed_details_rate", 0);
    metric_test_run(CMP_EQUAL, "rabbitmq_churn_rates_connection_created", "rabbitmq_churn_rates_connection_created", 34169);
    metric_test_run(CMP_EQUAL, "rabbitmq_churn_rates_connection_created_details_rate", "rabbitmq_churn_rates_connection_created_details_rate", 0);
    metric_test_run(CMP_EQUAL, "rabbitmq_churn_rates_queue_created", "rabbitmq_churn_rates_queue_created", 51);
    metric_test_run(CMP_EQUAL, "rabbitmq_churn_rates_queue_created_details_rate", "rabbitmq_churn_rates_queue_created_details_rate", 0);
    metric_test_run(CMP_EQUAL, "rabbitmq_churn_rates_queue_declared", "rabbitmq_churn_rates_queue_declared", 1893);
    metric_test_run(CMP_EQUAL, "rabbitmq_churn_rates_queue_declared_details_rate", "rabbitmq_churn_rates_queue_declared_details_rate", 0);
    metric_test_run(CMP_EQUAL, "rabbitmq_churn_rates_queue_deleted", "rabbitmq_churn_rates_queue_deleted", 39);
    metric_test_run(CMP_EQUAL, "rabbitmq_churn_rates_queue_deleted_details_rate", "rabbitmq_churn_rates_queue_deleted_details_rate", 0);
    metric_test_run(CMP_EQUAL, "rabbitmq_connections_channel_max", "rabbitmq_connections_channel_max", 2047);
    metric_test_run(CMP_EQUAL, "rabbitmq_connections_channels", "rabbitmq_connections_channels", 1);
    metric_test_run(CMP_EQUAL, "rabbitmq_connections_connected_at", "rabbitmq_connections_connected_at", 1725890129747);
    metric_test_run(CMP_EQUAL, "rabbitmq_connections_frame_max", "rabbitmq_connections_frame_max", 131072);
    metric_test_run(CMP_EQUAL, "rabbitmq_connections_gc_fullsweep_after", "rabbitmq_connections_gc_fullsweep_after", 65535);
    metric_test_run(CMP_GREATER, "rabbitmq_connections_gc_max_heap_size", "rabbitmq_connections_gc_max_heap_size", -1);
    metric_test_run(CMP_GREATER, "rabbitmq_connections_gc_min_bin_vheap_size", "rabbitmq_connections_gc_min_bin_vheap_size", 232);
    metric_test_run(CMP_EQUAL, "rabbitmq_connections_gc_min_heap_size", "rabbitmq_connections_gc_min_heap_size", 233);
    metric_test_run(CMP_EQUAL, "rabbitmq_connections_gc_minor_gcs", "rabbitmq_connections_gc_minor_gcs", 97);
    metric_test_run(CMP_EQUAL, "rabbitmq_connections_peer_port", "rabbitmq_connections_peer_port", 53270);
    metric_test_run(CMP_EQUAL, "rabbitmq_connections_port", "rabbitmq_connections_port", 5672);
    metric_test_run(CMP_EQUAL, "rabbitmq_connections_recv_cnt", "rabbitmq_connections_recv_cnt", 8324);
    metric_test_run(CMP_EQUAL, "rabbitmq_connections_recv_oct", "rabbitmq_connections_recv_oct", 142334);
    metric_test_run(CMP_EQUAL, "rabbitmq_connections_reductions", "rabbitmq_connections_reductions", 22233455);
    metric_test_run(CMP_EQUAL, "rabbitmq_connections_send_cnt", "rabbitmq_connections_send_cnt", 8219);
    metric_test_run(CMP_EQUAL, "rabbitmq_connections_send_oct", "rabbitmq_connections_send_oct", 68101);
    metric_test_run(CMP_EQUAL, "rabbitmq_connections_send_pend", "rabbitmq_connections_send_pend", 0);
    metric_test_run(CMP_EQUAL, "rabbitmq_connections_ssl", "rabbitmq_connections_ssl", 0);
    metric_test_run(CMP_EQUAL, "rabbitmq_connections_state", "rabbitmq_connections_state", 1);
    metric_test_run(CMP_EQUAL, "rabbitmq_connections_timeout", "rabbitmq_connections_timeout", 60);
    metric_test_run(CMP_EQUAL, "rabbitmq_disable_stats", "rabbitmq_disable_stats", 0);
    metric_test_run(CMP_EQUAL, "rabbitmq_enable_queue_totals", "rabbitmq_enable_queue_totals", 0);
    metric_test_run(CMP_EQUAL, "rabbitmq_exchanges_auto_delete", "rabbitmq_exchanges_auto_delete", 0);
    metric_test_run(CMP_GREATER, "rabbitmq_exchanges_durable", "rabbitmq_exchanges_durable", -1);
    metric_test_run(CMP_GREATER, "rabbitmq_exchanges_internal", "rabbitmq_exchanges_internal", -1);
    metric_test_run(CMP_GREATER, "rabbitmq_exchanges_publish_in", "rabbitmq_exchanges_publish_in", 4803);
    metric_test_run(CMP_GREATER, "rabbitmq_exchanges_publish_out", "rabbitmq_exchanges_publish_out", 4803);
    metric_test_run(CMP_EQUAL, "rabbitmq_is_op_policy_updating_enabled", "rabbitmq_is_op_policy_updating_enabled", 1);
    metric_test_run(CMP_EQUAL, "rabbitmq_message_stats_ack", "rabbitmq_message_stats_ack", 45986);
    metric_test_run(CMP_EQUAL, "rabbitmq_message_stats_ack_details_rate", "rabbitmq_message_stats_ack_details_rate", 0);
    metric_test_run(CMP_EQUAL, "rabbitmq_message_stats_confirm", "rabbitmq_message_stats_confirm", 55483);
    metric_test_run(CMP_EQUAL, "rabbitmq_message_stats_confirm_details_rate", "rabbitmq_message_stats_confirm_details_rate", 0);
    metric_test_run(CMP_EQUAL, "rabbitmq_message_stats_deliver", "rabbitmq_message_stats_deliver", 47510);
    metric_test_run(CMP_EQUAL, "rabbitmq_message_stats_deliver_details_rate", "rabbitmq_message_stats_deliver_details_rate", 0);
    metric_test_run(CMP_EQUAL, "rabbitmq_message_stats_deliver_get", "rabbitmq_message_stats_deliver_get", 47581);
    metric_test_run(CMP_EQUAL, "rabbitmq_message_stats_deliver_get_details_rate", "rabbitmq_message_stats_deliver_get_details_rate", 0);
    metric_test_run(CMP_EQUAL, "rabbitmq_message_stats_deliver_no_ack", "rabbitmq_message_stats_deliver_no_ack", 0);
    metric_test_run(CMP_EQUAL, "rabbitmq_message_stats_deliver_no_ack_details_rate", "rabbitmq_message_stats_deliver_no_ack_details_rate", 0);
    metric_test_run(CMP_EQUAL, "rabbitmq_message_stats_disk_reads", "rabbitmq_message_stats_disk_reads", 33627);
    metric_test_run(CMP_EQUAL, "rabbitmq_message_stats_disk_reads_details_rate", "rabbitmq_message_stats_disk_reads_details_rate", 0);
    metric_test_run(CMP_EQUAL, "rabbitmq_message_stats_disk_writes", "rabbitmq_message_stats_disk_writes", 44208);
    metric_test_run(CMP_EQUAL, "rabbitmq_message_stats_disk_writes_details_rate", "rabbitmq_message_stats_disk_writes_details_rate", 0);
    metric_test_run(CMP_EQUAL, "rabbitmq_message_stats_drop_unroutable", "rabbitmq_message_stats_drop_unroutable", 0);
    metric_test_run(CMP_EQUAL, "rabbitmq_message_stats_drop_unroutable_details_rate", "rabbitmq_message_stats_drop_unroutable_details_rate", 0);
    metric_test_run(CMP_EQUAL, "rabbitmq_message_stats_get", "rabbitmq_message_stats_get", 71);
    metric_test_run(CMP_EQUAL, "rabbitmq_message_stats_get_details_rate", "rabbitmq_message_stats_get_details_rate", 0);
    metric_test_run(CMP_EQUAL, "rabbitmq_message_stats_get_empty", "rabbitmq_message_stats_get_empty", 6);
    metric_test_run(CMP_EQUAL, "rabbitmq_message_stats_get_empty_details_rate", "rabbitmq_message_stats_get_empty_details_rate", 0);
    metric_test_run(CMP_EQUAL, "rabbitmq_message_stats_get_no_ack", "rabbitmq_message_stats_get_no_ack", 0);
    metric_test_run(CMP_EQUAL, "rabbitmq_message_stats_get_no_ack_details_rate", "rabbitmq_message_stats_get_no_ack_details_rate", 0);
    metric_test_run(CMP_EQUAL, "rabbitmq_message_stats_publish", "rabbitmq_message_stats_publish", 55484);
    metric_test_run(CMP_EQUAL, "rabbitmq_message_stats_publish_details_rate", "rabbitmq_message_stats_publish_details_rate", 0);
    metric_test_run(CMP_EQUAL, "rabbitmq_message_stats_redeliver", "rabbitmq_message_stats_redeliver", 1465);
    metric_test_run(CMP_EQUAL, "rabbitmq_message_stats_redeliver_details_rate", "rabbitmq_message_stats_redeliver_details_rate", 0);
    metric_test_run(CMP_EQUAL, "rabbitmq_message_stats_return_unroutable", "rabbitmq_message_stats_return_unroutable", 23);
    metric_test_run(CMP_EQUAL, "rabbitmq_message_stats_return_unroutable_details_rate", "rabbitmq_message_stats_return_unroutable_details_rate", 0);
    metric_test_run(CMP_EQUAL, "rabbitmq_nodes_being_drained", "rabbitmq_nodes_being_drained", 0);
    metric_test_run(CMP_GREATER, "rabbitmq_nodes_channel_closed", "rabbitmq_nodes_channel_closed", 0);
    metric_test_run(CMP_EQUAL, "rabbitmq_nodes_channel_created", "rabbitmq_nodes_channel_created", 34247);
    metric_test_run(CMP_EQUAL, "rabbitmq_nodes_connection_closed", "rabbitmq_nodes_connection_closed", 35511);
    metric_test_run(CMP_EQUAL, "rabbitmq_nodes_connection_created", "rabbitmq_nodes_connection_created", 34169);
    metric_test_run(CMP_EQUAL, "rabbitmq_nodes_context_switches", "rabbitmq_nodes_context_switches", 131033404);
    metric_test_run(CMP_EQUAL, "rabbitmq_nodes_disk_free", "rabbitmq_nodes_disk_free", 96644370432);
    metric_test_run(CMP_EQUAL, "rabbitmq_nodes_disk_free_alarm", "rabbitmq_nodes_disk_free_alarm", 0);
    metric_test_run(CMP_EQUAL, "rabbitmq_nodes_disk_free_limit", "rabbitmq_nodes_disk_free_limit", 50000000);
    metric_test_run(CMP_EQUAL, "rabbitmq_nodes_fd_total", "rabbitmq_nodes_fd_total", 32768);
    metric_test_run(CMP_EQUAL, "rabbitmq_nodes_fd_used", "rabbitmq_nodes_fd_used", 56);
    metric_test_run(CMP_EQUAL, "rabbitmq_nodes_gc_bytes_reclaimed", "rabbitmq_nodes_gc_bytes_reclaimed", 1017637643424);
    metric_test_run(CMP_EQUAL, "rabbitmq_nodes_gc_num", "rabbitmq_nodes_gc_num", 52944994);
    metric_test_run(CMP_EQUAL, "rabbitmq_nodes_gc_queue_length_channel_closed", "rabbitmq_nodes_gc_queue_length_channel_closed", 0);
    metric_test_run(CMP_EQUAL, "rabbitmq_nodes_gc_queue_length_channel_consumer_deleted", "rabbitmq_nodes_gc_queue_length_channel_consumer_deleted", 0);
    metric_test_run(CMP_EQUAL, "rabbitmq_nodes_gc_queue_length_connection_closed", "rabbitmq_nodes_gc_queue_length_connection_closed", 0);
    metric_test_run(CMP_EQUAL, "rabbitmq_nodes_gc_queue_length_consumer_deleted", "rabbitmq_nodes_gc_queue_length_consumer_deleted", 0);
    metric_test_run(CMP_EQUAL, "rabbitmq_nodes_gc_queue_length_exchange_deleted", "rabbitmq_nodes_gc_queue_length_exchange_deleted", 0);
    metric_test_run(CMP_EQUAL, "rabbitmq_nodes_gc_queue_length_node_node_deleted", "rabbitmq_nodes_gc_queue_length_node_node_deleted", 0);
    metric_test_run(CMP_EQUAL, "rabbitmq_nodes_gc_queue_length_queue_deleted", "rabbitmq_nodes_gc_queue_length_queue_deleted", 0);
    metric_test_run(CMP_EQUAL, "rabbitmq_nodes_gc_queue_length_vhost_deleted", "rabbitmq_nodes_gc_queue_length_vhost_deleted", 0);
    metric_test_run(CMP_EQUAL, "rabbitmq_nodes_io_read_avg_time", "rabbitmq_nodes_io_read_avg_time", 0);
    metric_test_run(CMP_EQUAL, "rabbitmq_nodes_io_read_bytes", "rabbitmq_nodes_io_read_bytes", 628662853);
    metric_test_run(CMP_EQUAL, "rabbitmq_nodes_io_read_count", "rabbitmq_nodes_io_read_count", 25381);
    metric_test_run(CMP_EQUAL, "rabbitmq_nodes_io_reopen_count", "rabbitmq_nodes_io_reopen_count", 0);
    metric_test_run(CMP_EQUAL, "rabbitmq_nodes_io_seek_avg_time", "rabbitmq_nodes_io_seek_avg_time", 0);
    metric_test_run(CMP_EQUAL, "rabbitmq_nodes_io_seek_count", "rabbitmq_nodes_io_seek_count", 4138);
    metric_test_run(CMP_EQUAL, "rabbitmq_nodes_io_sync_avg_time", "rabbitmq_nodes_io_sync_avg_time", 69.211680);
    metric_test_run(CMP_EQUAL, "rabbitmq_nodes_io_sync_count", "rabbitmq_nodes_io_sync_count", 17584);
    metric_test_run(CMP_EQUAL, "rabbitmq_nodes_io_write_avg_time", "rabbitmq_nodes_io_write_avg_time", 0);
    metric_test_run(CMP_EQUAL, "rabbitmq_nodes_io_write_bytes", "rabbitmq_nodes_io_write_bytes", 389663220);
    metric_test_run(CMP_EQUAL, "rabbitmq_nodes_io_write_count", "rabbitmq_nodes_io_write_count", 17914);
    metric_test_run(CMP_EQUAL, "rabbitmq_nodes_mem_alarm", "rabbitmq_nodes_mem_alarm", 0);
    metric_test_run(CMP_EQUAL, "rabbitmq_nodes_mem_limit", "rabbitmq_nodes_mem_limit", 3000000000);
    metric_test_run(CMP_EQUAL, "rabbitmq_nodes_mem_used", "rabbitmq_nodes_mem_used", 136110080);
    metric_test_run(CMP_EQUAL, "rabbitmq_nodes_mnesia_disk_tx_count", "rabbitmq_nodes_mnesia_disk_tx_count", 91);
    metric_test_run(CMP_EQUAL, "rabbitmq_nodes_mnesia_ram_tx_count", "rabbitmq_nodes_mnesia_ram_tx_count", 180531);
    metric_test_run(CMP_EQUAL, "rabbitmq_nodes_msg_store_read_count", "rabbitmq_nodes_msg_store_read_count", 31564);
    metric_test_run(CMP_EQUAL, "rabbitmq_nodes_msg_store_write_count", "rabbitmq_nodes_msg_store_write_count", 41671);
    metric_test_run(CMP_EQUAL, "rabbitmq_nodes_net_ticktime", "rabbitmq_nodes_net_ticktime", 60);
    metric_test_run(CMP_EQUAL, "rabbitmq_nodes_processors", "rabbitmq_nodes_processors", 24);
    metric_test_run(CMP_EQUAL, "rabbitmq_nodes_proc_total", "rabbitmq_nodes_proc_total", 1048576);
    metric_test_run(CMP_EQUAL, "rabbitmq_nodes_proc_used", "rabbitmq_nodes_proc_used", 580);
    metric_test_run(CMP_EQUAL, "rabbitmq_nodes_queue_created", "rabbitmq_nodes_queue_created", 51);
    metric_test_run(CMP_EQUAL, "rabbitmq_nodes_queue_declared", "rabbitmq_nodes_queue_declared", 1893);
    metric_test_run(CMP_EQUAL, "rabbitmq_nodes_queue_deleted", "rabbitmq_nodes_queue_deleted", 39);
    metric_test_run(CMP_EQUAL, "rabbitmq_nodes_queue_index_read_count", "rabbitmq_nodes_queue_index_read_count", 97);
    metric_test_run(CMP_EQUAL, "rabbitmq_nodes_queue_index_write_count", "rabbitmq_nodes_queue_index_write_count", 327);
    metric_test_run(CMP_EQUAL, "rabbitmq_nodes_running", "rabbitmq_nodes_running", 1);
    metric_test_run(CMP_EQUAL, "rabbitmq_nodes_run_queue", "rabbitmq_nodes_run_queue", 1);
    metric_test_run(CMP_EQUAL, "rabbitmq_nodes_sockets_total", "rabbitmq_nodes_sockets_total", 29401);
    metric_test_run(CMP_EQUAL, "rabbitmq_nodes_sockets_used", "rabbitmq_nodes_sockets_used", 10);
    metric_test_run(CMP_EQUAL, "rabbitmq_nodes_uptime", "rabbitmq_nodes_uptime", 1967711955);
    metric_test_run(CMP_EQUAL, "rabbitmq_object_totals_channels", "rabbitmq_object_totals_channels", 10);
    metric_test_run(CMP_EQUAL, "rabbitmq_object_totals_connections", "rabbitmq_object_totals_connections", 10);
    metric_test_run(CMP_EQUAL, "rabbitmq_object_totals_consumers", "rabbitmq_object_totals_consumers", 4);
    metric_test_run(CMP_EQUAL, "rabbitmq_object_totals_exchanges", "rabbitmq_object_totals_exchanges", 18);
    metric_test_run(CMP_EQUAL, "rabbitmq_object_totals_queues", "rabbitmq_object_totals_queues", 12);
    metric_test_run(CMP_EQUAL, "rabbitmq_queues_auto_delete", "rabbitmq_queues_auto_delete", 0);
    metric_test_run(CMP_EQUAL, "rabbitmq_queues_consumer_capacity", "rabbitmq_queues_consumer_capacity", 0);
    metric_test_run(CMP_EQUAL, "rabbitmq_queues_consumers", "rabbitmq_queues_consumers", 0);
    metric_test_run(CMP_EQUAL, "rabbitmq_queues_consumer_utilisation", "rabbitmq_queues_consumer_utilisation", 0);
    metric_test_run(CMP_EQUAL, "rabbitmq_queues_durable", "rabbitmq_queues_durable", 0);
    metric_test_run(CMP_EQUAL, "rabbitmq_queues_exclusive", "rabbitmq_queues_exclusive", 0);
    metric_test_run(CMP_EQUAL, "rabbitmq_queues_garbage_collection_fullsweep_after", "rabbitmq_queues_garbage_collection_fullsweep_after", 65535);
    metric_test_run(CMP_EQUAL, "rabbitmq_queues_garbage_collection_max_heap_size", "rabbitmq_queues_garbage_collection_max_heap_size", 0);
    metric_test_run(CMP_EQUAL, "rabbitmq_queues_garbage_collection_min_bin_vheap_size", "rabbitmq_queues_garbage_collection_min_bin_vheap_size", 46422);
    metric_test_run(CMP_EQUAL, "rabbitmq_queues_garbage_collection_min_heap_size", "rabbitmq_queues_garbage_collection_min_heap_size", 233);
    metric_test_run(CMP_EQUAL, "rabbitmq_queues_garbage_collection_minor_gcs", "rabbitmq_queues_garbage_collection_minor_gcs", 133);
    metric_test_run(CMP_EQUAL, "rabbitmq_queues_memory", "rabbitmq_queues_memory", 10768);
    metric_test_run(CMP_EQUAL, "rabbitmq_queues_message_bytes", "rabbitmq_queues_message_bytes", 0);
    metric_test_run(CMP_EQUAL, "rabbitmq_queues_message_bytes_paged_out", "rabbitmq_queues_message_bytes_paged_out", 0);
    metric_test_run(CMP_EQUAL, "rabbitmq_queues_message_bytes_persistent", "rabbitmq_queues_message_bytes_persistent", 0);
    metric_test_run(CMP_EQUAL, "rabbitmq_queues_message_bytes_ram", "rabbitmq_queues_message_bytes_ram", 0);
    metric_test_run(CMP_EQUAL, "rabbitmq_queues_message_bytes_ready", "rabbitmq_queues_message_bytes_ready", 0);
    metric_test_run(CMP_EQUAL, "rabbitmq_queues_message_bytes_unacknowledged", "rabbitmq_queues_message_bytes_unacknowledged", 0);
    metric_test_run(CMP_EQUAL, "rabbitmq_queues_messages", "rabbitmq_queues_messages", 0);
    metric_test_run(CMP_EQUAL, "rabbitmq_queues_messages_paged_out", "rabbitmq_queues_messages_paged_out", 0);
    metric_test_run(CMP_EQUAL, "rabbitmq_queues_messages_persistent", "rabbitmq_queues_messages_persistent", 0);
    metric_test_run(CMP_EQUAL, "rabbitmq_queues_messages_ram", "rabbitmq_queues_messages_ram", 0);
    metric_test_run(CMP_EQUAL, "rabbitmq_queues_messages_ready", "rabbitmq_queues_messages_ready", 0);
    metric_test_run(CMP_EQUAL, "rabbitmq_queues_messages_ready_ram", "rabbitmq_queues_messages_ready_ram", 0);
    metric_test_run(CMP_EQUAL, "rabbitmq_queues_message_stats_ack", "rabbitmq_queues_message_stats_ack", 3989);
    metric_test_run(CMP_EQUAL, "rabbitmq_queues_message_stats_deliver", "rabbitmq_queues_message_stats_deliver", 3989);
    metric_test_run(CMP_EQUAL, "rabbitmq_queues_message_stats_deliver_get", "rabbitmq_queues_message_stats_deliver_get", 3989);
    metric_test_run(CMP_EQUAL, "rabbitmq_queues_message_stats_deliver_no_ack", "rabbitmq_queues_message_stats_deliver_no_ack", 0);
    metric_test_run(CMP_EQUAL, "rabbitmq_queues_message_stats_get", "rabbitmq_queues_message_stats_get", 0);
    metric_test_run(CMP_EQUAL, "rabbitmq_queues_message_stats_get_empty", "rabbitmq_queues_message_stats_get_empty", 0);
    metric_test_run(CMP_EQUAL, "rabbitmq_queues_message_stats_get_no_ack", "rabbitmq_queues_message_stats_get_no_ack", 0);
    metric_test_run(CMP_EQUAL, "rabbitmq_queues_message_stats_publish", "rabbitmq_queues_message_stats_publish", 3989);
    metric_test_run(CMP_EQUAL, "rabbitmq_queues_message_stats_redeliver", "rabbitmq_queues_message_stats_redeliver", 0);
    metric_test_run(CMP_EQUAL, "rabbitmq_queues_messages_unacknowledged", "rabbitmq_queues_messages_unacknowledged", 0);
    metric_test_run(CMP_EQUAL, "rabbitmq_queues_messages_unacknowledged_ram", "rabbitmq_queues_messages_unacknowledged_ram", 0);
    metric_test_run(CMP_EQUAL, "rabbitmq_queues_recoverable_slaves_size", "rabbitmq_queues_recoverable_slaves_size", 0);
    metric_test_run(CMP_EQUAL, "rabbitmq_queues_reductions", "rabbitmq_queues_reductions", 7477221);
    metric_test_run(CMP_EQUAL, "rabbitmq_queues_slave_nodes_size", "rabbitmq_queues_slave_nodes_size", 0);
    metric_test_run(CMP_EQUAL, "rabbitmq_queues_state", "rabbitmq_queues_state", 1);
    metric_test_run(CMP_EQUAL, "rabbitmq_queues_synchronised_slave_nodes_size", "rabbitmq_queues_synchronised_slave_nodes_size", 0);
    metric_test_run(CMP_EQUAL, "rabbitmq_queue_totals_messages", "rabbitmq_queue_totals_messages", 0);
    metric_test_run(CMP_EQUAL, "rabbitmq_queue_totals_messages_details_rate", "rabbitmq_queue_totals_messages_details_rate", 0);
    metric_test_run(CMP_EQUAL, "rabbitmq_queue_totals_messages_ready", "rabbitmq_queue_totals_messages_ready", 0);
    metric_test_run(CMP_EQUAL, "rabbitmq_queue_totals_messages_ready_details_rate", "rabbitmq_queue_totals_messages_ready_details_rate", 0);
    metric_test_run(CMP_EQUAL, "rabbitmq_queue_totals_messages_unacknowledged", "rabbitmq_queue_totals_messages_unacknowledged", 0);
    metric_test_run(CMP_EQUAL, "rabbitmq_queue_totals_messages_unacknowledged_details_rate", "rabbitmq_queue_totals_messages_unacknowledged_details_rate", 0);
    metric_test_run(CMP_EQUAL, "rabbitmq_statistics_db_event_queue", "rabbitmq_statistics_db_event_queue", 0);
    metric_test_run(CMP_GREATER, "rabbitmq_vhosts_cluster_size", "rabbitmq_vhosts_cluster_size", 0);
    metric_test_run(CMP_GREATER, "rabbitmq_vhosts_cluster_state", "rabbitmq_vhosts_cluster_state", 0);
    metric_test_run(CMP_EQUAL, "rabbitmq_vhosts_messages", "rabbitmq_vhosts_messages", 0);
    metric_test_run(CMP_EQUAL, "rabbitmq_vhosts_messages_ready", "rabbitmq_vhosts_messages_ready", 0);
    metric_test_run(CMP_EQUAL, "rabbitmq_vhosts_message_stats_ack", "rabbitmq_vhosts_message_stats_ack", 45986);
    metric_test_run(CMP_EQUAL, "rabbitmq_vhosts_message_stats_confirm", "rabbitmq_vhosts_message_stats_confirm", 55483);
    metric_test_run(CMP_EQUAL, "rabbitmq_vhosts_message_stats_deliver", "rabbitmq_vhosts_message_stats_deliver", 47510);
    metric_test_run(CMP_EQUAL, "rabbitmq_vhosts_message_stats_deliver_get", "rabbitmq_vhosts_message_stats_deliver_get", 47581);
    metric_test_run(CMP_EQUAL, "rabbitmq_vhosts_message_stats_deliver_no_ack", "rabbitmq_vhosts_message_stats_deliver_no_ack", 0);
    metric_test_run(CMP_EQUAL, "rabbitmq_vhosts_message_stats_drop_unroutable", "rabbitmq_vhosts_message_stats_drop_unroutable", 0);
    metric_test_run(CMP_EQUAL, "rabbitmq_vhosts_message_stats_get", "rabbitmq_vhosts_message_stats_get", 71);
    metric_test_run(CMP_EQUAL, "rabbitmq_vhosts_message_stats_get_empty", "rabbitmq_vhosts_message_stats_get_empty", 6);
    metric_test_run(CMP_EQUAL, "rabbitmq_vhosts_message_stats_get_no_ack", "rabbitmq_vhosts_message_stats_get_no_ack", 0);
    metric_test_run(CMP_EQUAL, "rabbitmq_vhosts_message_stats_publish", "rabbitmq_vhosts_message_stats_publish", 55484);
    metric_test_run(CMP_EQUAL, "rabbitmq_vhosts_message_stats_redeliver", "rabbitmq_vhosts_message_stats_redeliver", 1465);
    metric_test_run(CMP_EQUAL, "rabbitmq_vhosts_message_stats_return_unroutable", "rabbitmq_vhosts_message_stats_return_unroutable", 23);
    metric_test_run(CMP_EQUAL, "rabbitmq_vhosts_messages_unacknowledged", "rabbitmq_vhosts_messages_unacknowledged", 0);
    metric_test_run(CMP_EQUAL, "rabbitmq_vhosts_recv_oct", "rabbitmq_vhosts_recv_oct", 406943686);
    metric_test_run(CMP_EQUAL, "rabbitmq_vhosts_send_oct", "rabbitmq_vhosts_send_oct", 333187900);
    metric_test_run(CMP_EQUAL, "rabbitmq_vhosts_tracing", "rabbitmq_vhosts_tracing", 0);
}

void api_test_parser_elasticsearch(char *binary) {
    context_arg *carg = calloc(1, sizeof(*carg));
    carg->data = calloc(1, sizeof(elastic_settings));
    string *msg;

    char nodes_json_path[PATH_MAX + 1];
    get_local_directory(nodes_json_path, binary, "tests/mock/elasticsearch/nodes.json");
    msg = get_file_content(nodes_json_path, 0);
    elasticsearch_nodes_handler(msg->s, msg->l, carg);
    assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 1, carg->parser_status);
    string_free(msg);
    carg->parser_status = 0;

    get_local_directory(nodes_json_path, binary, "tests/mock/elasticsearch/health.json");
    msg = get_file_content(nodes_json_path, 0);
    elasticsearch_health_handler(msg->s, msg->l, carg);
    assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 1, carg->parser_status);
    string_free(msg);
    carg->parser_status = 0;

    get_local_directory(nodes_json_path, binary, "tests/mock/elasticsearch/index.json");
    msg = get_file_content(nodes_json_path, 0);
    elasticsearch_index_handler(msg->s, msg->l, carg);
    assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 1, carg->parser_status);
    string_free(msg);
    carg->parser_status = 0;

    get_local_directory(nodes_json_path, binary, "tests/mock/elasticsearch/settings.json");
    msg = get_file_content(nodes_json_path, 0);
    elasticsearch_settings_handler(msg->s, msg->l, carg);
    assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 1, carg->parser_status);
    string_free(msg);
    carg->parser_status = 0;

    metric_test_run(CMP_EQUAL, "elasticsearch_active_primary_shards", "elasticsearch_active_primary_shards", 4);
    metric_test_run(CMP_EQUAL, "elasticsearch_active_shards", "elasticsearch_active_shards", 423);
    metric_test_run(CMP_EQUAL, "elasticsearch_active_shards_percent_as_number", "elasticsearch_active_shards_percent_as_number", 100);
    metric_test_run(CMP_GREATER, "elasticsearch_breakers_accounting", "elasticsearch_breakers_accounting", -1);
    metric_test_run(CMP_GREATER, "elasticsearch_breakers_accounting_bytes", "elasticsearch_breakers_accounting_bytes", -1);
    metric_test_run(CMP_GREATER, "elasticsearch_breakers_fielddata", "elasticsearch_breakers_fielddata", -1);
    metric_test_run(CMP_GREATER, "elasticsearch_breakers_fielddata_bytes", "elasticsearch_breakers_fielddata_bytes", -1);
    metric_test_run(CMP_GREATER, "elasticsearch_breakers_in_flight_requests", "elasticsearch_breakers_in_flight_requests", -1);
    metric_test_run(CMP_GREATER, "elasticsearch_breakers_in_flight_requests_bytes", "elasticsearch_breakers_in_flight_requests_bytes", 27839);
    metric_test_run(CMP_GREATER, "elasticsearch_breakers_parent", "elasticsearch_breakers_parent", -1);
    metric_test_run(CMP_GREATER, "elasticsearch_breakers_parent_bytes", "elasticsearch_breakers_parent_bytes", 33602900175);
    metric_test_run(CMP_GREATER, "elasticsearch_breakers_request", "elasticsearch_breakers_request", -1);
    metric_test_run(CMP_GREATER, "elasticsearch_breakers_request_bytes", "elasticsearch_breakers_request_bytes", -1);
    metric_test_run(CMP_EQUAL, "elasticsearch_cluster_indices_completion_size_in_bytes", "elasticsearch_cluster_indices_completion_size_in_bytes", 0);
    metric_test_run(CMP_EQUAL, "elasticsearch_cluster_indices_count", "elasticsearch_cluster_indices_count", 0);
    metric_test_run(CMP_EQUAL, "elasticsearch_cluster_indices_docs_count", "elasticsearch_cluster_indices_docs_count", 0);
    metric_test_run(CMP_EQUAL, "elasticsearch_cluster_indices_docs_deleted", "elasticsearch_cluster_indices_docs_deleted", 0);
    metric_test_run(CMP_EQUAL, "elasticsearch_cluster_indices_fielddata_evictions", "elasticsearch_cluster_indices_fielddata_evictions", 0);
    metric_test_run(CMP_EQUAL, "elasticsearch_cluster_indices_fielddata_memory_size_in_bytes", "elasticsearch_cluster_indices_fielddata_memory_size_in_bytes", 0);
    metric_test_run(CMP_EQUAL, "elasticsearch_cluster_indices_query_cache_cache_count", "elasticsearch_cluster_indices_query_cache_cache_count", 0);
    metric_test_run(CMP_EQUAL, "elasticsearch_cluster_indices_query_cache_cache_size", "elasticsearch_cluster_indices_query_cache_cache_size", 0);
    metric_test_run(CMP_EQUAL, "elasticsearch_cluster_indices_query_cache_evictions", "elasticsearch_cluster_indices_query_cache_evictions", 0);
    metric_test_run(CMP_EQUAL, "elasticsearch_cluster_indices_query_cache_hit_count", "elasticsearch_cluster_indices_query_cache_hit_count", 0);
    metric_test_run(CMP_EQUAL, "elasticsearch_cluster_indices_query_cache_memory_size_in_bytes", "elasticsearch_cluster_indices_query_cache_memory_size_in_bytes", 0);
    metric_test_run(CMP_EQUAL, "elasticsearch_cluster_indices_query_cache_miss_count", "elasticsearch_cluster_indices_query_cache_miss_count", 0);
    metric_test_run(CMP_EQUAL, "elasticsearch_cluster_indices_query_cache_total_count", "elasticsearch_cluster_indices_query_cache_total_count", 0);
    metric_test_run(CMP_EQUAL, "elasticsearch_cluster_indices_segments_count", "elasticsearch_cluster_indices_segments_count", 0);
    metric_test_run(CMP_EQUAL, "elasticsearch_cluster_indices_segments_doc_values_memory_in_bytes", "elasticsearch_cluster_indices_segments_doc_values_memory_in_bytes", 0);
    metric_test_run(CMP_EQUAL, "elasticsearch_cluster_indices_segments_fixed_bit_set_memory_in_bytes", "elasticsearch_cluster_indices_segments_fixed_bit_set_memory_in_bytes", 0);
    metric_test_run(CMP_EQUAL, "elasticsearch_cluster_indices_segments_index_writer_memory_in_bytes", "elasticsearch_cluster_indices_segments_index_writer_memory_in_bytes", 0);
    metric_test_run(CMP_EQUAL, "elasticsearch_cluster_indices_segments_max_unsafe_auto_id_timestamp", "elasticsearch_cluster_indices_segments_max_unsafe_auto_id_timestamp", 0);
    metric_test_run(CMP_EQUAL, "elasticsearch_cluster_indices_segments_memory_in_bytes", "elasticsearch_cluster_indices_segments_memory_in_bytes", 0);
    metric_test_run(CMP_EQUAL, "elasticsearch_cluster_indices_segments_norms_memory_in_bytes", "elasticsearch_cluster_indices_segments_norms_memory_in_bytes", 0);
    metric_test_run(CMP_EQUAL, "elasticsearch_cluster_indices_segments_points_memory_in_bytes", "elasticsearch_cluster_indices_segments_points_memory_in_bytes", 0);
    metric_test_run(CMP_EQUAL, "elasticsearch_cluster_indices_segments_stored_fields_memory_in_bytes", "elasticsearch_cluster_indices_segments_stored_fields_memory_in_bytes", 0);
    metric_test_run(CMP_EQUAL, "elasticsearch_cluster_indices_segments_terms_memory_in_bytes", "elasticsearch_cluster_indices_segments_terms_memory_in_bytes", 0);
    metric_test_run(CMP_EQUAL, "elasticsearch_cluster_indices_segments_term_vectors_memory_in_bytes", "elasticsearch_cluster_indices_segments_term_vectors_memory_in_bytes", 0);
    metric_test_run(CMP_EQUAL, "elasticsearch_cluster_indices_segments_version_map_memory_in_bytes", "elasticsearch_cluster_indices_segments_version_map_memory_in_bytes", 0);
    metric_test_run(CMP_EQUAL, "elasticsearch_cluster_indices_shards_index_primaries_avg", "elasticsearch_cluster_indices_shards_index_primaries_avg", 0);
    metric_test_run(CMP_EQUAL, "elasticsearch_cluster_indices_shards_index_primaries_max", "elasticsearch_cluster_indices_shards_index_primaries_max", 0);
    metric_test_run(CMP_EQUAL, "elasticsearch_cluster_indices_shards_index_primaries_min", "elasticsearch_cluster_indices_shards_index_primaries_min", 0);
    metric_test_run(CMP_EQUAL, "elasticsearch_cluster_indices_shards_index_replication_avg", "elasticsearch_cluster_indices_shards_index_replication_avg", 0);
    metric_test_run(CMP_EQUAL, "elasticsearch_cluster_indices_shards_index_replication_max", "elasticsearch_cluster_indices_shards_index_replication_max", 0);
    metric_test_run(CMP_EQUAL, "elasticsearch_cluster_indices_shards_index_replication_min", "elasticsearch_cluster_indices_shards_index_replication_min", 0);
    metric_test_run(CMP_EQUAL, "elasticsearch_cluster_indices_shards_index_shards_avg", "elasticsearch_cluster_indices_shards_index_shards_avg", 0);
    metric_test_run(CMP_EQUAL, "elasticsearch_cluster_indices_shards_index_shards_max", "elasticsearch_cluster_indices_shards_index_shards_max", 0);
    metric_test_run(CMP_EQUAL, "elasticsearch_cluster_indices_shards_index_shards_min", "elasticsearch_cluster_indices_shards_index_shards_min", 0);
    metric_test_run(CMP_EQUAL, "elasticsearch_cluster_indices_shards_primaries", "elasticsearch_cluster_indices_shards_primaries", 0);
    metric_test_run(CMP_EQUAL, "elasticsearch_cluster_indices_shards_replication", "elasticsearch_cluster_indices_shards_replication", 0);
    metric_test_run(CMP_EQUAL, "elasticsearch_cluster_indices_shards_total", "elasticsearch_cluster_indices_shards_total", 0);
    metric_test_run(CMP_EQUAL, "elasticsearch_cluster_indices_store_size_in_bytes", "elasticsearch_cluster_indices_store_size_in_bytes", 0);
    metric_test_run(CMP_EQUAL, "elasticsearch_cluster_nodes_count_coordinating_only", "elasticsearch_cluster_nodes_count_coordinating_only", 0);
    metric_test_run(CMP_EQUAL, "elasticsearch_cluster_nodes_count_data", "elasticsearch_cluster_nodes_count_data", 0);
    metric_test_run(CMP_EQUAL, "elasticsearch_cluster_nodes_count_ingest", "elasticsearch_cluster_nodes_count_ingest", 0);
    metric_test_run(CMP_EQUAL, "elasticsearch_cluster_nodes_count_master", "elasticsearch_cluster_nodes_count_master", 0);
    metric_test_run(CMP_EQUAL, "elasticsearch_cluster_nodes_count_total", "elasticsearch_cluster_nodes_count_total", 0);
    metric_test_run(CMP_EQUAL, "elasticsearch_cluster_nodes_count_voting_only", "elasticsearch_cluster_nodes_count_voting_only", 0);
    metric_test_run(CMP_EQUAL, "elasticsearch_cluster_nodes_discovery_types_zen", "elasticsearch_cluster_nodes_discovery_types_zen", 0);
    metric_test_run(CMP_EQUAL, "elasticsearch_cluster__nodes_failed", "elasticsearch_cluster__nodes_failed", 0);
    metric_test_run(CMP_EQUAL, "elasticsearch_cluster_nodes_fs_available_in_bytes", "elasticsearch_cluster_nodes_fs_available_in_bytes", 0);
    metric_test_run(CMP_EQUAL, "elasticsearch_cluster_nodes_fs_free_in_bytes", "elasticsearch_cluster_nodes_fs_free_in_bytes", 0);
    metric_test_run(CMP_EQUAL, "elasticsearch_cluster_nodes_fs_total_in_bytes", "elasticsearch_cluster_nodes_fs_total_in_bytes", 0);
    metric_test_run(CMP_EQUAL, "elasticsearch_cluster_nodes_jvm_max_uptime_in_millis", "elasticsearch_cluster_nodes_jvm_max_uptime_in_millis", 0);
    metric_test_run(CMP_EQUAL, "elasticsearch_cluster_nodes_jvm_mem_heap_max_in_bytes", "elasticsearch_cluster_nodes_jvm_mem_heap_max_in_bytes", 0);
    metric_test_run(CMP_EQUAL, "elasticsearch_cluster_nodes_jvm_mem_heap_used_in_bytes", "elasticsearch_cluster_nodes_jvm_mem_heap_used_in_bytes", 0);
    metric_test_run(CMP_EQUAL, "elasticsearch_cluster_nodes_jvm_threads", "elasticsearch_cluster_nodes_jvm_threads", 0);
    metric_test_run(CMP_EQUAL, "elasticsearch_cluster_nodes_network_types_http_types_security4", "elasticsearch_cluster_nodes_network_types_http_types_security4", 0);
    metric_test_run(CMP_EQUAL, "elasticsearch_cluster_nodes_network_types_transport_types_security4", "elasticsearch_cluster_nodes_network_types_transport_types_security4", 0);
    metric_test_run(CMP_EQUAL, "elasticsearch_cluster_nodes_os_allocated_processors", "elasticsearch_cluster_nodes_os_allocated_processors", 0);
    metric_test_run(CMP_EQUAL, "elasticsearch_cluster_nodes_os_available_processors", "elasticsearch_cluster_nodes_os_available_processors", 0);
    metric_test_run(CMP_EQUAL, "elasticsearch_cluster_nodes_os_mem_free_in_bytes", "elasticsearch_cluster_nodes_os_mem_free_in_bytes", 0);
    metric_test_run(CMP_EQUAL, "elasticsearch_cluster_nodes_os_mem_free_percent", "elasticsearch_cluster_nodes_os_mem_free_percent", 0);
    metric_test_run(CMP_EQUAL, "elasticsearch_cluster_nodes_os_mem_total_in_bytes", "elasticsearch_cluster_nodes_os_mem_total_in_bytes", 0);
    metric_test_run(CMP_EQUAL, "elasticsearch_cluster_nodes_os_mem_used_in_bytes", "elasticsearch_cluster_nodes_os_mem_used_in_bytes", 0);
    metric_test_run(CMP_EQUAL, "elasticsearch_cluster_nodes_os_mem_used_percent", "elasticsearch_cluster_nodes_os_mem_used_percent", 0);
    metric_test_run(CMP_EQUAL, "elasticsearch_cluster_nodes_process_cpu_percent", "elasticsearch_cluster_nodes_process_cpu_percent", 0);
    metric_test_run(CMP_EQUAL, "elasticsearch_cluster_nodes_process_open_file_descriptors_avg", "elasticsearch_cluster_nodes_process_open_file_descriptors_avg", 0);
    metric_test_run(CMP_EQUAL, "elasticsearch_cluster_nodes_process_open_file_descriptors_max", "elasticsearch_cluster_nodes_process_open_file_descriptors_max", 0);
    metric_test_run(CMP_EQUAL, "elasticsearch_cluster_nodes_process_open_file_descriptors_min", "elasticsearch_cluster_nodes_process_open_file_descriptors_min", 0);
    metric_test_run(CMP_EQUAL, "elasticsearch_cluster__nodes_successful", "elasticsearch_cluster__nodes_successful", 0);
    metric_test_run(CMP_EQUAL, "elasticsearch_cluster__nodes_total", "elasticsearch_cluster__nodes_total", 0);
    metric_test_run(CMP_GREATER, "elasticsearch_cluster_status", "elasticsearch_cluster_status", -1);
    metric_test_run(CMP_EQUAL, "elasticsearch_cluster_timestamp", "elasticsearch_cluster_timestamp", 0);
    metric_test_run(CMP_EQUAL, "elasticsearch_delayed_unassigned_shards", "elasticsearch_delayed_unassigned_shards", 0);
    metric_test_run(CMP_EQUAL, "elasticsearch_discovery_cluster_state_queue", "elasticsearch_discovery_cluster_state_queue", 0);
    metric_test_run(CMP_GREATER, "elasticsearch_discovery_published_cluster_states", "elasticsearch_discovery_published_cluster_states", -1);
    metric_test_run(CMP_EQUAL, "elasticsearch_fs", "elasticsearch_fs", 1726235801708);
    metric_test_run(CMP_GREATER, "elasticsearch_fs_io_stat_total", "elasticsearch_fs_io_stat_total", 35562);
    metric_test_run(CMP_GREATER, "elasticsearch_fs_total_bytes", "elasticsearch_fs_total_bytes", 1);
    metric_test_run(CMP_GREATER, "elasticsearch_http", "elasticsearch_http", 1);
    metric_test_run(CMP_EQUAL, "elasticsearch_indice_active_primary_shards", "elasticsearch_indice_active_primary_shards", 1);
    metric_test_run(CMP_EQUAL, "elasticsearch_indice_active_shards", "elasticsearch_indice_active_shards", 3);
    metric_test_run(CMP_EQUAL, "elasticsearch_indice_initializing_shards", "elasticsearch_indice_initializing_shards", 0);
    metric_test_run(CMP_EQUAL, "elasticsearch_indice_number_of_replicas", "elasticsearch_indice_number_of_replicas", 2);
    metric_test_run(CMP_EQUAL, "elasticsearch_indice_number_of_shards", "elasticsearch_indice_number_of_shards", 1);
    metric_test_run(CMP_EQUAL, "elasticsearch_indice_relocating_shards", "elasticsearch_indice_relocating_shards", 0);
    metric_test_run(CMP_EQUAL, "elasticsearch_indices_completion_bytes", "elasticsearch_indices_completion_bytes", 0);
    metric_test_run(CMP_EQUAL, "elasticsearch_indices_docs", "elasticsearch_indices_docs", 0);
    metric_test_run(CMP_EQUAL, "elasticsearch_indices_fielddata", "elasticsearch_indices_fielddata", 0);
    metric_test_run(CMP_EQUAL, "elasticsearch_indices_fielddata_bytes", "elasticsearch_indices_fielddata_bytes", 0);
    metric_test_run(CMP_EQUAL, "elasticsearch_indices_flush", "elasticsearch_indices_flush", 0);
    metric_test_run(CMP_EQUAL, "elasticsearch_indices_get", "elasticsearch_indices_get", 0);
    metric_test_run(CMP_EQUAL, "elasticsearch_indice_shard_active", "elasticsearch_indice_shard_active", 1);
    metric_test_run(CMP_EQUAL, "elasticsearch_indice_shard_active_shards", "elasticsearch_indice_shard_active_shards", 3);
    metric_test_run(CMP_EQUAL, "elasticsearch_indice_shard_initializing_shards", "elasticsearch_indice_shard_initializing_shards", 0);
    metric_test_run(CMP_EQUAL, "elasticsearch_indice_shard_relocating_shards", "elasticsearch_indice_shard_relocating_shards", 0);
    metric_test_run(CMP_EQUAL, "elasticsearch_indice_shard_status", "elasticsearch_indice_shard_status", 1);
    metric_test_run(CMP_EQUAL, "elasticsearch_indice_shard_unassigned_shards", "elasticsearch_indice_shard_unassigned_shards", 0);
    metric_test_run(CMP_EQUAL, "elasticsearch_indices_indexing", "elasticsearch_indices_indexing", 0);
    metric_test_run(CMP_EQUAL, "elasticsearch_indices_merges", "elasticsearch_indices_merges", 0);
    metric_test_run(CMP_EQUAL, "elasticsearch_indices_merges_bytes", "elasticsearch_indices_merges_bytes", 0);
    metric_test_run(CMP_EQUAL, "elasticsearch_indices_query_cache", "elasticsearch_indices_query_cache", 0);
    metric_test_run(CMP_EQUAL, "elasticsearch_indices_query_cache_bytes", "elasticsearch_indices_query_cache_bytes", 0);
    metric_test_run(CMP_EQUAL, "elasticsearch_indices_recovery", "elasticsearch_indices_recovery", 0);
    metric_test_run(CMP_EQUAL, "elasticsearch_indices_refresh", "elasticsearch_indices_refresh", 0);
    metric_test_run(CMP_EQUAL, "elasticsearch_indices_request_cache", "elasticsearch_indices_request_cache", 0);
    metric_test_run(CMP_EQUAL, "elasticsearch_indices_request_cache_bytes", "elasticsearch_indices_request_cache_bytes", 0);
    metric_test_run(CMP_EQUAL, "elasticsearch_indices_search", "elasticsearch_indices_search", 0);
    metric_test_run(CMP_GREATER, "elasticsearch_indices_segments", "elasticsearch_indices_segments", -92236854776001);
    metric_test_run(CMP_EQUAL, "elasticsearch_indices_segments_bytes", "elasticsearch_indices_segments_bytes", 0);
    metric_test_run(CMP_EQUAL, "elasticsearch_indices_store_bytes", "elasticsearch_indices_store_bytes", 0);
    metric_test_run(CMP_EQUAL, "elasticsearch_indice_status", "elasticsearch_indice_status", 1);
    metric_test_run(CMP_EQUAL, "elasticsearch_indices_translog", "elasticsearch_indices_translog", 0);
    metric_test_run(CMP_EQUAL, "elasticsearch_indices_translog_bytes", "elasticsearch_indices_translog_bytes", 0);
    metric_test_run(CMP_EQUAL, "elasticsearch_indices_warmer", "elasticsearch_indices_warmer", 0);
    metric_test_run(CMP_EQUAL, "elasticsearch_indice_unassigned_shards", "elasticsearch_indice_unassigned_shards", 0);
    metric_test_run(CMP_EQUAL, "elasticsearch_ingest_pipeline_xpack_monitoring_2", "elasticsearch_ingest_pipeline_xpack_monitoring_2", 0);
    metric_test_run(CMP_EQUAL, "elasticsearch_ingest_pipeline_xpack_monitoring_6", "elasticsearch_ingest_pipeline_xpack_monitoring_6", 0);
    metric_test_run(CMP_EQUAL, "elasticsearch_ingest_pipeline_xpack_monitoring_7", "elasticsearch_ingest_pipeline_xpack_monitoring_7", 0);
    metric_test_run(CMP_EQUAL, "elasticsearch_ingest_total", "elasticsearch_ingest_total", 0);
    metric_test_run(CMP_EQUAL, "elasticsearch_initializing_shards", "elasticsearch_initializing_shards", 0);
    metric_test_run(CMP_GREATER, "elasticsearch_jvm", "elasticsearch_jvm", 43656526450);
    metric_test_run(CMP_EQUAL, "elasticsearch_jvm_buffer_pool_direct", "elasticsearch_jvm_buffer_pool_direct", 253);
    metric_test_run(CMP_GREATER, "elasticsearch_jvm_buffer_pool_direct_bytes", "elasticsearch_jvm_buffer_pool_direct_bytes", 0);
    metric_test_run(CMP_EQUAL, "elasticsearch_jvm_buffer_pool_mapped", "elasticsearch_jvm_buffer_pool_mapped", 0);
    metric_test_run(CMP_EQUAL, "elasticsearch_jvm_buffer_pool_mapped_bytes", "elasticsearch_jvm_buffer_pool_mapped_bytes", 0);
    metric_test_run(CMP_GREATER, "elasticsearch_jvm_classes", "elasticsearch_jvm_classes", 488);
    metric_test_run(CMP_EQUAL, "elasticsearch_jvm_mem", "elasticsearch_jvm_mem", 39);
    metric_test_run(CMP_GREATER, "elasticsearch_jvm_mem_bytes", "elasticsearch_jvm_mem_bytes", 132529407);
    metric_test_run(CMP_GREATER, "elasticsearch_jvm_threads", "elasticsearch_jvm_threads", 243);
    metric_test_run(CMP_EQUAL, "elasticsearch_nodes_failed", "elasticsearch_nodes_failed", 0);
    metric_test_run(CMP_EQUAL, "elasticsearch_nodes_successful", "elasticsearch_nodes_successful", 1);
    metric_test_run(CMP_EQUAL, "elasticsearch_nodes_total", "elasticsearch_nodes_total", 1);
    metric_test_run(CMP_EQUAL, "elasticsearch_number_of_data_nodes", "elasticsearch_number_of_data_nodes", 1);
    metric_test_run(CMP_EQUAL, "elasticsearch_number_of_in_flight_fetch", "elasticsearch_number_of_in_flight_fetch", 0);
    metric_test_run(CMP_EQUAL, "elasticsearch_number_of_nodes", "elasticsearch_number_of_nodes", 1);
    metric_test_run(CMP_EQUAL, "elasticsearch_number_of_pending_tasks", "elasticsearch_number_of_pending_tasks", 0);
    metric_test_run(CMP_EQUAL, "elasticsearch_os", "elasticsearch_os", 1726235801707);
    metric_test_run(CMP_GREATER, "elasticsearch_os_cgrou_cpu", "elasticsearch_os_cgrou_cpu", -2);
    metric_test_run(CMP_EQUAL, "elasticsearch_os_cgrou_cpuacct", "elasticsearch_os_cgrou_cpuacct", 74957665140927376);
    metric_test_run(CMP_GREATER, "elasticsearch_os_cp_load_average", "elasticsearch_os_cp_load_average", 1.2);
    metric_test_run(CMP_EQUAL, "elasticsearch_os_cpu", "elasticsearch_os_cpu", 1);
    metric_test_run(CMP_GREATER, "elasticsearch_os_mem", "elasticsearch_os_mem", 3);
    metric_test_run(CMP_GREATER, "elasticsearch_os_mem_bytes", "elasticsearch_os_mem_bytes", 4914302975);
    metric_test_run(CMP_EQUAL, "elasticsearch_os_swap_bytes", "elasticsearch_os_swap_bytes", 0);
    metric_test_run(CMP_EQUAL, "elasticsearch_primaries_completion_bytes", "elasticsearch_primaries_completion_bytes", 0);
    metric_test_run(CMP_GREATER, "elasticsearch_primaries_docs", "elasticsearch_primaries_docs", -1);
    metric_test_run(CMP_EQUAL, "elasticsearch_primaries_fielddata", "elasticsearch_primaries_fielddata", 0);
    metric_test_run(CMP_EQUAL, "elasticsearch_primaries_fielddata_bytes", "elasticsearch_primaries_fielddata_bytes", 0);
    metric_test_run(CMP_GREATER, "elasticsearch_primaries_flush", "elasticsearch_primaries_flush", -1);
    metric_test_run(CMP_EQUAL, "elasticsearch_primaries_get", "elasticsearch_primaries_get", 0);
    metric_test_run(CMP_GREATER, "elasticsearch_primaries_indexing", "elasticsearch_primaries_indexing", -1);
    metric_test_run(CMP_GREATER, "elasticsearch_primaries_merges", "elasticsearch_primaries_merges", -1);
    metric_test_run(CMP_GREATER, "elasticsearch_primaries_merges_bytes", "elasticsearch_primaries_merges_bytes", -1);
    metric_test_run(CMP_EQUAL, "elasticsearch_primaries_query_cache", "elasticsearch_primaries_query_cache", 0);
    metric_test_run(CMP_EQUAL, "elasticsearch_primaries_query_cache_bytes", "elasticsearch_primaries_query_cache_bytes", 0);
    metric_test_run(CMP_EQUAL, "elasticsearch_primaries_recovery", "elasticsearch_primaries_recovery", 0);
    metric_test_run(CMP_GREATER, "elasticsearch_primaries_refresh", "elasticsearch_primaries_refresh", -2);
    metric_test_run(CMP_EQUAL, "elasticsearch_primaries_request_cache", "elasticsearch_primaries_request_cache", 0);
    metric_test_run(CMP_EQUAL, "elasticsearch_primaries_request_cache_bytes", "elasticsearch_primaries_request_cache_bytes", 0);
    metric_test_run(CMP_EQUAL, "elasticsearch_primaries_search", "elasticsearch_primaries_search", 0);
    metric_test_run(CMP_GREATER, "elasticsearch_primaries_segments", "elasticsearch_primaries_segments", -2);
    metric_test_run(CMP_GREATER, "elasticsearch_primaries_segments_bytes", "elasticsearch_primaries_segments_bytes", -1);
    metric_test_run(CMP_EQUAL, "elasticsearch_primaries_store_bytes", "elasticsearch_primaries_store_bytes", 14178662);
    metric_test_run(CMP_GREATER, "elasticsearch_primaries_translog", "elasticsearch_primaries_translog", -1);
    metric_test_run(CMP_GREATER, "elasticsearch_primaries_translog_bytes", "elasticsearch_primaries_translog_bytes", 54);
    metric_test_run(CMP_GREATER, "elasticsearch_primaries_warmer", "elasticsearch_primaries_warmer", -1);
    metric_test_run(CMP_GREATER, "elasticsearch_process", "elasticsearch_process", 1028);
    metric_test_run(CMP_GREATER, "elasticsearch_process_cpu", "elasticsearch_process_cpu", 0);
    metric_test_run(CMP_EQUAL, "elasticsearch_process_mem_bytes", "elasticsearch_process_mem_bytes", 111763275776);
    metric_test_run(CMP_EQUAL, "elasticsearch_relocating_shards", "elasticsearch_relocating_shards", 0);
    metric_test_run(CMP_GREATER, "elasticsearch_script", "elasticsearch_script", -1);
    metric_test_run(CMP_EQUAL, "elasticsearch_settings_index_blocks_read_only_allow_delete", "elasticsearch_settings_index_blocks_read_only_allow_delete", 0);
    metric_test_run(CMP_EQUAL, "elasticsearch_settings_index_blocks_write", "elasticsearch_settings_index_blocks_write", 0);
    metric_test_run(CMP_EQUAL, "elasticsearch_settings_index_mapper_dynamic", "elasticsearch_settings_index_mapper_dynamic", 0);
    metric_test_run(CMP_GREATER, "elasticsearch_settings_index_version_created", "elasticsearch_settings_index_version_created", 139939379684479);
    metric_test_run(CMP_EQUAL, "elasticsearch_settings_index_version_upgraded", "elasticsearch_settings_index_version_upgraded", 0);
    metric_test_run(CMP_EQUAL, "elasticsearch_shards_failed", "elasticsearch_shards_failed", 0);
    metric_test_run(CMP_EQUAL, "elasticsearch_shards_successful", "elasticsearch_shards_successful", 0);
    metric_test_run(CMP_EQUAL, "elasticsearch_shards_total", "elasticsearch_shards_total", 0);
    metric_test_run(CMP_EQUAL, "elasticsearch_task_max_waiting_in_queue_millis", "elasticsearch_task_max_waiting_in_queue_millis", 0);
    metric_test_run(CMP_EQUAL, "elasticsearch_thread_pool_analyze", "elasticsearch_thread_pool_analyze", 0);
    metric_test_run(CMP_EQUAL, "elasticsearch_thread_pool_ccr", "elasticsearch_thread_pool_ccr", 0);
    metric_test_run(CMP_EQUAL, "elasticsearch_thread_pool_data_frame_indexing", "elasticsearch_thread_pool_data_frame_indexing", 0);
    metric_test_run(CMP_EQUAL, "elasticsearch_thread_pool_fetch_shard_started", "elasticsearch_thread_pool_fetch_shard_started", 0);
    metric_test_run(CMP_EQUAL, "elasticsearch_thread_pool_fetch_shard_store", "elasticsearch_thread_pool_fetch_shard_store", 0);
    metric_test_run(CMP_EQUAL, "elasticsearch_thread_pool_flush", "elasticsearch_thread_pool_flush", 0);
    metric_test_run(CMP_EQUAL, "elasticsearch_thread_pool_force_merge", "elasticsearch_thread_pool_force_merge", 0);
    metric_test_run(CMP_GREATER, "elasticsearch_thread_pool_generic", "elasticsearch_thread_pool_generic", -1);
    metric_test_run(CMP_EQUAL, "elasticsearch_thread_pool_get", "elasticsearch_thread_pool_get", 0);
    metric_test_run(CMP_EQUAL, "elasticsearch_thread_pool_listener", "elasticsearch_thread_pool_listener", 0);
    metric_test_run(CMP_GREATER, "elasticsearch_thread_pool_management", "elasticsearch_thread_pool_management", -1);
    metric_test_run(CMP_EQUAL, "elasticsearch_thread_pool_ml_datafeed", "elasticsearch_thread_pool_ml_datafeed", 0);
    metric_test_run(CMP_EQUAL, "elasticsearch_thread_pool_ml_job_comms", "elasticsearch_thread_pool_ml_job_comms", 0);
    metric_test_run(CMP_EQUAL, "elasticsearch_thread_pool_ml_utility", "elasticsearch_thread_pool_ml_utility", 0);
    metric_test_run(CMP_EQUAL, "elasticsearch_thread_pool_refresh", "elasticsearch_thread_pool_refresh", 0);
    metric_test_run(CMP_EQUAL, "elasticsearch_thread_pool_rollup_indexing", "elasticsearch_thread_pool_rollup_indexing", 0);
    metric_test_run(CMP_GREATER, "elasticsearch_thread_pool_search", "elasticsearch_thread_pool_search", -1);
    metric_test_run(CMP_EQUAL, "elasticsearch_thread_pool_search_throttled", "elasticsearch_thread_pool_search_throttled", 0);
    metric_test_run(CMP_EQUAL, "elasticsearch_thread_pool_security_token_key", "elasticsearch_thread_pool_security_token_key", 0);
    metric_test_run(CMP_EQUAL, "elasticsearch_thread_pool_snapshot", "elasticsearch_thread_pool_snapshot", 0);
    metric_test_run(CMP_EQUAL, "elasticsearch_thread_pool_warmer", "elasticsearch_thread_pool_warmer", 0);
    metric_test_run(CMP_EQUAL, "elasticsearch_thread_pool_watcher", "elasticsearch_thread_pool_watcher", 0);
    metric_test_run(CMP_EQUAL, "elasticsearch_thread_pool_write", "elasticsearch_thread_pool_write", 0);
    metric_test_run(CMP_EQUAL, "elasticsearch_total_completion_bytes", "elasticsearch_total_completion_bytes", 0);
    metric_test_run(CMP_GREATER, "elasticsearch_total_docs", "elasticsearch_total_docs", -1);
    metric_test_run(CMP_EQUAL, "elasticsearch_total_fielddata", "elasticsearch_total_fielddata", 0);
    metric_test_run(CMP_EQUAL, "elasticsearch_total_fielddata_bytes", "elasticsearch_total_fielddata_bytes", 0);
    metric_test_run(CMP_GREATER, "elasticsearch_total_flush", "elasticsearch_total_flush", -1);
    metric_test_run(CMP_EQUAL, "elasticsearch_total_get", "elasticsearch_total_get", 0);
    metric_test_run(CMP_GREATER, "elasticsearch_total_indexing", "elasticsearch_total_indexing", -1);
    metric_test_run(CMP_GREATER, "elasticsearch_total_merges", "elasticsearch_total_merges", -1);
    metric_test_run(CMP_GREATER, "elasticsearch_total_merges_bytes", "elasticsearch_total_merges_bytes", -1);
    metric_test_run(CMP_EQUAL, "elasticsearch_total_query_cache", "elasticsearch_total_query_cache", 0);
    metric_test_run(CMP_EQUAL, "elasticsearch_total_query_cache_bytes", "elasticsearch_total_query_cache_bytes", 0);
    metric_test_run(CMP_EQUAL, "elasticsearch_total_recovery", "elasticsearch_total_recovery", 0);
    metric_test_run(CMP_GREATER, "elasticsearch_total_refresh", "elasticsearch_total_refresh", -1);
    metric_test_run(CMP_EQUAL, "elasticsearch_total_request_cache", "elasticsearch_total_request_cache", 0);
    metric_test_run(CMP_EQUAL, "elasticsearch_total_request_cache_bytes", "elasticsearch_total_request_cache_bytes", 0);
    metric_test_run(CMP_EQUAL, "elasticsearch_total_search", "elasticsearch_total_search", 0);
    metric_test_run(CMP_GREATER, "elasticsearch_total_segments", "elasticsearch_total_segments", -2);
    metric_test_run(CMP_GREATER, "elasticsearch_total_segments_bytes", "elasticsearch_total_segments_bytes", -1);
    metric_test_run(CMP_EQUAL, "elasticsearch_total_store_bytes", "elasticsearch_total_store_bytes", 45251826);
    metric_test_run(CMP_GREATER, "elasticsearch_total_translog", "elasticsearch_total_translog", -1);
    metric_test_run(CMP_GREATER, "elasticsearch_total_translog_bytes", "elasticsearch_total_translog_bytes", 164);
    metric_test_run(CMP_GREATER, "elasticsearch_total_warmer", "elasticsearch_total_warmer", -1);
    metric_test_run(CMP_GREATER, "elasticsearch_transport", "elasticsearch_transport", 177);
    metric_test_run(CMP_GREATER, "elasticsearch_transport_bytes", "elasticsearch_transport_bytes", 67317858381796);
    metric_test_run(CMP_EQUAL, "elasticsearch_unassigned_shards", "elasticsearch_unassigned_shards", 0);
}
