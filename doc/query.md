# Query
The query context creates metrics based on a query. The query can be processed on either a local metrics on Alligator or on the database.\


## make
Specifies the name of the created metric for internal queries and serves as the key for API operations.


## expr
The query that is relevant to the specified database.\
For instance, it can be in promql format for internal Alligator metric storage. Alternatively, it can be an [SQL](https://github.com/alligatormon/alligator/blob/master/doc/parsers/postgresql.md) query.


## action
Specifies the action context to run when expr is triggered. This is working only for 'internal' datasource.\
Here is an [explanation](https://github.com/alligatormon/alligator/blob/master/doc/action.md) of action context.


## datasource
Specifies the data source for the query.\
When making local metric requests, use the 'internal' key.\
For external databases, the 'name' field must be specified in the aggregator context with the database, and this name will serve as the datasource for the query.


## ns
When the database have internal namespaces (e.g., databases in a Relational DB), this field specifies the name of that namespace.


## field
Used to select the column in SQL responses. The column name will be used as the metric name and column values as the metric values.


## Examples

### Internal Alligator queries

This configuration will check the existance of process 'dockerd' on ports 8085 and 8080 and then create metric `socket_match` as a result of a query:
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

### External database queries

This configuration collect metrics about database sizes from an external MySQL instance:
```
aggregate {
	mysql mysql://user:password@127.0.0.1:3306 name=mysql;
}

query {
	expr 'SELECT table_schema "db_name", table_name "table", ROUND(SUM(data_length), 1) "mysql_table_size", ROUND(SUM(index_length), 1) "mysql_index_size", table_rows "mysql_table_rows" FROM information_schema.tables  GROUP BY table_schema';
	field mysql_table_size mysql_index_size mysql_table_rows;
	make mysql_db_size;
	datasource mysql;
}
```
