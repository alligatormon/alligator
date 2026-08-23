# Service Discovery

**Language / Язык:** [English](service-discovery.md) | [Русский](ru/service-discovery.md)

This is an example of service discovery and configuration retrieval from Consul and Etcd. When specified, alligator starts to recursively gather configuration from the root.
```
aggregate {
    # Consul configuration/discovery
    consul-configuration http://localhost:8500;
    consul-discovery http://localhost:8500;

    # Etcd configuration
    etcd-configuration http://localhost:2379;
}
```
Etcd does not provide a standard schema for service discovery, so it can only be used as a configuration source.


For Consul service discovery, it can be used as an example of registering a service to collect metrics from a specific URL:
```
consul services register -name=web -port=1334 -address=127.0.0.1 -meta alligator_port=2332 -meta alligator_host=127.0.0.2 -meta alligator_handler=uwsgi -meta alligator_proto=tcp
```

To use Consul as a configuration storage, store JSON that matches the alligator schema (see [api.md](api.md)). To obtain a JSON template from a running instance:

```
entrypoint {
    handler prometheus;
    tcp 1111;
}
```

```
curl -s http://127.0.0.1:1111/conf
```

Plain-text config files are converted to the same JSON shape at load time; there is no CLI flag to dump configuration (`-l` sets log level only).

To push configurations to etcd:

```
curl http://127.0.0.1:2379/v2/keys/message -XPUT -d value='{"entrypoint": [{"tcp":["1111"]}]}'
```
