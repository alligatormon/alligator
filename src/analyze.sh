#!/bin/sh

PARSE="*/*.c"
[ ! -z "$1" ] && PARSE="${*}"
echo "Parse ${PARSE}"

clang --analyze -Xanalyzer -analyzer-output=text -std=gnu11 -I. -I/usr/lib/jvm/java-14-openjdk-14.0.2.12-1.rolling.el7.x86_64/include/ -I/usr/lib/jvm/java-14-openjdk-14.0.2.12-1.rolling.el7.x86_64/include/linux/ -I/usr/local/include/libbson-1.0 -I/usr/include/zookeeper/ -Iexternal/mbedtls/include/ -I/usr/include/zookeeper/ -I/usr/include/mysql -I/usr/include/libbson-1.0/ ${PARSE}
