#!/bin/sh
[ -z "$1" ] && APPDIR="/app/src/" || APPDIR="$1"
$APPDIR/alligator $APPDIR/tests/system/mysql/alligator.conf&
rabbitmq-server&

curl -s localhost:1111 | grep -c rabbitmq_vhosts >/dev/null || echo "rabbitmq vhosts metrics not found!"
curl -s localhost:1111 | grep -c rabbitmq_queue || echo "rabbitmq queue metrics not found!"
curl -s localhost:1111 | grep -c rabbitmq_exchanges || echo "rabbitmq exchanges metrics not found!"
curl -s localhost:1111 | grep -c rabbitmq_nodes || echo "rabbitmq nodes metrics not found!"

kill %1
kill %2
