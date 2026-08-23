**Language / Язык:** [English](../resolver.md) | [Русский](resolver.md)

# Resolver
Контекст resolver позволяет задать другие DNS-серверы для разрешения доменных имён.

## Пример
Эта конфигурация заменяет использование resolver, настроенного в ОС, на указанные в списке:
```
resolver {
    udp://8.8.8.8:53;
    udp://8.8.4.4:53;
    tcp://8.8.8.8:53;
    tcp://8.8.4.4:53;
}
```
Также автоматически начинается сбор статистики о времени ответа DNS-серверов.

## Разрешение DNS через alligator
DNS resolver в Alligator используется для следующих целей:
- Простое разрешение имён для обращения к сервисам через aggregator
- Сбор статистики о времени ответа DNS-серверов
- Разрешение DNS-имён в метку метрики

Первый случай работает прозрачно для пользователя.\
Второй и третий — нет.

Например, следующая конфигурация разрешит A-запись google.com.
```
resolver {
    udp://8.8.8.8:53;
}
aggregate {
	dns resolver:// resolve=google.com type=a;
}
```
После этого будут сгенерированы следующие метрики:
```
aggregator_resolve_address {host="udp://8.8.8.8:53", class="IN", type="A", name="google.com"} 1
aggregator_resolve_address {host="udp://8.8.8.8:53", class="IN", type="A", name="google.com"} 1
aggregator_resolve_address {host="udp://8.8.8.8:53", class="IN", type="A", name="google.com"} 1
aggregator_resolve_address {host="udp://8.8.8.8:53", class="IN", type="A", name="google.com"} 1
aggregator_resolve_address {host="udp://8.8.8.8:53", class="IN", type="A", name="google.com"} 1
aggregator_resolve_address {host="udp://8.8.8.8:53", class="IN", type="A", name="google.com"} 1
aggregator_resolve_address {host="udp://8.8.8.8:53", class="IN", type="A", name="google.com"} 1
aggregator_resolve_address {host="udp://8.8.8.8:53", class="IN", type="A", name="google.com"} 1
aggregator_resolve_address {host="udp://8.8.8.8:53", class="IN", type="A", name="google.com"} 1
aggregator_resolve_address {host="udp://8.8.8.8:53", class="IN", type="A", name="google.com"} 1
aggregator_resolve_address {host="udp://8.8.8.8:53", class="IN", type="A", name="google.com"} 1
aggregator_resolve_address {host="udp://8.8.8.8:53", class="IN", type="A", name="google.com"} 1
resolver_read_time_mcs_quantile {quantile="0.90", host="udp://8.8.8.8:53"} 32
resolver_read_time_mcs_quantile {quantile="0.95", host="udp://8.8.8.8:53"} 68
resolver_read_time_mcs_quantile {quantile="0.99", host="udp://8.8.8.8:53"} 68
resolver_response_time_mcs_quantile {quantile="0.90", host="udp://8.8.8.8:53"} 32
resolver_response_time_mcs_quantile {quantile="0.95", host="udp://8.8.8.8:53"} 68
resolver_response_time_mcs_quantile {quantile="0.99", host="udp://8.8.8.8:53"} 68
resolver_write_time_mcs_quantile {quantile="0.90", host="udp://8.8.8.8:53"} 0
resolver_write_time_mcs_quantile {quantile="0.95", host="udp://8.8.8.8:53"} 0
resolver_write_time_mcs_quantile {quantile="0.99", host="udp://8.8.8.8:53"} 0
```

## Явное указание DNS-сервера
Конфигурацию выше можно переписать с явным указанием разных DNS-серверов:
```
aggregate {
	dns udp://8.8.8.8:53 resolve=google.com type=a;
	dns udp://8.8.4.4:53 resolve=yahoo.com type=aaaa;
}
```
