**Language / Язык:** [English](../grok.md) | [Русский](grok.md)

# Grok

Этот контекст позволяет разбирать логи в метрики, аналогично парсеру Grok в Elasticsearch.


## key
По умолчанию: -\
Множественное число: нет

Задаёт имя контекста. Его можно использовать как ссылку в scheduler или в API.


## name
По умолчанию: -\
Множественное число: нет

Имя метрики.


## match
По умолчанию: -\
Множественное число: нет

Шаблон для сопоставления со строкой лога. Захваченные поля извлекаются и добавляются как метки.

Сопоставление использует **PCRE 8** (тот же движок, что в amtail / selectors). `%{PATTERN:field}`
превращается в именованную группу `(?<field>...)`. Имена захватов должны быть `[A-Za-z_][A-Za-z0-9_]*`.
Alligator нормализует имена в стиле Elastic (`[process][name]`, `field:int`) в
`process_name` / `field` перед компиляцией. Предпочтительно писать в шаблонах имена, допустимые в PCRE
(`client_ip`, а не `client-ip`).


## log\_channel\_out (преобразованные логи)
Когда aggregate/entrypoint задаёт `log_channel_out` и строка **совпадает**, Alligator
отправляет один **плоский** JSON-документ в этот канал: именованные захваты плюс `message`
(совпавшая строка). Метрики по-прежнему создаются как обычно (двойной приёмник).
Это расширение Alligator — не часть самого Elastic Grok.


## bucket
По умолчанию: -\
Множественное число: да

Включает разделение входных метрик на несколько корзин (buckets).


## quantiles
По умолчанию: -\
Множественное число: да

Включает расчёт квантилей по значениям метрик.


## counter
По умолчанию: -\
Множественное число: да

Добавляет дополнительные счётчики метрик для каждой строки лога.

## splited_tags
По умолчанию: -\
Множественное число: нет

Задаёт разделённые метки (актуально для массивных переменных nginx вроде $upstream_addr, $upstream_response_time и т. п.)

Формат:
```
splited_tags <separator> [label1] [label2] ... [labelN];
```


## splited_inherit_tag
По умолчанию: -\
Множественное число: нет

Задаёт наследуемые метки с общего уровня тегов.

Формат:
```
splited_inherit_tag [label1] [label2] ... [labelN];
```


## splited_counter
По умолчанию: -\
Множественное число: да

Добавляет дополнительные счётчики метрик для каждой строки лога. Специально для массивных переменных с разделителем, заданным третьим аргументом.

Формат:
```
splited_counter <metric name> <label name> <separator>
```


## splited_quantiles
По умолчанию: -\
Множественное число: да

Включает расчёт квантилей по значениям метрик. Специально для массивных переменных с разделителем, заданным третьим аргументом.

Формат:
```
splited_quantiles <metric name> <label name> <separator> [quantile1] [quantile2] ... [quantileN];
```


## splited_bucket
По умолчанию: -\
Множественное число: да

Включает разделение входных метрик на несколько корзин. Специально для массивных переменных с разделителем, заданным третьим аргументом.

Формат:
```
splited_bucket <metric name> <label name> <separator> [bucket1] [bucket2] ... [bucketN];
```



Этот контекст следует использовать вместе с глобальной опцией grok\_patterns:
```
grok_patterns /etc/grok-patterns/patterns.conf;
```

Эта опция принимает массив путей к файлам с предопределёнными шаблонами типов данных.
Файл /etc/grok-patterns/patterns.conf содержит набор шаблонов по умолчанию.
Пользователи могут переопределить эти значения по умолчанию или добавить свои шаблоны, указав дополнительные файлы вторым и последующими аргументами.


## Обзор
Примеры использования будут приведены в документации, которая появится позже.

```
aggregate {
    grok file:///var/log/dmesg name=dmesg state=stream;
}
grok_patterns /etc/grok-patterns/patterns.conf;
grok {
  key dmesg;
  name dmesg_event;
  match '%{TIMESTAMP}] %{WORD:process}';
}
```

Альтернативно можно использовать локальную UDP-точку, которая разбирает логи в метрики:
```
entrypoint {
        udp 1112;
        handler grok;
        grok customword;
}

grok_patterns /etc/grok-patterns/patterns.conf;

grok {
  key customword;
  name customword;
  match '%{WORD:word}';
}
```

Проверить можно так:
```bash
$ echo something | nc -u 0.0.0.0 1112
^C
$ curl -s localhost:1111 | grep custom
customword {word="something"} 1.000000
$
```

## Пример с nginx:
```
aggregate {
    grok file:///var/log/nginx-access.log name=nginx log_level=off;
}

grok {
  key nginx;
  name nginx_log;
  match '%{IPORHOST:client_ip} - %{DATA} \[%{HTTPDATE}\] "%{DATA:request}" %{NUMBER:status} %{NUMBER:bytes} "%{DATA}" "%{DATA}" utadr="%{DATA:upstream_addr}" rt=%{DATA:response_time} ut="%{DATA:upstream_time}" us="%{WORD:upstream_status}';
  counter nginx_log_response_bytes bytes;
  quantiles nginx_log_response_time response_time 0.999 0.95 0.9;

  splited_tags ", " upstream_status upstream_addr;
  splited_inherit_tag server_name;
  splited_quantiles nginx_upstream_response_time upstream_time ", " 0.5 0.75 0.9;
}
```
