**Language / Язык:** [English](../metric-receive.md) | [Русский](metric-receive.md)

# Приём метрик

Alligator может стоять перед Prometheus как процесс, который принимает push, отдаёт scrape и выполняет blackbox-проверки по запросу. Здесь — сценарий целиком. Списки полей остаются в [entrypoint](entrypoint.md), [cluster](cluster.md) и [probe](probe.md).

Один процесс слушает несколько портов. HTTP-порты делят правила аутентификации. UDP — нет: у StatsD и plaintext Graphite нет заголовков, поэтому их ограничивают через `allow` / `deny`, а Basic и секрет с периодом оставляют на HTTP-порту, который скрейпит Prometheus.

```
entrypoint {
    handler prometheus;
    tcp 1111;
    format prometheus;
    ttl 5m;
    allow 10.0.0.0/8;
}
```

`GET /` и `GET /metrics` отдают серии, лежащие на этом entrypoint. `ttl` удаляет серии StatsD, Graphite и Pushgateway, которые давно не обновлялись. `ttl 0` хранит их до перезапуска.

## Blackbox

Именем blackbox называются две разные задачи.

**По запросу.** Prometheus (или скрипт) вызывает `GET /probe`, в том же виде, что blackbox_exporter. Сначала задайте модуль `probe`. Встроенных имён модулей нет. Обработчик запускает проверку и сразу возвращает последний результат; первый вызов нового target часто пустой.

```
entrypoint {
    handler prometheus;
    tcp 9115;
}

probe {
    name http_2xx;
    prober http;
    valid_status_codes 2xx;
    timeout 5s;
}

probe {
    name icmp;
    prober icmp;
    timeout 5s;
    loop 10;
    percent 0.5;
}
```

```
curl -sG 'http://127.0.0.1:9115/probe' \
  --data-urlencode 'module=http_2xx' \
  --data-urlencode 'target=example.com'
```

`target` — это host или `host:port`. Схему задают `prober` и `tls`. Для HTTPS укажите `tls on` при `prober http`. `prober grpc` вызывает `grpc.health.v1.Health/Check`; TLS задаётся `tls on` у модуля, а не схемой в `target`. Проверки фиксированного списка URL по расписанию — это `aggregate { blackbox … }`, не этот порт. См. [probe](probe.md) и [blackbox](parsers/blackbox.md).

## StatsD

StatsD принимается entrypoint'ом с `handler prometheus` по умолчанию, по UDP или TCP. Точки в имени становятся подчёркиваниями. `|g` и `|ms` заменяют серию (gauge). `|c` прибавляет к ней. Теги DogStatsD после `#` становятся лейблами.

```
entrypoint {
    handler prometheus;
    udp 127.0.0.1:8125;
    tcp 8125;
    ttl 5m;
    allow 10.0.0.0/8;
}
```

```
printf 'app.queue.depth:12|g|#host:web01\n' | nc -u 127.0.0.1 8125
printf 'app.requests:1|c|#host:web01,env:prod\n' | nc 127.0.0.1 8125
```

В хранилище это `app_queue_depth{host="web01"}` и `app_requests{host="web01",env="prod"}`. `mapping` переписывает имена по glob до записи. См. [entrypoint — mapping](entrypoint.md#mapping).

## Graphite

Plaintext Graphite идёт тем же путём приёма: `name value [timestamp]`. Timestamp игнорируется. Значение заменяет серию, как gauge. Точки в пути становятся подчёркиваниями.

```
entrypoint {
    handler prometheus;
    tcp 2003;
    udp 2003;
    ttl 5m;
    allow 10.0.0.0/8;
    mapping {
        template servers.*.cpu.*;
        name node_cpu;
        label host "$1";
        label mode "$2";
        match glob;
    }
}
```

```
printf 'servers.web01.cpu.user 12.5 %s\n' "$(date +%s)" | nc 127.0.0.1 2003
```

С таким mapping серия — `node_cpu{host="web01",mode="user"}`. Без mapping имя будет `servers_web01_cpu_user`: `web02` — это другое имя, а не лейбл `host`. Два агента с одним и тем же путём по-прежнему затирают один gauge.

Текстовый Pushgateway на `POST /metrics/job/...` — HTTP-вариант того же приёма. Формат описан в [entrypoint](entrypoint.md#pushgateway-body-formats). `metric_aggregation count` суммирует counter и histogram Pushgateway внутри Alligator. StatsD и Graphite сами выбирают прибавление или замену через `|c` и голое значение, поэтому эта опция к ним не относится.

## Кластер, gauge и несколько хостов

Для counter и histogram кластер не нужен: их агрегируют через PromQL (`sum`, `rate`, `histogram_quantile`). Для gauge он нужен.

Когда отказоустойчивость не требуется, ставьте Alligator на каждый сервер и скрейпьте этот хост. Gauge остаётся там, куда его отправили.

Чтобы разложить писателей по нескольким Alligator без oplog, закрепите клиента за узлом липкими сессиями в nginx (`ip_hash` или sticky cookie).

`DELETE /metrics/job/...` удаляет серии только на узле, который принял запрос. Это работает для установки per-host и для липких сессий на балансировщике. Если запрос может попасть на любой узел, серию снимают через `ttl`, а не через DELETE.

`cluster { type oplog }` копирует принятые сэмплы на другие узлы Alligator. `sharedlock` — другой тип: crawl `aggregate` выполняется на одном узле и не реплицирует push.

Каждый приёмник перечисляет одни и те же servers и ставит `instance` в свой `host:port` из этого списка. Строка должна совпадать.

```
cluster {
    name recv;
    type oplog;
    size 10000;
    sharding_key __name__;
    replica_factor 2;
    servers recv1.example:1111 recv2.example:1111;
}

entrypoint {
    handler prometheus;
    tcp 1111;
    udp 8125;
    cluster recv;
    instance recv1.example:1111;
    ttl 5m;
}
```

У `recv2` тот же блок с `instance recv2.example:1111`.

`sharding_key __name__` (значение по умолчанию) кладёт все сэмплы одного имени метрики на один primary. `replica_factor 2` хранит эту копию и ещё на одном узле. Gauge не суммируется: на primary побеждает последнее значение серии, реплика хранит то же значение. StatsD `|c` по-прежнему прибавляется на primary. Копия на другом узле зависит от типа. Копии counter и histogram прибавляются. Копии gauge и untyped заменяются. Сэмплы StatsD и Graphite — untyped, пока не включён `metric_aggregation`, поэтому их реплики заменяют значение.

Поэтому нужен лейбл хоста. Два агента, которые шлют `queue_depth` без лейбла, — это одна серия, и более поздний gauge затирает ранний на узле, которому принадлежит это имя. Шлите `queue_depth` с `host=web01` и `host=web02`, и обе серии останутся. Чтобы привязать серию к хосту, а не к имени, задайте `sharding_key host` (в хеш попадает значение лейбла; отсутствующий лейбл ключ не добавляет).

Primary пишет серию в namespace entrypoint. Копия реплики лежит в `<cluster>:<primary-instance>`, например `recv:recv1.example:1111`. Её отдают так: `GET /metrics?namespace=recv:recv1.example:1111`. `GET /oplog` показывает журнал репликации. См. [cluster](cluster.md).

## Кто может пушить и скрейпить

HTTP-аутентификация задаётся на entrypoint. Она закрывает scrape, Pushgateway, `/probe` и API. UDP она не закрывает.

Статические способы: `auth basic`, `auth bearer` и `auth other`. Достаточно любого одного. Нет учётных данных — 401. Данные есть, но не подошли — 403.

Секрет с периодом — вторая группа. `GET /authorize` отдаёт текущий код и сам предыдущий код не требует. `period` — размер бакета. Клиент может прислать текущий бакет или соседний с любой стороны. `validate cookie` читает cookie. `validate header` читает `header_name` (по умолчанию `X-Alligator-Auth`). Это не заголовок `Authorization`. `bind remote_addr`, `bind header <name>` и `bind cookie <name>` привязывают код к адресу принятого клиента, заголовку и cookie. Те же значения нужны и при выпуске кода, и на следующих запросах. См. [authorize](entrypoint.md#authorize).

`satisfy` соединяет две группы:

| Конфиг | Запрос принимается, когда |
|--------|---------------------------|
| `satisfy any` (по умолчанию) | прошёл basic/bearer/other или верен код с периодом |
| `satisfy all` | прошли оба |

`GET /authorize` пропускает только проверку кода с периодом. При `satisfy all` и `auth basic` для выдачи кода всё ещё нужен пароль.

Basic **или** cookie:

```
entrypoint {
    handler prometheus;
    tcp 1111;
    auth basic metrics:change-me;
    satisfy any;
    authorize {
        secret change-me-too;
        period 30s;
        set cookie;
        validate cookie;
        cookie_name alligator_auth;
    }
}
```

```
curl -sD- -u metrics:change-me http://127.0.0.1:1111/authorize
curl -s -u metrics:change-me http://127.0.0.1:1111/metrics
curl -s --cookie alligator_auth=<hex> http://127.0.0.1:1111/metrics
```

Заголовок вместо cookie: `set header`, `validate header`, затем `X-Alligator-Auth: <hex>` на scrape или push. `set json` возвращает `{"secret","period","expires"}`, если клиенту не нужен `Set-Cookie`. `expires` в JSON — конец текущего бакета. Код остаётся годным ещё и на следующем бакете.

Basic **и** заголовок:

```
entrypoint {
    tcp 1111;
    auth basic metrics:change-me;
    satisfy all;
    authorize {
        secret change-me-too;
        period 30s;
        set json;
        validate header;
        header_name X-Alligator-Auth;
    }
}
```

```
code=$(curl -s -u metrics:change-me http://127.0.0.1:1111/authorize | sed -n 's/.*"secret":"\([0-9a-f]*\)".*/\1/p')
curl -s -u metrics:change-me -H "X-Alligator-Auth: $code" http://127.0.0.1:1111/metrics
```

`allow` / `deny` по-прежнему проверяются в момент accept, до этой HTTP-проверки. См. [entrypoint — auth](entrypoint.md#auth) и [authorize](entrypoint.md#authorize).

## Что оставить

`metric_filter` отбрасывает сэмплы на приёме, до `add_label` и `metricstransform`. Лейбл, добавленный через `add_label`, в фильтре не виден. Exclude побеждает. Если есть хотя бы одно правило `include`, имя должно совпасть с одним из них.

```
entrypoint {
    handler prometheus;
    tcp 1111;
    udp 8125;
    metric_filter {
        include ^app_ match_type regexp;
        include ^node_cpu match_type regexp;
        include ^probe_ match_type regexp;
        exclude_label env test match_type strict;
    }
}
```

Фильтр только из include отбрасывает и серии `alligator_*`, принятые в том же context, включая метрики сессий. Добавьте include на нужные имена или оставьте только exclude. Первый блок `metric_filter` на entrypoint побеждает. Следующие блоки игнорируются. См. [entrypoint — metric_filter](entrypoint.md#metric_filter).
