#!/bin/sh
[ -z "$1" ] && APPDIR="/app/src/" || APPDIR="$1"
. /app/src/tests/system/common.sh
DIR="druid"

nginx -p /app/src/ -c tests/mock/$DIR/nginx.conf
$APPDIR/bin/alligator $APPDIR/tests/mock/$DIR/alligator.conf&
sleep 10

TEXT=`curl -s localhost:1111 | grep ^druid_`
METRICS=`cat $APPDIR/tests/mock/$DIR/metrics.txt`
for NAME in $METRICS
do
	echo "$TEXT" | grep $NAME >/dev/null 2>&1 && success $NAME || error "$TEXT" $NAME
done < $APPDIR/tests/mock/$DIR/metrics.txt

STRNUM=`curl -s localhost:1111 | wc -l`
if [ $STRNUM -ge 90000 ];
then
	success "number of metrics: $STRNUM"
else
	error "number of metrics: $STRNUM" "number of metrics: $STRNUM"
fi

kill %1
nginx -p /app/src/ -c tests/mock/$DIR/nginx.conf -s stop
