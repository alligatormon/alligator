#!/bin/sh
[ -z "$1" ] && APPDIR="/app/src/" || APPDIR="$1"
DIR="proxysql"
. /app/src/tests/system/common.sh

proxysql&
$APPDIR/bin/alligator $APPDIR/tests/system/$DIR/alligator.conf&
sleep 9

TEXT=`curl -s localhost:1111`
METRICS=`cat $APPDIR/tests/system/$DIR/metrics.txt`
for NAME in $METRICS
do
	echo "$TEXT" | grep $NAME >/dev/null 2>&1 && success $NAME || error "$TEXT" $NAME
done

kill %1
kill %2
