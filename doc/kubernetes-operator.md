# Kubernetes operator (native)

Alligator includes an in-process Kubernetes operator via the `kubernetes_operator` aggregate parser.

## Features

- **Watch API** with `timeoutSeconds=3600` (reconnects automatically after server timeout)
- **Label selector** `alligator.io/enabled=true` on API list/watch (faster than full-cluster pod LIST)
- Pod label `alligator.io/enabled: "true"` or annotation `alligator/scrape` for opt-in
- Pod annotation discovery (port `alligator/<port>-{handler,proto,path}`)
- **Node-local** scraping when `NODE_NAME` is set or `node_local=on`
- Namespace filter via `namespace=` URL query or JSON `namespaces` array
- Target reconcile with stable keys (`k8s:namespace:uid:container:port`) and stale target removal
- In-cluster ServiceAccount token injection for API calls
- **sharedlock** for cluster-wide aggregates such as `kubernetes_ingress`

## Pod labels

Scraped workloads should carry:

```yaml
metadata:
  labels:
    alligator.io/enabled: "true"
  annotations:
    alligator/scrape: "true"
    alligator/http-handler: "prometheus"
    alligator/http-proto: "http"
    alligator/http-path: "/metrics"
```

Either the label or `alligator/scrape` enables discovery; port annotations are still required.

## Configuration

Plain config:

```
aggregate {
  kubernetes_operator https://kubernetes.default.svc/?node_local=on&namespace=mail;
}
```

JSON API:

```json
{
  "aggregate": [{
    "handler": "kubernetes_operator",
    "url": "https://kubernetes.default.svc/",
    "node_local": true,
    "watch": true,
    "namespaces": ["mail", "prod"]
  }]
}
```

Watch is enabled automatically in-cluster (ServiceAccount token present). Disable with `watch=off` in the URL query, JSON `"watch": false`, or `ALLIGATOR_K8S_WATCH=off`.

## DaemonSet + Helm

See [deploy/charts/alligator/README.md](../deploy/charts/alligator/README.md).

## Backward compatibility

`kubernetes_endpoint` remains available and now also uses node-local filtering when `NODE_NAME` is present, with target reconcile.
