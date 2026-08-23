**Language / Язык:** [English](../service-discovery.md) | [Русский](service-discovery.md)

# Service Discovery

Это пример service discovery и получения конфигурации из Consul и Etcd. При указании alligator начинает рекурсивно собирать конфигурацию из корня.
```
aggregate {
    # Consul configuration/discovery
    consul-configuration http://localhost:8500;
    consul-discovery http://localhost:8500;

    # Etcd configuration
    etcd-configuration http://localhost:2379;
}
```
Etcd не предоставляет стандартную схему service discovery, поэтому его можно использовать только как источник конфигурации.


Для service discovery в Consul можно использовать пример регистрации сервиса для сбора метрик с конкретного URL:
```
consul services register -name=web -port=1334 -address=127.0.0.1 -meta alligator_port=2332 -meta alligator_host=127.0.0.2 -meta alligator_handler=uwsgi -meta alligator_proto=tcp
```

Чтобы использовать Consul как хранилище конфигурации, можно воспользоваться JSON-представлением конфигурации alligator. Чтобы получить пример JSON-кодированной конфигурации alligator, выполните команду `alligator -l 1 <path_to_conf>`. Она кодирует конфигурацию в JSON и может служить основой для настроек.
```
'{"entrypoint": [{"tcp":["1111"]}]}'
```

Чтобы отправить конфигурации в etcd, используйте следующую директиву:
```
curl http://127.0.0.1:2379/v2/keys/message -XPUT -d value='{"entrypoint": [{"tcp":["1111"]}]}'
```
