**Language / Язык:** [English](../../parsers/prometheus_metrics.md) | [Русский](prometheus_metrics.md)

## OpenMetrics

Чтобы включить сбор статистики с любых эндпоинтов в формате OpenMetrics, используйте следующую опцию:
```
aggregate {
    prometheus_metrics http://localhost/metrics;
}
```

### Пример чтения метрик из файла:
```
aggregate {
    prometheus_metrics file:///var/run/keepalived_time_state state=save notify=true;
}
```

Эта опция позволяет собирать метрики с других экспортёров.
