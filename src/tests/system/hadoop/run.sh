#!/bin/sh
[ -z "$1" ] && APPDIR="/app/src/" || APPDIR="$1"
. /app/src/tests/system/common.sh
DIR="hadoop"

hdfs --config tests/system/hadoop/conf/ datanode&
sleep 5
$APPDIR/bin/alligator $APPDIR/tests/system/$DIR/alligator.yaml&
sleep 50

TEXT=`curl -s localhost:1111`
METRICS=`cat $APPDIR/tests/system/$DIR/metrics.txt`
for NAME in $METRICS
do
	echo "$TEXT" | grep $NAME >/dev/null 2>&1 && success $NAME || error "$TEXT" $NAME
done

kill %2
killall -15 java
