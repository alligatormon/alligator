#!/bin/sh
[ -z "$1" ] && APPDIR="/app/src/" || APPDIR="$1"
DIR="mysql"
. /app/src/tests/system/common.sh

#mysql_install_db --user=mysql --datadir=/mysql/
#cd '/usr' ; /usr/bin/mysqld_safe --user=mysql --datadir='/mysql/'&
$APPDIR/alligator $APPDIR/tests/system/mysql/alligator.conf&
sleep 9

TEXT=`curl -s localhost:1111`
METRICS=`cat $APPDIR/tests/system/$DIR/metrics.txt`
for NAME in $METRICS
do
	echo "$TEXT" | grep $NAME >/dev/null 2>&1 && success $NAME || error "$TEXT" $NAME
done

kill %1
kill %2
