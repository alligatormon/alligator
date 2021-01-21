#!/bin/sh
[ -z "$1" ] && APPDIR="/app/src/" || APPDIR="$1"
. /app/src/tests/system/common.sh
DIR="kafka"

ls external/kafka.tar.gz || wget -O external/kafka.tar.gz 'https://apache-mirror.rbc.ru/pub/apache/kafka/2.7.0/kafka_2.13-2.7.0.tgz'
cd external
ls kafka_2.13-2.7.0 || tar xfvz kafka.tar.gz
cd kafka_2.13-2.7.0
sh ./bin/zookeeper-server-start.sh config/zookeeper.properties&
sleep 15
export KAFKA_JMX_OPTS="-Dcom.sun.management.jmxremote=true -Dcom.sun.management.jmxremote.authenticate=false -Dcom.sun.management.jmxremote.ssl=false -Djava.rmi.server.hostname=localhost -Djava.net.preferIPv4Stack=true"
export export JMX_PORT=12345
sh bin/kafka-server-start.sh config/server.properties&
cd ../../

$APPDIR/alligator $APPDIR/tests/system/$DIR/alligator.conf&
sleep 15

text=`curl -s localhost:1111`
metrics=`cat $APPDIR/tests/system/$DIR/metrics.txt`
for name in $metrics
do
	echo "$text" | grep $name >/dev/null 2>&1 && success $name || error "$text" $name
done

sh external/kafka_2.13-2.7.0/bin/zookeeper-server-stop.sh
sh external/kafka_2.13-2.7.0/bin/kafka-server-stop.sh
kill %1
kill %2
kill %3
