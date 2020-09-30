#!/bin/sh
[ -z "$1" ] && APPDIR="/app/src/" || APPDIR="$1"
. /app/src/tests/system/common.sh

gearmand -d -t 1
sleep 1
gearman -w -f wc -- wc -l &
sleep 1
kill -1 %1

$APPDIR/alligator $APPDIR/tests/system/gearmand/alligator.yaml&
sleep 5

TEXT=`curl -s localhost:1111`
METRICS=`cat $APPDIR/tests/system/gearmand/metrics.txt`
for NAME in $METRICS
do
	echo "$TEXT" | grep $NAME >/dev/null 2>&1 && success $NAME || error "$TEXT" $NAME
done

kill %2
killall gearmand
