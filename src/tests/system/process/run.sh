#!/bin/sh
/bin/alligator tests/system/process/alligator.conf >/dev/null &
sleep 5
curl -s localhost:1111 | grep root && echo success || echo error
kill %1
