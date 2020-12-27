#!/bin/sh
[ -z "$1" ] && APPDIR="/app/src/" || APPDIR="$1"
. /app/src/tests/system/common.sh
DIR="consul-sd"

consul agent -dev >/dev/null 2>&1 &
sleep 1
consul services register -name=web -port=1334 -address=127.0.0.1 -meta alligator_port=2332 -meta alligator_host=127.0.0.2 -meta alligator_handler=uwsgi -meta alligator_proto=tcp
consul kv put fvwv '{"entrypoint": [{"tcp":["1113"]}]}'
$APPDIR/alligator $APPDIR/tests/system/$DIR/alligator.conf&
sleep 25

TEXT=`curl -s localhost:1111`
METRICS=`cat $APPDIR/tests/system/$DIR/metrics.txt`
for NAME in $METRICS
do
	echo "$TEXT" | grep $NAME >/dev/null 2>&1 && success $NAME || error "$TEXT" $NAME
done

ss -nlpt | grep 1113 >/dev/null && echo "configuration OK" || echo "configuration ERR"

kill %1
kill %2
