#!/bin/sh
[ -z "$1" ] && APPDIR="/app/src/" || APPDIR="$1"
. /app/src/tests/system/common.sh
DIR="gdnsd"

gdnsd -c $APPDIR/tests/system/gdnsd/ daemonize
$APPDIR/bin/alligator $APPDIR/tests/system/$DIR/alligator.yaml&
sleep 5

TEXT=`curl -s localhost:1111`
METRICS=`cat $APPDIR/tests/system/$DIR/metrics.txt`
for NAME in $METRICS
do
	echo "$TEXT" | grep $NAME >/dev/null 2>&1 && success $NAME || error "$TEXT" $NAME
done

kill %1
pkill -15 gdnsd
