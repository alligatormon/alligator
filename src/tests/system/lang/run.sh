#!/bin/sh
[ -z "$1" ] && APPDIR="/app/src/" || APPDIR="$1"
. /app/src/tests/system/common.sh
DIR="lang"

go build -o /tmp/liblang.so -buildmode=c-shared $APPDIR/tests/system/$DIR/main.go

#$APPDIR/bin/alligator $APPDIR/tests/system/$DIR/alligator.conf&
#sleep 10
#
#TEXT=`curl -s localhost:1111`
#METRICS=`cat $APPDIR/tests/system/$DIR/metrics.txt`
#for NAME in $METRICS
#do
#	echo "$TEXT" | grep $NAME >/dev/null 2>&1 && success $NAME || error "$TEXT" $NAME
#done
#
#pkill alligator
