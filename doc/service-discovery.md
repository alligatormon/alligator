# Service Discovery

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

To use Consul as a configuration storage, you can utilize the JSON implementation of alligator configuration. To illustrate an example of a JSON-encoded configuration in alligator, you can simply use the command `alligator -l 1 <path_to_conf>`. This encodes the configuration in JSON format, providing a basis for settings.
```
'{"entrypoint": [{"tcp":["1111"]}]}'
```

To push configurations to etcd, you can use the following directive:
```
curl http://127.0.0.1:2379/v2/keys/message -XPUT -d value='{"entrypoint": [{"tcp":["1111"]}]}'
```
