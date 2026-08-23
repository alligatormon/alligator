**Language / Язык:** [English](../kubernetes-operator.md) | [Русский](kubernetes-operator.md)

# Kubernetes operator (native)

Alligator включает in-process Kubernetes operator через aggregate parser `kubernetes_operator`.

## Возможности

- **Watch API** с `timeoutSeconds=3600` (автоматически переподключается после server timeout)
- **Label selector** `alligator.io/enabled=true` в API list/watch (быстрее, чем полный LIST pod по кластеру)
- Метка pod `alligator.io/enabled: "true"` или аннотация `alligator/scrape` для opt-in
- Обнаружение по аннотациям pod (порт `alligator/<port>-{handler,proto,path}`)
- **Node-local** scraping, когда задан `NODE_NAME` или `node_local=on`
- Фильтр namespace через query-параметр URL `namespace=` или JSON-массив `namespaces`
- Reconcile target со стабильными ключами (`k8s:namespace:uid:container:port`) и удалением устаревших target
- In-cluster инъекция ServiceAccount token для вызовов API
- **sharedlock** для кластерных aggregate, таких как `kubernetes_ingress`

## Метки pod

Scrape-нагрузки должны иметь:

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

Для discovery достаточно метки или `alligator/scrape`; аннотации портов по-прежнему обязательны.

## Конфигурация

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

Watch включается автоматически in-cluster (при наличии ServiceAccount token). Отключите через `watch=off` в query URL, JSON `"watch": false` или `ALLIGATOR_K8S_WATCH=off`.

## DaemonSet + Helm

См. [deploy/charts/alligator/README.md](../../deploy/charts/alligator/README.md).

## Обратная совместимость

`kubernetes_endpoint` по-прежнему доступен и теперь также использует node-local фильтрацию при наличии `NODE_NAME`, с reconcile target.
