# Entrypoint
The entrypoint is necessary to enable ports that allows Prometheus pull daemons to gather information.
Alligator configuration supports a list of entrypoints, providing the capability to have many different ports for various purposes.

Entrypoint functionality includes:
- Push data interface
- Metric return
- Alligator API
- Replication log for [cluster](https://github.com/alligatormon/alligator/blob/master/doc/cluster.md) configuration.

Full explain
```
entrypoint {
    return [empty|on]>;
    reject <label name> <label key>;
    auth <basic|bearer|other> <label key>;
    auth_header <header_name;
    header 
    ttl <time to live>;
    tcp <port>;
    tcp <addr>:<port>;
    udp <port>;
    unixgram <path/to/socket>;
    unix <path/to/socket>;
    allow ;
    deny ;
    api ;
    handler <handler>;
    metric_aggregation [off|count]; # for counting histograms and counter datatypes as aggregation gateway
    cluster <cluster_name>;
    instance <instance_name>;
    mapping {
        template <template>;
        name <name>;
        label <label_name> <label_value>;
        buckets <buckets 1> <buckets 2> ... <buckets N>;
        le <le 1> <le 2> ... <le N>;
        quantiles <quantile 1> <quantile 2> ... <quantile N>;
        match [glob];
    }
}
```

## return
Default: on
Possible values:
- on
- empty

Enabling or disabling the response body on requests. This can be useful for Alligator ports that need to be accessed from the internet as the solution for receving Pushgateway metrics from a browser's JavaScript.


#configuration with reject metric label http_response_code="404":
```
entrypoint {
    reject http_response_code 404;
    ttl 86400;
    tcp 1111;

    allow 127.0.0.0/8; # support ACL mechanism
}
```

## StatsD mapping:
```
entrypoint {
        udp 127.0.0.1:8125;
        tcp 8125;
        mapping {
                template test1.*.test2.*;
                name "$1"_"$2";
                label label_name_"$1" "$2"_key;
                buckets 100 200 300;
                match glob;
        }
        mapping {
                template test2.*.test3.*;
                name "$1"_"$2";
                label label_name_"$1" "$2"_key;
                le 100 200 300;
                match glob;
        }
        mapping {
                template test3.*.test4.*;
                name "$1"_"$2";
                label label_name_"$1" "$2"_key;
                quantiles 0.999 0.95 0.9;
                match glob;
        }
}
```
