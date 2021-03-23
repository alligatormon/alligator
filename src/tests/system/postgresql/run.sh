#!/bin/sh
[ -z "$1" ] && APPDIR="/app/src/" || APPDIR="$1"

su postgres -c 'initdb -D /tmp/pg'
su postgres -c 'pg_ctl -D /tmp/pg -l logfile start'

$APPDIR/bin/alligator $APPDIR/tests/system/postgresql/alligator.conf&
sleep 15

curl localhost:1111
kill %1
su postgres -c 'pg_ctl -D /tmp/pg -l logfile stop'
