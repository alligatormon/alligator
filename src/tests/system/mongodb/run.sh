#!/bin/sh
[ -z "$1" ] && APPDIR="/app/src/" || APPDIR="$1"
$APPDIR/alligator $APPDIR/tests/system/mongodb/alligator.conf&

mongod --dbpath /mongo --fork --syslog
$APPDIR/tests/system/mongodb/inserts.mongo

curl localhost:1111
kill %1
