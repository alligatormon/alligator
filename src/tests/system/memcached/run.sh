#!/bin/sh
[ -z "$1" ] && APPDIR="/app/src/" || APPDIR="$1"
. /app/src/tests/system/common.sh
DIR="memcached"

# TLS configuration
memcached -u nobody -Z -o ssl_ca_cert=/app/src/tests/certs/ca.crt -o ssl_chain_cert=/app/src/tests/certs/server.crt -o ssl_key=/app/src/tests/certs/server.key -o ssl_verify_mode=2 -d
$APPDIR/alligator $APPDIR/tests/system/$DIR/alligator-tls.yaml&
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

# TCP configuration
memcached -u nobody -d
$APPDIR/alligator $APPDIR/tests/system/$DIR/alligator.yaml&
sleep 15

TEXT=`curl -s localhost:1111`
METRICS=`cat $APPDIR/tests/system/$DIR/metrics.txt`
for NAME in $METRICS
do
	echo "$TEXT" | grep $NAME >/dev/null 2>&1 && success $NAME || error "$TEXT" $NAME
done

pkill alligator
pkill memcached
