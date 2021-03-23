#!/bin/sh
[ -z "$1" ] && APPDIR="/app/src/" || APPDIR="$1"
. /app/src/tests/system/common.sh

$APPDIR/bin/alligator $APPDIR/tests/system/elasticsearch/alligator.yaml&

sudo -u elasticsearch /usr/share/elasticsearch/bin/systemd-entrypoint >/dev/null&

for ((i=0; i<100; i++))
{
	echo test localhost:9200
	curl -sfo /dev/null localhost:9200
	if [ $? -eq 0 ]
	then
		break
	fi
	sleep 1
}
sleep 10

TEXT=`curl -s localhost:1111`
METRICS=`cat $APPDIR/tests/system/elasticsearch/metrics.txt`
for NAME in $METRICS
do
	#echo "NAME is $NAME"
	echo "$TEXT" | grep $NAME >/dev/null 2>&1 && success $NAME || error $TEXT $NAME
done

JAVAPID=`pgrep java`
kill -1 $JAVAPID
kill %1
