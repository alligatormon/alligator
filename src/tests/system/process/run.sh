#!/bin/sh
./bin/alligator tests/system/process/alligator.conf >/dev/null &
. /app/src/tests/system/common.sh
sleep 5
curl -s localhost:1111 | grep root && success "root ok" || error "root error" ""
curl -s localhost:1111 | grep bash | grep 1 >/dev/null && success "process match bash success" ||  error "bash error" ""
curl -s localhost:1111 | grep tail | grep 1 >/dev/null && success "process match tail success" ||  error "tail error" ""
kill %1
