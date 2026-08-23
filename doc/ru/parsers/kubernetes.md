**Language / Язык:** [English](../../parsers/kubernetes.md) | [Русский](kubernetes.md)

## Kubernetes

### Сбор статистики из контейнеров Kubernetes
Чтобы включить сбор статистики из контейнеров Kubernetes, используйте следующую опцию:
```
aggregate {
    kubernetes_operator https://kubernetes.default.svc/?node_local=on;
}
```

Устаревшее poll-based обнаружение по-прежнему доступно:
```
aggregate {
    kubernetes_endpoint https://k8s.example.com 'env=Authorization:Bearer TOKEN';
}
```
Первый шаг включает сбор данных из POD, где существуют и включены аннотации Alligator:
```
alligator/scrape: 'true'
alligator/<port-name>-handler: <parser>
alligator/<port-name>-proto: http|https|tcp|...
alligator/<port-name>-path: /metrics
```

См. [kubernetes-operator.md](../../kubernetes-operator.md) про DaemonSet, node-local mode и развёртывание через Helm.

### Ingress и периодические blackbox-проверки
Чтобы включить сбор ingress и выполнять периодические blackbox-проверки, используйте следующую опцию:
```
aggregate {
    kubernetes_ingress https://k8s.example.com 'env=Authorization:Bearer TOKEN';
}
```

### Ресурсы POD
CAdvisor Alligator реализует метрики из известного экспортёра CAdvisor.

Пример использования в конфигурационном файле:
```
system {
    cadvisor [docker=http://unix:/var/run/docker.sock:/containers/json] [log_level=info] [add_labels=collector:cadvisor];
}
```

### Проверка сертификатов из kubeconfig
Полезная опция — проверка срока действия x509-сертификатов, включённых в kubeconfig.
```
aggregate {
    kubeconfig file:///etc/kubectl/kubeconfig state=begin;
}
```

Для проверки X509-сертификатов в файловой системе см. описание в [контексте x509](https://github.com/alligatormon/alligator/blob/master/doc/x509.md).
