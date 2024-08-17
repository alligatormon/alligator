MySQL support by user queries:
```
modules {
	mysql /usr/lib/libmysqlclient.so;
}

aggregate {
	sphinxsearch mysql://127.0.0.1:9313;
	mysql mysql://user:password@127.0.0.1:3306 name=mysql;
}

query {
	expr 'SELECT table_schema "db_name", table_name "table", ROUND(SUM(data_length), 1) "mysql_table_size", ROUND(SUM(index_length), 1) "mysql_index_size", table_rows "mysql_table_rows" FROM information_schema.tables  GROUP BY table_schema';
	field mysql_table_size mysql_index_size mysql_table_rows;
	make mysql_db_size;
	datasource mysql;
}
query {
	expr 'SELECT command command, state state, user user, count(*) mysql_processlist_count, sum(time) mysql_processlist_sum_time FROM information_schema.processlist WHERE ID != connection_id() GROUP BY command,state, user';
	field mysql_processlist_count mysql_processlist_sum_time;
	make mysql_processlist_stats;
	datasource mysql;
}
```
