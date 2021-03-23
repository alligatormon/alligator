#!/bin/sh
[ -z "$1" ] && APPDIR="/app/src/" || APPDIR="$1"
. /app/src/tests/system/common.sh
DIR="zookeeper-sd"

zookeeper-server start
sleep 1
#echo 1 > /var/lib/zookeeper/myid
/usr/lib/zookeeper/bin/zkCli.sh create /test '{"aggregate": [{"url": "https://google.com", "handler": "http"}]}' >/dev/null 2>&1
$APPDIR/bin/alligator $APPDIR/tests/system/$DIR/alligator.conf&
sleep 25

TEXT=`curl -s localhost:1111`
METRICS=`cat $APPDIR/tests/system/$DIR/metrics.txt`
for NAME in $METRICS
do
	echo "$TEXT" | grep $NAME >/dev/null 2>&1 && success $NAME || error "$TEXT" $NAME
done

ss -nlpt | grep 1113 >/dev/null && echo "configuration OK" || echo "configuration ERR"

kill %1
zookeeper-server stop
