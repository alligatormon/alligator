#!/bin/sh
[ -z "$1" ] && APPDIR="/app/src/" || APPDIR="$1"
. /app/src/tests/system/common.sh

DIR="mongodb"
mongod --fork --syslog
#$APPDIR/tests/system/mongodb/inserts.mongo

$APPDIR/bin/alligator $APPDIR/tests/system/$DIR/alligator.conf&
sleep 15

TEXT=`curl -s localhost:1111`
METRICS=`cat $APPDIR/tests/system/$DIR/metrics.txt`
for NAME in $METRICS
do
	echo "$TEXT" | grep $NAME >/dev/null 2>&1 && success $NAME || error "$TEXT" $NAME
done

curl localhost:1111
kill %1
pkill mongod
