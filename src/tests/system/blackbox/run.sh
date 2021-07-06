#!/bin/sh
[ -z "$1" ] && APPDIR="/app/src/" || APPDIR="$1"
. /app/src/tests/system/common.sh
DIR="blackbox"

$APPDIR/bin/alligator $APPDIR/tests/system/$DIR/alligator.conf&
sleep 1
curl -s 'http://127.0.0.1:1111/probe?target=r0.ru&module=http_23xx'
curl -s 'http://127.0.0.1:1111/probe?target=yandex.ru&module=http_2xx_nfr'
curl -s 'http://127.0.0.1:1111/probe?target=8.8.8.8&module=icmp'
sleep 25


TEXT=`curl -s localhost:1111`
METRICS=`cat $APPDIR/tests/system/$DIR/metrics.txt`
for NAME in $METRICS
do
	echo "$TEXT" | grep $NAME >/dev/null 2>&1 && success $NAME || error "$TEXT" $NAME
done

TEXT=`curl -s 'http://127.0.0.1:1111/probe?target=r0.ru&module=http_23xx'`
METRICS=`cat $APPDIR/tests/system/$DIR/http23xx.txt`
for NAME in $METRICS
do
	echo "$TEXT" | grep $NAME >/dev/null 2>&1 && success $NAME || error "$TEXT" $NAME
done


TEXT=`curl -s 'http://127.0.0.1:1111/probe?target=yandex.ru&module=http_2xx_nfr'`
METRICS=`cat $APPDIR/tests/system/$DIR/http_2xx_nfr.txt`
for NAME in $METRICS
do
	echo "$TEXT" | grep $NAME >/dev/null 2>&1 && success $NAME || error "$TEXT" $NAME
done

TEXT=`curl -s 'http://127.0.0.1:1111/probe?target=8.8.8.8&module=icmp'`
METRICS=`cat $APPDIR/tests/system/$DIR/icmp.txt`
for NAME in $METRICS
do
	echo "$TEXT" | grep $NAME >/dev/null 2>&1 && success $NAME || error "$TEXT" $NAME
done

pkill alligator
