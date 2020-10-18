#!/bin/sh
[ -z "$1" ] && APPDIR="/app/src/" || APPDIR="$1"
. /app/src/tests/system/common.sh
DIR="json"

nginx -p /app/src/ -c tests/mock/$DIR/nginx.conf
$APPDIR/alligator $APPDIR/tests/mock/$DIR/alligator.conf&
sleep 15

TEXT=`curl -s localhost:1111`
METRICS=`cat $APPDIR/tests/mock/$DIR/metrics.txt`
for NAME in $METRICS
do
	echo "$TEXT" | grep $NAME >/dev/null 2>&1 && success $NAME || error "$TEXT" $NAME
done < $APPDIR/tests/mock/$DIR/metrics.txt

kill %1
nginx -p /app/src/ -c tests/mock/$DIR/nginx.conf -s stop
