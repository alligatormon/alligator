# Nginx

The open-source version of Nginx doesn't provide metrics. It is recommended to use the [VTS](https://github.com/vozlt/nginx-module-vts/tree/master) module to obtain metrics.

In addition, it is useful to monitor process statistics, running services, and open ports with the following configuration:
```
system {
    process nginx;
    services nginx.service;
}

query {
	expr 'count by (src_port, process) (socket_stat{process="nginx", src_port="80"})';
	make socket_match;
	datasource internal;
}

query {
	expr 'count by (src_port, process) (socket_stat{process="nginx", src_port="443"})';
	make socket_match;
	datasource internal;
}
```


The command `nginx -t` returns the validation status of the configuration, producing the following metric:
```
aggregate {
	process 'exec:///sbin/nginx -t';
}
```
This will generate a metric based on the exit status of Nginx, providing two metrics for configuration validation:
```
alligator_process_exit_status {proto="shell", key="exec:process:/sbin/nginx -t:/", type="aggregator"} 0
alligator_process_term_signal {proto="shell", key="exec:process:/sbin/nginx -t:/", type="aggregator"} 0
```

## nginx upstream check module
Alligator also supports collecting metrics from the [upstream check module](https://github.com/yaoweibin/nginx_upstream_check_module), which is widely used for active health checks.

The following configuration should be specified within a server context to enable metric transfer:
```
location /status {
    check_status;
}
```

To enable statistics collection from the upstream check module, use the following option:
```
aggregate {
	nginx_upstream_check http://localhost/status;
}
```

For checking X509 certificates on the filesystem, please refer to the explanation provided in the x509 [context](https://github.com/alligatormon/alligator/blob/master/doc/x509.md).
