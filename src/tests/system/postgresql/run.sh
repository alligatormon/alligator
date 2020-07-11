#!/bin/sh
[ -z "$1" ] && APPDIR="/app/src/" || APPDIR="$1"
$APPDIR/alligator $APPDIR/tests/system/postgresql/alligator.conf&

mkdir /postgres
chown postgres:postgres /postgres
su postgres -c "initdb -D /postgres"
su postgres -c "pg_ctl -D /postgres -l logfile start"

curl localhost:1111
kill %1
