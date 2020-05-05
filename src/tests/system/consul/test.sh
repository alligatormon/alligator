DIR="tests/system/consul"
SDIR="tests/system"

test_consul_start()
{
	./tests/system/daemonize.sh start consul-server consul agent -config-file $DIR/config.json -dev
	sleep 10
	$SDIR/daemonize.sh start consul-client consul agent -config-file $DIR/config-client.json
}

test_consul_stop()
{
	$SDIR/daemonize.sh stop consul-client
	$SDIR/daemonize.sh stop consul-server
}

test_alligator_start()
{
	$SDIR/daemonize.sh start alligator ./alligator $1
	sleep 1
}

test_alligator_stop()
{
	$SDIR/daemonize.sh stop alligator
}

test_consul_load_data()
{
	curl -XPUT -d @$DIR/http1.json http://localhost:8500/v1/agent/service/register
	curl -XPUT -d @$DIR/http2.json http://localhost:8500/v1/agent/service/register
}

delete_data()
{
	rm -rf /opt/consul/
}

test_consul_conf()
{
	delete_data
	test_consul_start
	test_alligator_start $DIR/alligator.conf
	test_consul_load_data

	curl http://127.0.0.1:8500/v1/agent/services | jq
	curl -s localhost:1111

	test_alligator_stop
	#test_consul_stop
}

test_consul_conf
