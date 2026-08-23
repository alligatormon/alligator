**Language / Язык:** [English](../logging.md) | [Русский](logging.md)

# Руководство по логированию для разработчиков

Диагностика Alligator проходит через функции в `src/common/logs.h` / `src/common/logs.c`. Не используйте `printf` / `puts` / `perror` / `fprintf(stderr)` для операционных логов и не оборачивайте вызовы логов в `if (log_level …)`.

Настройка назначения и каналов для операторов описана в [configuration.md](../configuration.md) (уровни логов, `log_dest`, `log_channel`, raw/out sinks).

## API

| Функция | Когда использовать |
|----------|-------------|
| `glog(priority, fmt, …)` | Глобально / без `context_arg` (старт, парсер конфигурации, модули без carg) |
| `carglog(carg, priority, fmt, …)` | Всё, где есть `context_arg *` (aggregates, entrypoints, probes, parsers) |
| `carg_or_glog(carg, priority, fmt, …)` | Null-safe: ведёт себя как `carglog`, если `carg` задан, иначе как `glog` |
| `langlog(lo, priority, fmt, …)` | Только модуль Lang |
| `cslog(priority, fmt, …)` | Только модуль Chrome CDP |

Отправка пользовательских/преобразованных данных отделена: `carglog_raw`, `carg_emit_log`, `carg_emit_log_document` (см. configuration.md). Не используйте их для диагностики.

Фильтрация: сообщение выводится, когда настроенный `level >= priority`. Для контекста `carg->log_level == 0` означает наследование глобального `ac->log_level`.

## Уровни (`L_*`)

| Приоритет | Для чего |
|----------|---------|
| `L_FATAL` | Невосстановимый abort при старте (редко) |
| `L_ERROR` | Сбои, останавливающие или ломающие текущую операцию (connect, фатальный parse, TLS, auth) |
| `L_WARN` | Деградация / пропуск / неожиданное, но продолжающееся выполнение |
| `L_INFO` | Вехи жизненного цикла (выбран конфиг, начало/конец scrape, SD add/remove) — не на каждую метрику |
| `L_DEBUG` | Шаг на запрос или шаг парсера без payload |
| `L_TRACE` | Детализация на уровне полей; предпочитайте key, размеры, status codes, усечённый preview — не полные тела |

## Правила

1. Предпочитайте `carglog`, когда есть `context_arg *`; иначе `glog`.
2. Завершайте каждую format string символом `\n`. При необходимости включайте `carg->key`, host, `errno` / `strerror` или `uv_strerror`.
3. **Не** пишите `if (ac->log_level > N) printf(...)`. Вызывайте `glog` / `carglog` и позвольте им фильтровать.
4. Внешнюю проверку уровня оставляйте только когда она управляет дорогой работой или поведением, не связанным с логом (например, дамп переменных amtail или наследование stderr Chrome).
5. Не путайте сериализацию конфигурации (`if (log_level)` в `config/get.c` означает «вывести поле») с диагностическим логированием.
6. Не трогайте текст CLI `--help` / usage и stdout тестового harness.

## До / после

```c
/* before — обходит channels, formats и семантику L_* */
if (carg->log_level > 0)
	printf("no arg 'module' in query '%s'\n", http_data->uri);

/* after */
carglog(carg, L_WARN, "no arg 'module' in query '%s'\n", http_data->uri);
```

```c
/* before */
if ((s = socket(AF_UNIX, SOCK_DGRAM, 0)) == -1) {
	if (carg->log_level > 0)
		perror("socket");
	return;
}

/* after */
if ((s = socket(AF_UNIX, SOCK_DGRAM, 0)) == -1) {
	carglog(carg, L_ERROR, "unixgram socket(%s): %s\n", carg->key, strerror(errno));
	return;
}
```

## Антипаттерны

- Ручной `if (log_level > N)` с `printf` / `puts` / `perror`
- Логирование полных тел request/response на `L_INFO` или `L_DEBUG` на горячих путях
- Использование `glog`, когда доступен `carg` (теряются channel контекста и префикс `[key]`)
- Восприятие числовых уровней как старой шкалы `0/1/2/3` — всегда передавайте enum `L_*`
