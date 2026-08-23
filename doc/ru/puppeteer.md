**Language / Язык:** [English](../puppeteer.md) | [Русский](puppeteer.md)

# Контекст конфигурации Puppeteer

Позволяет использовать JS-модуль Puppeteer для проверки списка сайтов и сбора статистики загрузки.

## Требования
- Установленный `Node.js`
- Установленный npm-пакет:
```
puppeteer@14.3.0
```
- Установленный браузер Chromium (или Google Chrome)

Скрипт времени выполнения также требует `/var/lib/alligator/argOptions.js`.

## argOptions.js
`/var/lib/alligator/puppeteer-alligator.js` загружает дополнительные параметры запуска из:

`/var/lib/alligator/argOptions.js`

Минимальный файл:
```js
global.argOptions = {};
module.exports = global.argOptions;
```

Пример с явным путём к браузеру:
```js
global.argOptions = {
  executablePath: "/usr/bin/chromium-browser"
};
module.exports = global.argOptions;
```

Пример с прокси:
```js
global.argOptions = {
  args: ["--proxy-server=http://127.0.0.1:8080"]
};
module.exports = global.argOptions;
```

## Обзор использования конфигурации puppeteer
```
puppeteer {
    https://google.com;
}
```

## Пример в формате JSON (расширенный)
Ниже расширенный JSON-пример со всеми часто используемыми опциями:
```
"puppeteer": {
    "https://google.com": {
        "add_label": {
            "team": "sre",
            "service": "web-check"
        },
        "headers": {
            "Connection": "close",
            "Host": "google.com"
        },
        "env": {
            "X-Debug": "1"
        },
        "post_data": "body request",
        "console_events": true,
        "timeout": "5s",
        "metricstransform": {
            "transforms": [
                {
                    "include": "puppeteer_eventSourceResponseStatus",
                    "match_type": "strict",
                    "operations": [
                        {
                            "action": "update_label",
                            "label": "source",
                            "value_actions": [
                                {
                                    "regex": "^https?://([^/]+).*$",
                                    "replacement": "$1"
                                }
                            ]
                        }
                    ]
                }
            ]
        },
        "screenshot": {
            "minimum_code": 400,
            "type": "png",
            "dir": "/var/lib/alligator/",
            "fullPage": false
        }
    }
}
```

## Пример в plain-формате
Plain-конфиг может использовать inline-опции `key=value` для каждого URL:

```
puppeteer {
  https://google.com \
    timeout=5s \
    console_events=true \
    post_data='{"ping":"ok"}' \
    headers=Connection:close \
    headers=Host:google.com \
    env=X-Debug:1 \
    add_label=team:sre \
    add_label=service:web-check \
    screenshot=minimum_code:400 \
    screenshot=type:png \
    screenshot=dir:/var/lib/alligator/ \
    screenshot=fullPage:false;
}
```

## post_data
Задаёт тело HTTP POST-запроса.

## timeout
Задаёт таймаут загрузки страницы. Поддерживаются суффиксы длительности (например, `5s`, `1m`, `2m30s`).

## console_events
Управляет экспортом метрик `eventConsole` из сообщений консоли браузера.

Включено только если значение одно из:
- `true`
- `"true"`
- `1`

Любое другое значение (или отсутствие опции) отключает экспорт метрик `eventConsole`.

## headers, env
Задают дополнительные заголовки запроса.

В plain-формате:
- `headers=Key:Value`
- `env=Key:Value`

## screenshot
Включает сохранение скриншотов. Скриншот сохраняется только если код ответа больше или равен `minimum_code`.

### minimum_code
Минимальный код ответа для срабатывания захвата скриншота.

### type
Тип скриншота. Сейчас поддерживается только `png`.

### dir
Каталог, куда сохраняются скриншоты.

### fullPage
Если `true`, захватывается скриншот всей страницы.

## metricstransform
`metricstransform` переписывает **ключи и/или значения меток** перед экспортом метрик (та же семантика на этапе экспорта, что и в [action](action.md#metricstransform)).
Поддерживается и в JSON, и в plain-конфигурации.

Используйте, когда нужно:
- нормализовать шумные значения меток (URL, пути, ID)
- извлечь часть значения метки через группы захвата regex
- снизить кардинальность меток
- переименовать ключ метки при экспорте (`new_label` в plain или JSON, или `label_key_actions` только в JSON)

Реализация поддерживает структуру, похожую на OTel Collector:
- `transforms[].include` — целевое имя метрики (в [action](action.md#metricstransform) см. [сопоставление сохранённых и экспортируемых имён](action.md#сопоставление-имён-метрик-include-metric-metric_regex))
- `transforms[].match_type` — `strict` или `regexp`
- `transforms[].operations[].action` — используйте `update_label`
- `transforms[].operations[].label` — имя метки для обновления
- `transforms[].operations[].new_label` — необязательное фиксированное новое имя ключа (игнорируется, если задан `label_key_actions`)
- `transforms[].operations[].label_key_actions[]` — необязательные шаги regex по **ключу** (те же поля, что у `value_actions`)
- `transforms[].operations[].value_actions[]`:
  - `regex` — шаблон regex для текущего значения метки
  - `replacement` (или `new_value`) — строка замены; поддерживает группы захвата `$1`, `$2`, ...
  - необязательный `flags` (например, `i`)
  - необязательный `replace_all: true`

### metricstransform: извлечение хоста из URL
```
"puppeteer": {
  "https://example.org": {
    "metricstransform": {
      "transforms": [
        {
          "include": "puppeteer_eventSourceResponseStatus",
          "match_type": "strict",
          "operations": [
            {
              "action": "update_label",
              "label": "source",
              "value_actions": [
                {
                  "regex": "^https?://([^/]+).*$",
                  "replacement": "$1"
                }
              ]
            }
          ]
        }
      ]
    }
  }
}
```

### metricstransform: оставить только шаблон пути
```
"puppeteer": {
  "https://api.example.org/orders/12345": {
    "metricstransform": {
      "transforms": [
        {
          "include": "puppeteer_eventRequestFailed",
          "match_type": "strict",
          "operations": [
            {
              "action": "update_label",
              "label": "source",
              "value_actions": [
                {
                  "regex": "(^https?://[^/]+/orders/)[0-9]+(.*$)",
                  "replacement": "$1{id}$2"
                }
              ]
            }
          ]
        }
      ]
    }
  }
}
```

### metricstransform: сопоставление метрик по regexp
```
"puppeteer": {
  "https://example.org": {
    "metricstransform": {
      "transforms": [
        {
          "include": "^puppeteer_event.*$",
          "match_type": "regexp",
          "operations": [
            {
              "action": "update_label",
              "label": "source",
              "value_actions": [
                {
                  "regex": "(\\?.*)$",
                  "replacement": "",
                  "replace_all": false
                }
              ]
            }
          ]
        }
      ]
    }
  }
}
```

### metricstransform: переписать `source` во всех метриках puppeteer
```
"puppeteer": {
  "https://example.org": {
    "metricstransform": {
      "transforms": [
        {
          "include": "^puppeteer_.*$",
          "match_type": "regexp",
          "operations": [
            {
              "action": "update_label",
              "label": "source",
              "value_actions": [
                {
                  "regex": "^https?://([^/]+).*$",
                  "replacement": "$1"
                }
              ]
            }
          ]
        }
      ]
    }
  }
}
```

Это правило применяется ко всем метрикам puppeteer, но обновляет только ряды, у которых уже есть метка `source`.

### metricstransform: пример в plain-конфиге

Правило можно передать как **JSON-строку** (тот же объект, что в JSON-конфиге):

```
puppeteer {
  https://example.org metricstransform='{"transforms":[{"include":"^puppeteer_.*$","match_type":"regexp","operations":[{"action":"update_label","label":"source","value_actions":[{"regex":"^https?://([^/]+).*$","replacement":"$1"}]}]}]}';
}
```

Или использовать **нативный блок** (без JSON); ключевые слова те же, что в [action.md § metricstransform](action.md#metricstransform):

```
puppeteer {
  https://example.org metricstransform {
    include ^puppeteer_.*$ match_type regexp label source regex '^https?://([^/]+).*$' replacement '$1';
  };
}
```

## См. также: chromecdp

Для той же категории проверок загрузки в браузере без Node.js и npm-пакета Puppeteer используйте нативную интеграцию CDP в [chromecdp.md](../chromecdp.md). Блоки `puppeteer` и `chromecdp` могут сосуществовать; см. [Сравнение с `puppeteer`](../chromecdp.md#comparison-with-puppeteer) в том документе — отличия по runtime, метрикам и конфигурации.
