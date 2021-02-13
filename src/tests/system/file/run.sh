#!/bin/sh
[ -z "$1" ] && APPDIR="/app/src/" || APPDIR="$1"
. /app/src/tests/system/common.sh
DIR="file"
rm -rf /var/lib/alligator/file_stat

$APPDIR/alligator $APPDIR/tests/system/$DIR/alligator.yaml&
sleep 5

TEXT=`curl -s localhost:1111`
METRICS=`cat $APPDIR/tests/system/$DIR/metrics.txt`
for NAME in $METRICS
do
	echo "$TEXT" | grep $NAME >/dev/null 2>&1 && success $NAME || error "$TEXT" $NAME
done

echo 'test_metric 42' >> /tmp/metrics-state.txt
sleep 11
curl -s localhost:1111 | grep 'test_metric 42' && success "step 1 file state" || error  "step 1 file state" "step 1 file state"

kill %1

$APPDIR/alligator $APPDIR/tests/system/$DIR/alligator.yaml&
sleep 1
echo 'test_metric2 24' >> /tmp/metrics-state.txt

curl -s localhost:1111 | grep 'test_metric 42' && error "step 2 file state" "step 2 file state" || success "step 2 file state"
curl -s localhost:1111 | grep 'test_metric2 24' && success "step 3 file state" || error  "step 3 file state" "step 3 file state"
sleep 10

kill %2
