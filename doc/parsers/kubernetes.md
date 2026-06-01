## Kubernetes

### Collecting Statistics from Kubernetes Containers
To enable the collection of statistics from Kubernetes containers, use the following option:
```
aggregate {
    kubernetes_operator https://kubernetes.default.svc/?node_local=on;
}
```

Legacy poll-based discovery remains available:
```
aggregate {
    kubernetes_endpoint https://k8s.example.com 'env=Authorization:Bearer TOKEN';
}
```
The first step enables the gathering of data from PODs where Alligator annotations exist and is enabled:
```
alligator/scrape: 'true'
alligator/<port-name>-handler: <parser>
alligator/<port-name>-proto: http|https|tcp|...
alligator/<port-name>-path: /metrics
```

See [kubernetes-operator.md](../kubernetes-operator.md) for DaemonSet, node-local mode, and Helm deployment.

### Ingresses and Periodic Blackbox Checks
To enable the collection of ingresses and to perform periodic blackbox checks, use the following option:
```
aggregate {
    kubernetes_ingress https://k8s.example.com 'env=Authorization:Bearer TOKEN';
}

### PODs resources
Alligator's CAdvisor implements metrics from the well-known exporter called CAdvisor.

Example of usage in the configuration file:
```
system {
    cadvisor [docker=http://unix:/var/run/docker.sock:/containers/json] [log_level=info] [add_labels=collector:cadvisor];
}
```

### Checking Certificates Specified in Kubeconfig
It's a useful option to check the expiration date of included x509 certificates in the kubeconfig.
```
aggregate {
    kubeconfig file:///etc/kubectl/kubeconfig state=begin;
}
```

For checking X509 certificates on the filesystem, please refer to the explanation provided in the x509 [context](https://github.com/alligatormon/alligator/blob/master/doc/x509.md).
