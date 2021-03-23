#!/bin/sh
[ -z "$1" ] && APPDIR="/app/src/" || APPDIR="$1"
. /app/src/tests/system/common.sh
mkdir -p /opt/aerospike/sys/udf/lua
mkdir -p /opt/aerospike/usr/udf/lua
mkdir -p /opt/aerospike/smd

asd --config-file tests/system/aerospike/aerospike.conf

$APPDIR/bin/alligator $APPDIR/tests/system/aerospike/alligator.yaml&
sleep 25

TEXT=`curl -s localhost:1111`
METRICS=`cat $APPDIR/tests/system/aerospike/metrics.txt`
for NAME in $METRICS
do
	echo "$TEXT" | grep $NAME >/dev/null 2>&1 && success $NAME || error "$TEXT" $NAME
done

pkill alligator
pkill aerospike
