#!/bin/sh
[ -z "$1" ] && APPDIR="/app/src/" || APPDIR="$1"
. /app/src/tests/system/common.sh
DIR="action"

$APPDIR/bin/alligator $APPDIR/tests/system/$DIR/alligator.conf&
sleep 10

cat /tmp/test | jq '.[0].samples[0].value' && success ACTION OK || error ACTION ERR

pkill alligator
