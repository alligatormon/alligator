# Alligator Helm chart

Install native Kubernetes operator mode as a DaemonSet:

```bash
helm install alligator ./deploy/charts/alligator -n monitoring --create-namespace
```

Upgrade:

```bash
helm upgrade alligator ./deploy/charts/alligator -n monitoring -f my-values.yaml
```

## Highlights

- **DaemonSet**: one collector pod per node
- **node-local scraping**: `kubernetes_operator` with `fieldSelector=spec.nodeName=...`
- **sharedlock cluster**: optional dynamic peer discovery via headless Service Endpoints
- **ConfigMap bootstrap**: rendered from `values.yaml`; DaemonSet rolls on checksum change

## Key values

| Value | Description |
|-------|-------------|
| `operator.enabled` | Enable `kubernetes_operator` aggregate |
| `operator.nodeLocal` | Scrape only pods on the same node |
| `operator.namespaces` | Limit to namespaces (empty = all allowed by RBAC) |
| `cluster.enabled` | Enable sharedlock cluster block |
| `cluster.dynamicPeers` | Discover peers from headless Service Endpoints |
| `serviceMonitor.enabled` | Create Prometheus PodMonitor |

## Scraped workloads (label + annotations + ports)

The operator **watch/list** only requests pods with label `alligator.io/enabled=true` (fewer objects from the API). Each workload you want scraped must carry that label (or rely on the legacy `alligator/scrape` annotation alone — see below).

### Why both `spec.containers[].ports` and annotations?

This is not Helm-specific; it matches how the in-process operator has always worked:

| Source | Role |
|--------|------|
| **Pod spec** `containers[].ports[]` | Declares which ports exist, their **names**, and **`containerPort`** numbers. The operator builds scrape URLs as `{proto}://{podIP}:{containerPort}{path}` only for ports listed here. |
| **Annotations** `alligator/<port-name>-{handler,proto,path}` | Alligator-specific scrape settings Kubernetes does not model: parser/handler, HTTP(S), metrics path. The `<port-name>` segment must equal the port **`name`** in the pod spec (e.g. port `name: metrics` → `alligator/metrics-handler`). |

So the port block is not “optional metadata” — without a named port in the spec there is no container port to connect to. Annotations alone cannot invent a port number.

Opt-in (at least one):

- **Label** `alligator.io/enabled: "true"` — recommended; also satisfies the API label selector.
- **Annotation** `alligator/scrape: "true"` — still supported if you cannot add the label.

Example (port name `metrics` → annotations `alligator/metrics-*`):

```yaml
metadata:
  labels:
    alligator.io/enabled: "true"
  annotations:
    alligator/scrape: "true"
    alligator/metrics-handler: prometheus_metrics
    alligator/metrics-proto: http
    alligator/metrics-path: /metrics
spec:
  containers:
    - name: app
      ports:
        - name: metrics          # must match the prefix in alligator/metrics-*
          containerPort: 8080
```

Multiple ports: add one `ports[]` entry per port and a matching `alligator/<that-port-name>-{handler,proto,path}` set.

## Prometheus

Scrape **each** Alligator pod (PodMonitor), not a load-balanced Service.
