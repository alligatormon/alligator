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
    auth <basic|bearer|other> <user:password|token|secret>;
    auth_header <header_name>;
    header <name> <value>;
    ttl <time to live>;
    tcp <port>;
    tcp <addr>:<port>;
    udp <port>;
    unixgram <path/to/socket>;
    unix <path/to/socket>;
    allow <CIDR>;
    deny <CIDR>;
    api [on|off];
    handler <handler>;
    pingloop <number>;
    metric_aggregation [off|count]; # for counting histograms and counter datatypes as aggregation gateway
    cluster <cluster_name>;
    instance <instance_name>;
    lang <lang>;
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

Plural: no

Possible values:
- on
- empty

Enabling or disabling the response body on requests. This can be useful for Alligator ports that need to be accessed from the internet as the solution for receving Pushgateway metrics from a browser's JavaScript.

## reject
Default: -

Plural: yes


Enable filter drop metrics with label name and value (tags for StatsD) equal to field.
For instance, next configuration drops all metrics with labels **http_response_code="404"**, **name="bot"**:

```
entrypoint {
    reject http_response_code 404;
    reject name bot;
    tcp 1111;
}
```

## auth
Default: -

Plural: yes

Possible values:
- `basic <user>:<password>`
- `bearer <token>`
- `other <secret>`

"auth" option allows for HTTP authentication methods to access this port.

There is support for three types of HTTP authentication: Basic, Bearer and simple secret. Basic authentication enables the use of a [login/password](https://datatracker.ietf.org/doc/html/rfc7617) for HTTP authentication, while Bearer authentication allows for the use of statically specified [tokens](https://datatracker.ietf.org/doc/html/rfc6750).
The "other" type of authentication is custom method specific to Alligator, which only allows for the use of statically specified secrets in the configuration file and compares the client passed header for validation.

Alligator responds with a 401 HTTP status code if authentication data is not specified, and it responds with a 403 HTTP status code if authentication fails.

The configuration allows for combination of several methods within one context. For instance, the following configuration enables authentication through any of following header:
```
entrypoint {
    auth basic root:qwerty;
    auth basic user:password;
    auth bearer ZV0aFdP2kx44WVWRSkFCQsDhKvAHuA6Hhw4Kzr6OhoGe13RKxyUjgZo7ODco34sq;
    auth bearer F89rMiV1h8YyVhMIk9rI1GLSxW3fquSHCjf1PuAReABa47ivUbjshmTVZkD5bpXs;
    auth other LUTTK4SrdH;
    auth other RiLJXWD3Xu;
    tcp 1111;
}
```

Example of authorizations on Alligator:
```
$ curl -w '%{response_code}\n' -u nouser:nopassword http://localhost:1111
403
$ curl -w '%{response_code}\n' -u root:qwerty http://localhost:1111
200
$ curl -w '%{response_code}\n' -H "Authorization: Bearer ZV0aFdP2kx44WVWRSkFCQsDhKvAHuA6Hhw4Kzr6OhoGe13RKxyUjgZo7ODco34sq" http://localhost:1111
200
$
```

## auth_header
Default: Authorization

Plural: no

This option allows for the change of the authorization header name to other.

## header
Default: -
Plural: yes

The header option makes it possible to respond with custom headers to clients. Custom headers work only if authorization is passed, or the request method is OPTIONS.

An example of its usage is privided below:

```
entrypoint {
    header access-control-allow-headers 'Authorization';
    header access-control-allow-methods POST;
    header access-control-allow-origin *;
    tcp 1111;
}
```

# ttl
Default: 300

Plural: no

Possible values:
- 0
- {any_number}{any_unit}


This options allows user to set maximum time to live metrics that have been pushed by statsd, pushgateway or graphite methods.
TTL in alligator provides capability to remove obsolete metrics, that haven't been updated for a long time.

A zero value can be used to disable the TTL functionality. This means that any received metrics are never deleted;

The following configuration demonstrates the use of TTL. Metrics that haven't been updated for one hour will be deleted.
```
entrypoint {
    ttl 1h;
    tcp 1111;
}
```

To disable the TTL functionality:
```
entrypoint {
    ttl 0;
    tcp 1111;
}
```

More information about units that user can specify in configuratino can be obtained from configuration [doc](https://github.com/alligatormon/alligator/blob/master/doc/configuration.md).


# tcp, udp, unix, unixgram
Default: -

Plural: yes

Configuration specifies the listen port (or socket) for income queries. This is central part of entrypoint context. Any entrypoint must have at least one of this option to run entrypoint.
Alligator usually runs on port tcp to response with metrics to Prometheus.
UDP configuration is usually can be used for statsd metrics because udp is one of supported protocol for statsd.
UDP and unix socket also can be used for obtain rsyslog stats in [impstats](https://github.com/alligatormon/alligator/blob/master/doc/rsyslog.md).

For example, the following configuration enables the TCP and UDP ports to receive StatsD metrics in TCP and UDP mode:
```
entrypoint {
    tcp 1111;
    udp 1111;
}
```

In the next example, alligator opens two ports on INADDR_ANY and another one on the localhost address:
```
entrypoint {
    tcp 1111 80 127.0.0.1:9000;
}
```

An additional example demonstrating the use of a Unix-socket is as follows:
```
entrypoint {
        unix /tmp/alligator.sock;
}

system {
        base;
}
```

To test this, use the following command:
```
$ curl --unix-socket /tmp/alligator.sock localhost/metrics
context_switches 339451
cores_num 1
cores_num_cgroup 0
cores_num_hw 1
cpu_usage_calc_delta_time 18446744073.591579
cpu_usage_time {type="idle"} 2053.710000
cpu_usage_time {type="iowait"} 16.770000
cpu_usage_time {type="nice"} 0.000000
cpu_usage_time {type="system"} 21.850000
cpu_usage_time {type="user"} 83.900000
forks 4625
interrupts 135704
softirq 180882
time_now 1724141426
$
```

# allow, deny
Default: -

Plural: yes

Possible values:
- {IPv4 address}
- {IPv6 address}
- {IPv4 CIDR}
- {IPv6 CIDR}

These options control access policies for Alligator's port restrictions.
One important thing to note is that this option has a default policy. If first argument in the configuration is "allow", then the default policy will be "deny". Conversely, if the first argument is "deny", it will allow permission to anyone not listed in the "deny" lists.

The following configuration is an example of allowing access to port 80 only from localhost, allowing access to port 1111 from private networks, and implementing a more complex access policy for port 1112:
```
entrypoint {
    tcp 80;
    allow 127.0.0.1;
}

entrypoint {
    tcp 1111;
    allow 192.168.0.0/24;
    allow 172.16.0.0./12;
    allow 10.0.0.0/8;
}

entrypoint {
    tcp 1112;
    deny 100.64.0.0/10;
    deny 10.15.20.30;
    deny 127.0.0.2;
    allow 10.0.0.0/8;
}
```

# api
Default: off\
Plural: no\
Possible values:
- on
- off

Enables or disables the ability to make PUT/POST request to the Alligator API for updating the runtime configuration. The API documentation is available in the [document](https://github.com/alligatormon/alligator/blob/master/doc/api.md)

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
