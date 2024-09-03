# API

JSON API is similar to plaintext configuration. The description of directives can be found in other sections of the documentation.
```
{
  "log_level": "<level>",
  "ttl": "<time>",
  "entrypoint": [
    {
      "log_level": "<level>",
      "return": "[empty|on]>"
    },
    {
      "reject": "key>",
      "auth": [
        {
          "type": "<basic|bearer|other>",
          "data": "<user:password|token|secret>"
        }
      ],
      "auth_header": "<header_name>",
      "env": {
        "<name>": "<value>"
      },
      "ttl": 0,
      "tcp": [
        "<port>",
        "<addr>:<port>"
      ],
      "udp": [
        "<port>"
      ],
      "unixgram": [
        "<path/to/socket>"
      ],
      "unix": [
        "<path/to/socket>"
      ],
      "allow": [
        "<CIDR>"
      ],
      "deny": [
        "<CIDR>"
      ],
      "api": "[on|off]",
      "handler": "<handler>"
    },
    {
      "pingloop": "<number>",
      "metric_aggregation": "[off|count]",
      "cluster": "<cluster_name>",
      "instance": "<instance_name>",
      "lang": "<lang>",
      "mapping": [
        {
          "template": "<template>",
          "name": "<name>",
          "buckets": [
            0.0,
            1.0,
            0.0,
            2.0,
            0.0,
            0.0,
            0.0
          ],
          "le": [
            0.0,
            1.0,
            0.0,
            2.0,
            0.0,
            0.0,
            0.0
          ],
          "quantiles": [
            0.0,
            1.0,
            0.0,
            2.0,
            0.0,
            0.0,
            0.0
          ],
          "match": "[glob]",
          "label": {
            "<label_name>": "<label_value>"
          }
        }
      ]
    }
  ],
  "resolver": [
    "<server1>",
    "...",
    "<serverN>"
  ],
  "system": {
    "base": {},
    "disk": {},
    "network": {},
    "process": [
      "[nginx]",
      "[bash]",
      "[/[bash]*/]"
    ],
    "services": [
      "[nginx.service]"
    ],
    "smart": {},
    "firewall": {
      "ipset": "entries|on"
    },
    "cpuavg": {
      "period": 5
    },
    "packages": [
      "[nginx]",
      "[alligator]"
    ],
    "cadvisor": {},
    "pidfile": [
      "/path/to/pidfile1",
      "...",
      "[/path/to/pidfileN]"
    ],
    "userprocess": [
      "user1",
      "...",
      "[userN]"
    ],
    "groupprocess": [
      "user1",
      "...",
      "[groupN]"
    ],
    "cgroup": [
      "/cgroup1/",
      "...",
      "/[cgroupN]/"
    ],
    "sysfs": "/path/to/dir",
    "procfs": "/path/to/dir",
    "rundir": "/path/to/dir",
    "usrdir": "/path/to/dir",
    "etcdir": "/path/to/dir",
    "log_level": "off"
  },
  "aggregate": [
    {
      "env": {
        "Connection": "close"
      },
      "add_label": {
        "name": "localcluster"
      },
      "pquery": [
        "<query>"
      ],
      "handler": "<parser>",
      "url": "<url>",
      "state": "save",
      "notify": "true",
      "file_stat": "true",
      "checksum": "murmur3",
      "ca_certificate": "/path/to/ca/cert",
      "tls_certificate": "/path/to/client/crt",
      "tls_key": "/path/to/client/key",
      "tls_server_name": "server_name",
      "cluster": "repl",
      "instance": "srv1.example.com:1111",
      "key": "<smth>",
      "name": "<smth>",
      "log_level": "level",
      "timeout": "10s",
      "lang": "something",
      "resolve": "<domainname>",
      "follow_redirects": 0
    }
  ],
  "query": [
    {
      "expr": "<expr>",
      "field": [
        "smth"
      ],
      "make": "make",
      "datasource": "<internal>"
    }
  ],
  "action": [
    {
      "name": "<name>",
      "expr": "url>",
      "ns": "<namespace>",
      "work_dir": "<directory>",
      "serializer": "<serializer>",
      "follow_redirects": "0",
      "engine": "<engine>",
      "index_template": "<index_template>"
    }
  ],
  "scheduler": [
    {
      "name": "<name>",
      "period": "<1h15s>",
      "datasource": "<internal>",
      "action": "<run-script>",
      "lang": "<call-func>",
      "expr": "promql>"
    }
  ],
  "x509": [
    {
      "name": "name",
      "path": "/path/to/certs",
      "match": "match",
      "password": "/password/",
      "type": "/type/"
    }
  ],
  puppeteer" {
    "https://google.com": {
        "headers": {
            "Connection": "close"
            "Host": "hotname"
        },
        "post_data": "body request",
        "timeout": 30000,
        "screenshot": {
            "minimum_code": 400,
            "type": "png",
            "dir": "/path/to/save/pictures/"
        }
    }
}
}
```
