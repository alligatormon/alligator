# Puppeteer configuration context
It allows the use of the Puppeteer JS module to check a list of sites for loading statistics.

## Requirements
- Installed `Node.js`
- Installed npm package:
```
puppeteer@14.3.0
```
- Installed Chromium browser (or Google Chrome)

The runtime script also requires `/var/lib/alligator/argOptions.js`.

## argOptions.js
`/var/lib/alligator/puppeteer-alligator.js` loads extra launch options from:

`/var/lib/alligator/argOptions.js`

Minimal file:
```js
global.argOptions = {};
module.exports = global.argOptions;
```

Example with explicit browser path:
```js
global.argOptions = {
  executablePath: "/usr/bin/chromium-browser"
};
module.exports = global.argOptions;
```

Example with proxy:
```js
global.argOptions = {
  args: ["--proxy-server=http://127.0.0.1:8080"]
};
module.exports = global.argOptions;
```

## Overview of using puppeteer configuration
```
puppeteer {
    https://google.com;
}
```

## Example in JSON format (extended)
Below is an extended JSON example with all commonly used options:
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

## Example in plain format
Plain config can use inline `key=value` options for each URL:

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
Sets HTTP POST request body.

## timeout
Sets timeout for page loading. Supports duration suffixes (for example `5s`, `1m`, `2m30s`).

## console_events
Controls exporting `eventConsole` metrics from browser console messages.

Enabled only when value is one of:
- `true`
- `"true"`
- `1`

Any other value (or missing option) disables `eventConsole` metric export.

## headers, env
Sets extra request headers.

In plain format:
- `headers=Key:Value`
- `env=Key:Value`

## screenshot
Enables screenshot saving. Screenshot is saved only when response code is greater than or equal to `minimum_code`.

### minimum_code
Minimum response code to trigger screenshot capture.

### type
Screenshot type. Currently, only `png` is supported.

### dir
Directory where screenshots are saved.

### fullPage
If `true`, captures full page screenshot.

## metricstransform
`metricstransform` rewrites label values before exporting metrics.
It is supported in both JSON and plain configuration formats.

Use it when you want to:
- normalize noisy label values (URLs, paths, IDs)
- extract a portion of label value via regex capture groups
- reduce high-cardinality labels

The implementation supports an OTel-collector-like structure:
- `transforms[].include` - target metric name (on [actions](https://github.com/alligatormon/alligator/blob/master/doc/action.md#metricstransform), see [matching stored vs exported names](https://github.com/alligatormon/alligator/blob/master/doc/action.md#matching-metric-names-include-metric-metric-regex))
- `transforms[].match_type` - `strict` or `regexp`
- `transforms[].operations[].action` - use `update_label`
- `transforms[].operations[].label` - label name to update
- `transforms[].operations[].value_actions[]`:
  - `regex` - regex pattern for current label value
  - `replacement` (or `new_value`) - replacement string; supports `$1`, `$2`, ... capture groups
  - optional `flags` (for example `i`)
  - optional `replace_all: true`

### metricstransform example: extract host from URL
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

### metricstransform example: keep path template only
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

### metricstransform example: regexp metric matching
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

### metricstransform example: rewrite `source` in all puppeteer metrics
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

This rule applies to all puppeteer metrics, but updates only series that already have the `source` label.

### metricstransform example in plain config
In plain config, pass the same JSON rule as a string:
```
puppeteer {
  https://example.org metricstransform='{"transforms":[{"include":"^puppeteer_.*$","match_type":"regexp","operations":[{"action":"update_label","label":"source","value_actions":[{"regex":"^https?://([^/]+).*$","replacement":"$1"}]}]}]}';
}
```
