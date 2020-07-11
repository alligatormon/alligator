#!/bin/sh
./tests/system/statsd/run.sh
cd solr && sh run.sh && cd -
cd mongodb && ./run.sh && cd -
