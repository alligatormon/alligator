#!/bin/sh
printf "#!/bin/sh\ncat tests/mock/linux/iptables\n" > /usr/sbin/iptables
chmod +x /usr/sbin/iptables

[ -z "$1" ] && APPDIR="/app/src/" || APPDIR="$1"
. /app/src/tests/system/common.sh

$APPDIR/bin/alligator $APPDIR/tests/mock/linux/alligator.conf&
sleep 15

TEXT=`curl -s localhost:1111`
METRICS=`cat $APPDIR/tests/mock/linux/metrics.txt`
for NAME in $METRICS
do
	echo "$TEXT" | grep $NAME >/dev/null 2>&1 && success $NAME || error "$TEXT" $NAME
done < $APPDIR/tests/mock/linux/metrics.txt

kill %1
