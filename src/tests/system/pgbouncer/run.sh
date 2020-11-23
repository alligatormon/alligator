#!/bin/sh
[ -z "$1" ] && APPDIR="/app/src/" || APPDIR="$1"

/usr/bin/pgbouncer $APPDIR/tests/system/pgbouncer/pgbouncer.ini
$APPDIR/alligator $APPDIR/tests/system/pgbouncer/alligator.conf&
sleep 15

curl localhost:1111
kill %1
