This is example of service discovery configuration from consul and etcd.
documentation will appear here soon
```
aggregate {
    # Consul configuration/discovery
    consul-configuration http://localhost:8500;
    consul-discovery http://localhost:8500;

    # Etcd configuration
    etcd-configuration http://localhost:2379;
}
```
