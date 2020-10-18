#!/bin/sh
[ -z "$1" ] && APPDIR="/app/src/" || APPDIR="$1"
. /app/src/tests/system/common.sh
DIR="uwsgi"

uwsgi --plugin python36 --http-socket :9090 --stats :1717 --stats-http --wsgi-file tests/system/uwsgi/hw.py >/dev/null 2>&1 &
uwsgi --plugin python36 --http-socket :9091 --stats :1718 --wsgi-file tests/system/uwsgi/hw.py >/dev/null 2>&1 &
$APPDIR/alligator $APPDIR/tests/system/$DIR/alligator.json&
sleep 15

TEXT=`curl -s localhost:1111`
METRICS=`cat $APPDIR/tests/system/$DIR/metrics.txt`
for NAME in $METRICS
do
	echo "$TEXT" | grep $NAME >/dev/null 2>&1 && success $NAME || error "$TEXT" $NAME
done

pkill -9 uwsgi
pkill alligator
