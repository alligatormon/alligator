# MySQL, MariaDB

MySQL support by user queries:
```

aggregate {
	mysql mysql://user:password@localhost name=mysql;
}

query {
	expr "SELECT table_schema "db_name", ROUND(SUM(data_length + index_length), 1) "mysql_database_size"  FROM information_schema.tables  GROUP BY table_schema";
	field mysql_database_size;
	datasource mysql;
	make mysql_db_size;
}
```
