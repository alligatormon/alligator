#!/bin/sh
IFS=""
[ -z "$1" ] && APPDIR="/app/src/" || APPDIR="$1"
$APPDIR/alligator $APPDIR/tests/system/shell/alligator.conf&
sleep 4
TEXT=`curl -s localhost:1111`

echo $TEXT | grep 'alligator_process_exit_status {proto="shell", key="/usr/bin/echo 1 | grep -c 1", type="aggregator"} 0' && echo "alligator_process_exit_status OK in grep 1" || echo "shell alligator_process_exit_status metrics not equal in grep 1!"
echo $TEXT | grep 'alligator_process_exit_status {proto="shell", key="/usr/bin/echo 1 | grep -c 2", type="aggregator"} 1' && echo "alligator_process_exit_status OK in grep 2" || echo "shell alligator_process_exit_status metrics not equal in grep 2!"
TIMEREAD=`echo $TEXT | grep 'alligator_read_time' | grep /usr/bin/sleep | awk '{print $6}'`

if [ $TIMEREAD -ge 1000000 ]
then
	echo "test ok"
else
	echo "test not ok"
fi

kill %1
