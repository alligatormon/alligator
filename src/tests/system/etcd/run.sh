#!/bin/sh
[ -z "$1" ] && APPDIR="/app/src/" || APPDIR="$1"
. /app/src/tests/system/common.sh
DIR="etcd"

etcd >/dev/null 2>&1 &
sleep 1

curl -X PUT http://127.0.0.1:2379/v2/keys/_etcd/registry/UUID18/handler -d 'value=alligator_handler=uwsgi'
curl -X PUT http://127.0.0.1:2379/v2/keys/_etcd/registry/UUID18/url -d 'value=alligator_url=tcp://127.0.0.2:1234'
curl http://127.0.0.1:2379/v2/keys/message -XPUT -d value='{"entrypoint": [{"tcp":["1113"]}]}'
$APPDIR/alligator $APPDIR/tests/system/$DIR/alligator.conf&
sleep 5

TEXT=`curl -s localhost:1111`
METRICS=`cat $APPDIR/tests/system/$DIR/metrics.txt`
for NAME in $METRICS
do
	echo "$TEXT" | grep $NAME >/dev/null 2>&1 && success $NAME || error "$TEXT" $NAME
done

ss -nlpt | grep 1113 >/dev/null && echo "configuration OK" || echo "configuration ERR"

kill %1
kill %2
