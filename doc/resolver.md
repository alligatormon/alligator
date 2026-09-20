# Resolver
The resolver context allows setting other DNS servers to resolve domain names.

## Example
This configuration replaces the use of the operating system configured resolver with those specified in the list:
```
resolver {
    udp://8.8.8.8:53;
    udp://8.8.4.4:53;
    tcp://8.8.8.8:53;
    tcp://8.8.4.4:53;
}
```
It also automatically starts collecting statistics about DNS servers response times.

## Resolve DNS by alligator
The DNS resolver is used in Alligator to achieve these goals:
- Simply resolving names to ask service via aggregator
- Collecting statistics about response time of DNS servers
- Resolving DNS names to the metric label

The first one works transparently for the user.\
The second and third do not.

For example, the following configuration will resolve the A record of google.com.
```
resolver {
    udp://8.8.8.8:53;
}
aggregate {
	dns resolver:// resolve=google.com type=a;
}
```
It will then generate the following metrics:
```
alligator_dns_rr_info {host="udp://8.8.8.8:53", class="IN", type="A", name="google.com"} 1
alligator_dns_rr_info {host="udp://8.8.8.8:53", class="IN", type="A", name="google.com"} 1
alligator_dns_rr_info {host="udp://8.8.8.8:53", class="IN", type="A", name="google.com"} 1
alligator_dns_rr_info {host="udp://8.8.8.8:53", class="IN", type="A", name="google.com"} 1
alligator_dns_rr_info {host="udp://8.8.8.8:53", class="IN", type="A", name="google.com"} 1
alligator_dns_rr_info {host="udp://8.8.8.8:53", class="IN", type="A", name="google.com"} 1
alligator_dns_rr_info {host="udp://8.8.8.8:53", class="IN", type="A", name="google.com"} 1
alligator_dns_rr_info {host="udp://8.8.8.8:53", class="IN", type="A", name="google.com"} 1
alligator_dns_rr_info {host="udp://8.8.8.8:53", class="IN", type="A", name="google.com"} 1
alligator_dns_rr_info {host="udp://8.8.8.8:53", class="IN", type="A", name="google.com"} 1
alligator_dns_rr_info {host="udp://8.8.8.8:53", class="IN", type="A", name="google.com"} 1
alligator_dns_rr_info {host="udp://8.8.8.8:53", class="IN", type="A", name="google.com"} 1
alligator_dns_read_duration_seconds {quantile="0.90", host="udp://8.8.8.8:53"} 0.000032
alligator_dns_read_duration_seconds {quantile="0.95", host="udp://8.8.8.8:53"} 0.000068
alligator_dns_read_duration_seconds {quantile="0.99", host="udp://8.8.8.8:53"} 0.000068
alligator_dns_response_duration_seconds {quantile="0.90", host="udp://8.8.8.8:53"} 0.000032
alligator_dns_response_duration_seconds {quantile="0.95", host="udp://8.8.8.8:53"} 0.000068
alligator_dns_response_duration_seconds {quantile="0.99", host="udp://8.8.8.8:53"} 0.000068
alligator_dns_write_duration_seconds {quantile="0.90", host="udp://8.8.8.8:53"} 0
alligator_dns_write_duration_seconds {quantile="0.95", host="udp://8.8.8.8:53"} 0
alligator_dns_write_duration_seconds {quantile="0.99", host="udp://8.8.8.8:53"} 0
```

## Specifies the DNS server explicitly
The configuration above can be rewritten by explicitly setting of different DNS servers:
```
aggregate {
	dns udp://8.8.8.8:53 resolve=google.com type=a;
	dns udp://8.8.4.4:53 resolve=yahoo.com type=aaaa;
}
```

Several UDP probes may share one `bind_address=<port>`. They use a single local socket; replies are matched by DNS transaction id and the question name in the packet (`alligator_dns_rr_info` `name` label). Unique source ports per domain are not required.

Explicit `dns udp://... resolve=<domain>` probes also put that domain on resolver timing quantiles:

```
alligator_dns_read_duration_seconds {quantile="0.90", host="udp://8.8.8.8:53", name="google.com"} 0.000032
alligator_dns_response_duration_seconds {quantile="0.90", host="udp://8.8.8.8:53", name="google.com"} 0.000032
alligator_dns_write_duration_seconds {quantile="0.90", host="udp://8.8.8.8:53", name="google.com"} 0
```
