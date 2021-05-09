#!/bin/sh
#IFS=""
[ -z "$1" ] && APPDIR="/app/src/" || APPDIR="$1"
. /app/src/tests/system/common.sh
DIR="redis"
cd $APPDIR/tests/system/redis/

$APPDIR/bin/alligator $APPDIR/tests/system/redis/alligator.conf&

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
sleep 3

# check queries
redis-server redis.conf
# ( for i in `seq 0 100`; do printf "set `pwgen -0 -N 1` $RANDOM\r\n"; done ) > generated
cat $APPDIR/tests/system/$DIR/generate.txt | nc localhost 6379
printf 'set test_metric1 test_metric_statsd:12\r\n' | nc localhost 6379
$APPDIR/bin/alligator $APPDIR/tests/system/redis/alligator.conf&
sleep 25

TEXT=`curl -s localhost:1111`
METRICS=`cat $APPDIR/tests/system/$DIR/metrics-generated.txt`
for NAME in $METRICS
do
	echo "$TEXT" | grep $NAME >/dev/null 2>&1 && success $NAME || error "$TEXT" $NAME
done

kill %2
pkill redis-server
cd -
