# Configuration
## Available log levels
- off
- fatal
- error
- warn
- info
- debug
- trace

## Log destinations
```
log_dest <dest>;
```

Destination can be standart streams of a Unix OS, a file or a UDP port. For example this directive can be set as follows:
```
- stdout
- stderr
- file:///var/log/messages
- udp://127.0.0.1:514
```

## Available units for time data in configuration file:
- ms
- s
- h
- d



## /etc/alligator.conf
Bellow is an example of the structure configuration file for Alligator:
```
log_level <level>;
ttl <time>;

entrypoint {
    <options>;
}

resolver {
    <server1>;
    <server2>;
    ...
    <serverN>;
}

system {
    <option1>;
    <option2>;
    ...
    <optionN> [<param1>] ... [<paramN>];
}

aggregate {
    <option1>;
    <option2>;
    ...
    <optionN>;
}

query {
    <query1 options>;
}

query {
    <query2 options>;
}

action {
    <action1 options>;
}

action {
    <action2 options>;
}

scheduler {
    <scheduler1 options>;
}

scheduler {
    <scheduler2 options>;
}

x509 {
    <certificate options>;
}

x509 {
    <certificate options>;
}

puppeteer {
    <puppeteer options>;
}
```

# Support environment variables
`__` is a nesting separator of contexts
Example:
```
export ALLIGATOR__ENTRYPOINT0__TCP0=1111
export ALLIGATOR__ENTRYPOINT0__TCP1=1112
export ALLIGATOR__TTL=1200
export ALLIGATOR__LOG_LEVEL=0
export ALLIGATOR__AGGREGATE0__HANDLER=tcp
export ALLIGATOR__AGGREGATE0__URL="tcp://google.com:80"
```
converts to configuration:
```
{
  "entrypoint": [
    {
      "tcp": [
        "1111",
        "1112"
      ]
    }
  ],
  "ttl": "1200",
  "log_level": "0",
  "aggregate": [
    {
      "handler": "tcp",
      "url": "tcp://google.com:80"
    }
  ]
}
```

