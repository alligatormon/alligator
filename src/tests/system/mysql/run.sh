#!/bin/sh
[ -z "$1" ] && APPDIR="/app/src/" || APPDIR="$1"
$APPDIR/alligator $APPDIR/tests/system/mysql/alligator.conf&

mysql_install_db --user=mysql --datadir=/mysql/
cd '/usr' ; /usr/bin/mysqld_safe --user=mysql --datadir='/mysql/'&

curl localhost:1111
kill %1
kill %2
