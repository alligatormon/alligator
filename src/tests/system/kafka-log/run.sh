#!/bin/sh
[ -z "$1" ] && APPDIR="/app/src/" || APPDIR="$1"
. "$APPDIR/tests/system/common.sh"
DIR="kafka-log"
KAFKA_HOME="external/kafka_2.13-2.7.0"
TOPIC="alligator-log-test"

ls external/kafka.tar.gz || wget -O external/kafka.tar.gz 'https://apache-mirror.rbc.ru/pub/apache/kafka/2.7.0/kafka_2.13-2.7.0.tgz'
cd external
ls kafka_2.13-2.7.0 || tar xfvz kafka.tar.gz
cd kafka_2.13-2.7.0
sh ./bin/zookeeper-server-start.sh config/zookeeper.properties&
sleep 15
sh bin/kafka-server-start.sh config/server.properties&
cd ../../

sleep 10
sh "$KAFKA_HOME/bin/kafka-topics.sh" --create --if-not-exists --bootstrap-server 127.0.0.1:9092 --replication-factor 1 --partitions 1 --topic "$TOPIC"

$APPDIR/bin/alligator "$APPDIR/tests/system/$DIR/alligator.conf"&
sleep 20

text=`sh "$KAFKA_HOME/bin/kafka-console-consumer.sh" --bootstrap-server 127.0.0.1:9092 --topic "$TOPIC" --from-beginning --max-messages 1 --timeout-ms 15000 2>/dev/null`
echo "$text" | grep '"message"' >/dev/null 2>&1 && success "kafka log message" || error "$text" "kafka log message"

sh "$KAFKA_HOME/bin/zookeeper-server-stop.sh"
sh "$KAFKA_HOME/bin/kafka-server-stop.sh"
kill %1
kill %2
kill %3
killall -9 java 2>/dev/null
