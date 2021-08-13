#!/bin/sh
[ -z "$1" ] && APPDIR="/app/src/" || APPDIR="$1"
. /app/src/tests/system/common.sh
DIR="configuration"

$APPDIR/bin/alligator $APPDIR/tests/system/$DIR/alligator-full.json&
sleep 4

TEXT=`curl -s localhost:1111/conf`
echo $TEXT | jq --sort-keys . > /tmp/actual.json
cat $APPDIR/tests/system/$DIR/alligator-full.json | jq --sort-keys . > /tmp/expected.json

kill %1
sleep 1

diff /tmp/actual.json /tmp/expected.json | egrep -v -- 'key|---|"tcp":|"1111"|\],|[0-9],[0-9]'
DIFFLEN=`diff /tmp/actual.json /tmp/expected.json | egrep -v -- 'key|---|"tcp":|"1111"|\],|[0-9],[0-9]' | wc -l`
if [ $DIFFLEN -gt 0 ]
then
	error "equal IN CONFIG!" "equal IN CONFIG"
else
	success "configuration getset success"
fi
