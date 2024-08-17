
# prometheus entrypoint for metrics (additional, set ttl for this context metrics from statsd/pushgateway)
entrypoint {
	[return empty]; # this case for push-only interfaces without return any http bodies
	ttl 3600;
	tcp 1111;
	unixgram /var/lib/alligator.unixgram;
	unix /var/lib/alligator.unix;
	handler prometheus;
	metric_aggregation count; # for counting histograms and counter datatypes as aggregation gateway
}

#configuration with reject metric label http_response_code="404":
entrypoint {
	reject http_response_code 404;
	ttl 86400;
	tcp 1111;

	allow 127.0.0.0/8; # support ACL mechanism
}

# StatsD mapping:
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
