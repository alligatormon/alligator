#!/bin/sh
[ -z "$1" ] && APPDIR="/app/src/" || APPDIR="$1"
. /app/src/tests/system/common.sh
DIR="jks"

#keytool -genkey -keyalg RSA -alias selfsigned -keystore tests/system/jks/keystore.jks -storepass password -validity 360 -keysize 2048 -dname "CN=KK, OU=Development, O=OO, L=LL, S=SS, C=SS"
$APPDIR/alligator $APPDIR/tests/system/$DIR/alligator1.conf&
sleep 5

text=`curl -s localhost:1111`
metrics=`cat $APPDIR/tests/system/$DIR/metrics.txt`
for name in $metrics
do
	echo "$text" | grep $name >/dev/null 2>&1 && success $name || error "$text" $name
done

kill -9 %1

$APPDIR/alligator $APPDIR/tests/system/$DIR/alligator2.conf&
sleep 5

text=`curl -s localhost:1111`
metrics=`cat $APPDIR/tests/system/$DIR/metrics.txt`
for name in $metrics
do
	echo "$text" | grep $name >/dev/null 2>&1 && success $name || error "$text" $name
done

kill -9 %2
