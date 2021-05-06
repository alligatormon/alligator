#!/bin/sh
[ -z "$1" ] && APPDIR="/app/src/" || APPDIR="$1"
. /app/src/tests/system/common.sh
DIR="memcached"

# TLS configuration
memcached -u nobody -Z -o ssl_ca_cert=/app/src/tests/certs/ca.crt -o ssl_chain_cert=/app/src/tests/certs/server.crt -o ssl_key=/app/src/tests/certs/server.key -o ssl_verify_mode=2 -d
$APPDIR/bin/alligator $APPDIR/tests/system/$DIR/alligator-tls.yaml&
sleep 15

TEXT=`curl -s localhost:1111`
METRICS=`cat $APPDIR/tests/system/$DIR/metrics.txt`
for NAME in $METRICS
do
	echo "$TEXT" | grep $NAME >/dev/null 2>&1 && success $NAME || error "$TEXT" $NAME
done
METRICS=`cat $APPDIR/tests/system/$DIR/metrics-tls.txt`
for NAME in $METRICS
do
	echo "$TEXT" | grep $NAME >/dev/null 2>&1 && success $NAME || error "$TEXT" $NAME
done

pkill alligator
pkill memcached

# TCP configuration json
memcached -u nobody -d
$APPDIR/bin/alligator $APPDIR/tests/system/$DIR/alligator.json&
sleep 15

TEXT=`curl -s localhost:1111`
METRICS=`cat $APPDIR/tests/system/$DIR/metrics.txt`
for NAME in $METRICS
do
	echo "$TEXT" | grep $NAME >/dev/null 2>&1 && success $NAME || error "$TEXT" $NAME
done

pkill alligator
pkill memcached

# TCP configuration conf
memcached -u nobody -d
printf 'set xyzkez 0 0 4\r\n442f\r\n' | nc localhost 11211
printf 'set test_metric 0 0 5\r\n424.4\r\n' | nc localhost 11211
printf 'set third_metric 0 0 6\r\n300.91\r\n' | nc localhost 11211
$APPDIR/bin/alligator $APPDIR/tests/system/$DIR/alligator.conf&
sleep 25

TEXT=`curl -s localhost:1111`
METRICS=`cat $APPDIR/tests/system/$DIR/metrics-additional.txt`
for NAME in $METRICS
do
	echo "$TEXT" | grep $NAME >/dev/null 2>&1 && success $NAME || error "$TEXT" $NAME
done

pkill alligator
pkill memcached
