#!/bin/sh
[ -z "$1" ] && APPDIR="/app/src/" || APPDIR="$1"
. "$APPDIR/tests/system/common.sh"
DIR="pushgateway-protobuf"
TESTDIR="$APPDIR/tests/system/$DIR"
TMP=`mktemp -d`
RC=0

CT_PB='application/vnd.google.protobuf; proto=io.prometheus.client.MetricFamily'
URL="localhost:1111/metrics/job/pgpb_sys_job/instance/pgpb_sys_inst"

python3 "$TESTDIR/gen_fixture.py" "$TMP" || { error "fixtures" "generated"; exit 1; }

$APPDIR/bin/alligator "$TESTDIR/alligator.conf"&
APID=$!
sleep 3

# usage: check_status <expected> <label> <content-type> <data-binary file>
check_status()
{
	code=`curl -s -o /dev/null -w '%{http_code}' -X POST -H "Content-Type: $3" --data-binary "@$4" "$URL"`
	if [ "$code" = "$1" ]
	then
		success "$2 -> $code"
	else
		error "HTTP $code (expected $1)" "$2"
		RC=1
	fi
}

: > "$TMP/empty.pb"
check_status 202 "empty body, delimited"          "$CT_PB; encoding=delimited"     "$TMP/empty.pb"
check_status 415 "encoding=text"                  "$CT_PB; encoding=text"          "$TMP/fixture.pb"
check_status 415 "encoding=compact-text"          "$CT_PB; encoding=compact-text"  "$TMP/fixture.pb"
check_status 400 "truncated/malformed framing"    "$CT_PB; encoding=delimited"     "$TMP/fixture-bad.pb"
check_status 202 "valid delimited fixture"        "$CT_PB; encoding=delimited"     "$TMP/fixture.pb"

sleep 1
text=`curl -s localhost:1111`
metrics=`cat "$TESTDIR/metrics.txt"`
for name in $metrics
do
	echo "$text" | grep -F -- "$name" >/dev/null 2>&1 && success "$name" || { error "$text" "$name"; RC=1; }
done

# one series carrying URL labels and body labels together
echo "$text" | grep '^pgpb_sys_gauge{' | grep 'job="pgpb_sys_job"' | grep 'instance="pgpb_sys_inst"' | grep 'env="sys"' | grep -E ' 42(\.0+)?$' >/dev/null 2>&1 \
	&& success "pgpb_sys_gauge{job,instance,env} 42" || { error "$text" "pgpb_sys_gauge{job,instance,env} 42"; RC=1; }

# DELETE the same grouping URL: 202, series gone
code=`curl -s -o /dev/null -w '%{http_code}' -X DELETE "$URL"`
[ "$code" = "202" ] && success "DELETE group -> $code" || { error "HTTP $code (expected 202)" "DELETE group"; RC=1; }
text=`curl -s localhost:1111`
echo "$text" | grep -F 'pgpb_sys_gauge' >/dev/null 2>&1 \
	&& { error "$text" "pgpb_sys_gauge removed after DELETE"; RC=1; } || success "pgpb_sys_gauge removed after DELETE"
echo "$text" | grep -F 'pgpb_sys_job' >/dev/null 2>&1 \
	&& { error "$text" "job series removed after DELETE"; RC=1; } || success "job series removed after DELETE"

# no-op delete, missing job, wrong path
code=`curl -s -o /dev/null -w '%{http_code}' -X DELETE "localhost:1111/metrics/job/pgpb_sys_missing"`
[ "$code" = "202" ] && success "DELETE missing group -> $code" || { error "HTTP $code (expected 202)" "DELETE missing group"; RC=1; }
code=`curl -s -o /dev/null -w '%{http_code}' -X DELETE "localhost:1111/metrics/instance/x"`
[ "$code" = "400" ] && success "DELETE without job -> $code" || { error "HTTP $code (expected 400)" "DELETE without job"; RC=1; }
code=`curl -s -o /dev/null -w '%{http_code}' -X DELETE "localhost:1111/metrics/job/pgpb_sys_job/instance"`
[ "$code" = "400" ] && success "DELETE odd components -> $code" || { error "HTTP $code (expected 400)" "DELETE odd components"; RC=1; }
code=`curl -s -o /dev/null -w '%{http_code}' -X DELETE "localhost:1111/not-metrics"`
[ "$code" = "404" ] && success "DELETE non-metrics path -> $code" || { error "HTTP $code (expected 404)" "DELETE non-metrics path"; RC=1; }

kill $APID 2>/dev/null
wait $APID 2>/dev/null
rm -rf "$TMP"
[ "$RC" = "0" ] && success "PUSHGATEWAY PROTOBUF OK" || error "failures" "PUSHGATEWAY PROTOBUF OK"
exit $RC
