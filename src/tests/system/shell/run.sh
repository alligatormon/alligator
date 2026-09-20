#!/bin/sh
IFS=""
[ -z "$1" ] && APPDIR="/app/src/" || APPDIR="$1"
$APPDIR/bin/alligator $APPDIR/tests/system/shell/alligator.conf&
sleep 4
TEXT=`curl -s localhost:1111`

echo $TEXT | grep 'alligator_process_exit_status' | grep '/usr/bin/echo 1 | grep -c 1' | grep '} 0' && echo "alligator_process_exit_status OK in grep 1" || echo "shell alligator_process_exit_status metrics not equal in grep 1!"
echo $TEXT | grep 'alligator_process_exit_status' | grep '/usr/bin/echo 1 | grep -c 2' | grep '} 1' && echo "alligator_process_exit_status OK in grep 2" || echo "shell alligator_process_exit_status metrics not equal in grep 2!"
TIMEREAD=`echo $TEXT | grep 'alligator_session_duration_seconds' | grep /usr/bin/sleep | awk '{print $NF}'`

if awk -v t="$TIMEREAD" 'BEGIN { exit !(t+0 >= 1) }'
then
	echo "test ok"
else
	echo "test not ok"
fi

kill %1
