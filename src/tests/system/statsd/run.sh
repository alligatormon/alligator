#!/bin/sh
[ -z "$1" ] && APPDIR="/app/src/" || APPDIR="$1"
$APPDIR/alligator $APPDIR/tests/system/statsd/alligator.conf&
usleep 100000
$APPDIR/tests/system/statsd/test.sh localhost 1111 1111 http 8125
$APPDIR/tests/system/statsd/test_expire.sh localhost 1111 1112
python3 $APPDIR/tests/system/statsd/tcpstats.py

curl localhost:1111
kill %1
