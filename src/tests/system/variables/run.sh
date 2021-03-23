#!/bin/sh
[ -z "$1" ] && APPDIR="/app/src/" || APPDIR="$1"
. /app/src/tests/system/common.sh
DIR="variables"

export ALLIGATOR__ENTRYPOINT0__TCP0=1111
export ALLIGATOR__ENTRYPOINT0__TCP1=1112
export ALLIGATOR__TTL=1200
export ALLIGATOR__LOG_LEVEL=0
export ALLIGATOR__AGGREGATE0__HANDLER=tcp
export ALLIGATOR__AGGREGATE0__URL="tcp://google.com:80"
$APPDIR/bin/alligator&
sleep 1
if [ `ss -nlpt | egrep -c '1111|1112'` -eq 2 ]
then
	success "ports 1111, 1112: OK"
else
	error "ports 1111, 1112" FAIL
fi
sleep 5

TEXT=`curl -s localhost:1111`
METRICS=`cat $APPDIR/tests/system/$DIR/metrics.txt`
for NAME in $METRICS
do
	echo "$TEXT" | grep $NAME >/dev/null 2>&1 && success $NAME || error "$TEXT" $NAME
done

kill %1
