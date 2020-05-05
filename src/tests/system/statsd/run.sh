#!/bin/sh
[ -z "$1" ] && APPDIR="/app/src/" || APPDIR="$1"
$APPDIR/alligator $APPDIR/tests/system/statsd/alligator.conf&
usleep 1000000
$APPDIR/tests/system/statsd/test.sh localhost 1111 1111 http 8125

curl localhost:1111
kill %1
