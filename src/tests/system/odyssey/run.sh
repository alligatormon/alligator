#!/bin/sh
[ -z "$1" ] && APPDIR="/app/src/" || APPDIR="$1"
. /app/src/tests/system/common.sh

DIR="odyssey"
test $APPDIR/external/odyssey
if [ $? -ne 0 ]
then
	wget https://github.com/yandex/odyssey/releases/download/1.1/odyssey.linux-amd64.b7bcb86.tar.gz
	tar xfvz odyssey.linux-amd64.b7bcb86.tar.gz
	mv odyssey $APPDIR/external/odyssey
fi
$APPDIR/external/odyssey /app/src/tests/system/odyssey/odyssey.conf
echo select 1 | psql -h 127.0.0.1 -p 6432 -U postgres postgres
$APPDIR/alligator $APPDIR/tests/system/$DIR/alligator.conf&
sleep 5

TEXT=`curl -s localhost:1111`
METRICS=`cat $APPDIR/tests/system/$DIR/metrics.txt`
for NAME in $METRICS
do
	echo "$TEXT" | grep $NAME >/dev/null 2>&1 && success $NAME || error "$TEXT" $NAME
done

kill %1
pkill odyssey
