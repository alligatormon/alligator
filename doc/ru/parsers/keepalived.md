**Language / Язык:** [English](../../parsers/keepalived.md) | [Русский](keepalived.md)

## Keepalived

Чтобы включить сбор статистики с Keepalived, используйте следующую опцию:
```
aggregate {
    keepalived file:///var/run/keepalived.pid state=begin notify=true;
}
```

### Сбор статистики
Помимо обычного способа проверки Keepalived, также поддерживаются уведомления об изменении состояния. Для настройки укажите следующую конфигурацию в контексте `vrrp_instance`:
```
notify /usr/libexec/keepalived-notify.sh root
```

Ниже пример скрипта для добавления метрик в Alligator:
```
#!/bin/sh
umask -S u=rwx,g=rx,o=rx
echo "[$(date -Iseconds)]" "$0" "$@" >>"/var/run/keepalived.$1.$2.state"
echo "keepalive_last_time{type=\"$1\", name=\"$2\", state=\"$3\"} `date +%s`" > /var/run/keepalived_time_state
echo "keepalive_last_status{type=\"$1\", name=\"$2\", state=\"$3\"} 1" >> /var/run/keepalived_time_state
```

Наконец, чтобы Alligator мог проверять этот файл, необходимо указать файл в конфигурации Alligator:
```
aggregate {
    prometheus_metrics file:///var/run/keepalived_time_state state=save notify=true;
}
```

Также полезно проверять статистику процесса и запущенные сервисы:
```
system {
    process keepalived;
    services keepalived.service;
}
```
