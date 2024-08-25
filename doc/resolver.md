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
aggregator_resolve_address {host="udp://8.8.8.8:53", class="IN", type="A", name="google.com"} 1
aggregator_resolve_address {host="udp://8.8.8.8:53", class="IN", type="A", name="google.com"} 1
aggregator_resolve_address {host="udp://8.8.8.8:53", class="IN", type="A", name="google.com"} 1
aggregator_resolve_address {host="udp://8.8.8.8:53", class="IN", type="A", name="google.com"} 1
aggregator_resolve_address {host="udp://8.8.8.8:53", class="IN", type="A", name="google.com"} 1
aggregator_resolve_address {host="udp://8.8.8.8:53", class="IN", type="A", name="google.com"} 1
aggregator_resolve_address {host="udp://8.8.8.8:53", class="IN", type="A", name="google.com"} 1
aggregator_resolve_address {host="udp://8.8.8.8:53", class="IN", type="A", name="google.com"} 1
aggregator_resolve_address {host="udp://8.8.8.8:53", class="IN", type="A", name="google.com"} 1
aggregator_resolve_address {host="udp://8.8.8.8:53", class="IN", type="A", name="google.com"} 1
aggregator_resolve_address {host="udp://8.8.8.8:53", class="IN", type="A", name="google.com"} 1
aggregator_resolve_address {host="udp://8.8.8.8:53", class="IN", type="A", name="google.com"} 1
resolver_read_time_mcs_quantile {quantile="0.90", host="udp://8.8.8.8:53"} 32
resolver_read_time_mcs_quantile {quantile="0.95", host="udp://8.8.8.8:53"} 68
resolver_read_time_mcs_quantile {quantile="0.99", host="udp://8.8.8.8:53"} 68
resolver_response_time_mcs_quantile {quantile="0.90", host="udp://8.8.8.8:53"} 32
resolver_response_time_mcs_quantile {quantile="0.95", host="udp://8.8.8.8:53"} 68
resolver_response_time_mcs_quantile {quantile="0.99", host="udp://8.8.8.8:53"} 68
resolver_write_time_mcs_quantile {quantile="0.90", host="udp://8.8.8.8:53"} 0
resolver_write_time_mcs_quantile {quantile="0.95", host="udp://8.8.8.8:53"} 0
resolver_write_time_mcs_quantile {quantile="0.99", host="udp://8.8.8.8:53"} 0
```

## Specifies the DNS server explicitly
The configuration above can be rewritten by explicitly setting of different DNS servers:
```
aggregate {
	dns udp://8.8.8.8:53 resolve=google.com type=a;
	dns udp://8.8.4.4:53 resolve=yahoo.com type=aaaa;
}
```
