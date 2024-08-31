## RabbitMQ

To enable the collection of statistics from RabbitMQ, use the following option:
```
aggregate {
    rabbitmq http://guest:guest@localhost:15672;
}
```

It is also useful to check process statistics, running services and open ports:
```
system {
    process beam.smp;
    services rabbitmq-server.service;
}

query {
    expr 'count by (src_port, process) (socket_stat{process="beam.smp", src_port="25672"})';
    make socket_match;
    datasource internal;
}
query {
    expr 'count by (src_port, process) (socket_stat{process="beam.smp", src_port="15692"})';
    make socket_match;
    datasource internal;
}
query {
    expr 'count by (src_port, process) (socket_stat{process="beam.smp", src_port="15672"})';
    make socket_match;
    datasource internal;
}
query {
    expr 'count by (src_port, process) (socket_stat{process="beam.smp", src_port="5672"})';
    make socket_match;
    datasource internal;
}
query {
    expr 'count by (src_port, process) (socket_stat{process="epmd", src_port="4369"})';
    make socket_match;
    datasource internal;
}
```
