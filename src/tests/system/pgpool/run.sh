#!/bin/sh
[ -z "$1" ] && APPDIR="/app/src/" || APPDIR="$1"
. /app/src/tests/system/common.sh

DIR="pgpool"
su postgres -c 'initdb -D /tmp/pg'
su postgres -c 'pg_ctl -D /tmp/pg -l logfile start'
pgpool -f $APPDIR/tests/system/pgpool/pgpool.conf -n &
echo select 1 | psql -h 127.0.0.1 -p 9999 -U postgres postgres
$APPDIR/bin/alligator $APPDIR/tests/system/$DIR/alligator.yaml&
sleep 15

TEXT=`curl -s localhost:1111`
METRICS=`cat $APPDIR/tests/system/$DIR/metrics.txt`
for NAME in $METRICS
do
	echo "$TEXT" | grep $NAME >/dev/null 2>&1 && success $NAME || error "$TEXT" $NAME
done

kill %1
kill %2
su postgres -c 'pg_ctl -D /tmp/pg -l logfile stop'
