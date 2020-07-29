#!/bin/sh
IFS=""
rpm -q rabbitmq-server >/dev/null || yum -y install rabbitmq-server
[ -z "$1" ] && APPDIR="/app/src/" || APPDIR="$1"
$APPDIR/alligator $APPDIR/tests/system/rabbitmq/alligator.conf&
rabbitmq-server >/dev/null &

rabbitmq-plugins enable rabbitmq_management >/dev/null

sleep 5

TEXT=`curl -s localhost:1111`
echo $TEXT | grep -c rabbitmq_vhosts >/dev/null && echo "rabbitmq vhosts metrics ok" || echo "rabbitmq vhosts metrics not found!"
echo $TEXT | grep -c rabbitmq_queue >/dev/null && echo "rabbitmq queue metrics ok" || echo "rabbitmq queue metrics not found!"
echo $TEXT | grep -c rabbitmq_exchanges >/dev/null && echo "rabbitmq exchanges metrics ok" || echo "rabbitmq exchanges metrics not found!"
echo $TEXT | grep -c rabbitmq_nodes >/dev/null && echo "rabbitmq nodes metrics ok" || echo "rabbitmq nodes metrics not found!"

kill %1
kill %2
