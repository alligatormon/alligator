#!/bin/sh
IFS=""
[ -z "$1" ] && APPDIR="/app/src/" || APPDIR="$1"
$APPDIR/alligator $APPDIR/tests/system/redis/alligator.conf&

redis-server redis1.conf >/dev/null
redis-server redis2.conf >/dev/null
redis-server redis3.conf >/dev/null

yes | redis-cli --cluster create 127.0.0.1:6379 127.0.0.1:6380 127.0.0.1:6381 --cluster-replicas 0 >/dev/null
echo
echo

sleep 5
TEXT=`curl -s localhost:1111`
echo $TEXT | grep "cluster" 2>&1 >/dev/null && echo "redis cluster test ok" || echo "redis cluster test fail"
echo $TEXT | grep "redis_used_memory" 2>&1 >/dev/null && echo "redis info test ok" || echo "redis info test fail"


kill `cat /var/run/redis_6379.pid`
kill `cat /var/run/redis_6380.pid`
kill `cat /var/run/redis_6381.pid`
rm -rf nodes-63*

kill %1
