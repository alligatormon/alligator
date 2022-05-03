#!/bin/sh
[ -z "$1" ] && APPDIR="/app/src/" || APPDIR="$1"
. /app/src/tests/system/common.sh

cp $APPDIR/tests/system/keepalived/keepalived.{stats,data} /tmp/

$APPDIR/bin/alligator $APPDIR/tests/system/keepalived/alligator.yaml&
sleep 5

TEXT=`curl -s localhost:1111`
METRICS=`cat $APPDIR/tests/system/keepalived/metrics.txt`
for NAME in $METRICS
do
	echo "$TEXT" | grep $NAME >/dev/null 2>&1 && success $NAME || error "$TEXT" $NAME
done

rm -f /tmp/keepalived.stats
rm -f /tmp/keepalived.data
