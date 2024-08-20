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
- basic <user>:<password>
- bearer <token>
- other <secret>

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
