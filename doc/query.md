
```
query_period 20s;
query {
	expr 'count by (src_port, process) (socket_stat{process="dockerd", src_port="8085"})';
	make socket_match;
	datasource internal;
}
query {
	expr 'count by (src_port, process) (socket_stat{process="dockerd", src_port="8080"})';
	make socket_match;
	datasource internal;
}
```
