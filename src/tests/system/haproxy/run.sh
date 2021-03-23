#!/bin/sh
IFS=""
[ -z "$1" ] && APPDIR="/app/src/" || APPDIR="$1"
haproxy -f $APPDIR/tests/system/haproxy/haproxy.cfg

#tcp conf test
$APPDIR/bin/alligator $APPDIR/tests/system/haproxy/alligator.conf&
sleep 5

TEXT=`curl -s localhost:1111`
echo $TEXT | grep -c 'haproxy_stat {type="weight", name="app", svname="app1"}' >/dev/null && echo "haproxy stat metrics ok" || echo "haproxy stat metrics not found!"
echo $TEXT | grep -c 'Haproxy_ConnRate' >/dev/null && echo "haproxy info metrics ok" || echo "haproxy info metrics not found!"
echo $TEXT | grep -c 'haproxy_sess_count' >/dev/null && echo "haproxy session metrics ok" || echo "haproxy session metrics not found!"
echo $TEXT | grep -c 'haproxy_pool_allocated' >/dev/null && echo "haproxy pool metrics ok" || echo "haproxy pool metrics not found!"
killall alligator


# http conf test
$APPDIR/bin/alligator $APPDIR/tests/system/haproxy/alligator-http.conf&
sleep 5

TEXT=`curl -s localhost:1111`
echo $TEXT | grep -c 'haproxy_stat {type="weight", name="app", svname="app1"}' >/dev/null && echo "haproxy stat metrics ok" || echo "haproxy stat metrics not found!"
killall alligator

kill `cat /var/run/haproxy.pid`
