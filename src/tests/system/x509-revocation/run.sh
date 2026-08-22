#!/bin/sh
[ -z "$1" ] && APPDIR="/app/src/" || APPDIR="$1"
. /app/src/tests/system/common.sh
DIR="x509-revocation"
WORKDIR="${TMPDIR:-/tmp}/alligator-x509-revocation-$$"
mkdir -p "$WORKDIR/certs"
CONF="$WORKDIR/alligator.conf"

# Minimal CA + leaf + revoke + CRL for the filesystem checker.
openssl req -x509 -newkey rsa:2048 -nodes -keyout "$WORKDIR/ca.key" \
	-out "$WORKDIR/certs/ca.crt" -days 2 -subj "/CN=alligator-rev-ca" >/dev/null 2>&1 || {
	echo "openssl not available, skip $DIR"
	exit 0
}
openssl req -newkey rsa:2048 -nodes -keyout "$WORKDIR/leaf.key" \
	-out "$WORKDIR/leaf.csr" -subj "/CN=revoked.alligator.local" >/dev/null 2>&1
touch "$WORKDIR/index.txt"
echo 1000 > "$WORKDIR/serial"
cat > "$WORKDIR/ca.cnf" <<EOF
[ca]
default_ca = CA_default
[CA_default]
dir = $WORKDIR
database = $WORKDIR/index.txt
serial = $WORKDIR/serial
new_certs_dir = $WORKDIR
certificate = $WORKDIR/certs/ca.crt
private_key = $WORKDIR/ca.key
default_md = sha256
policy = policy_any
[policy_any]
commonName = supplied
EOF
openssl ca -batch -config "$WORKDIR/ca.cnf" -in "$WORKDIR/leaf.csr" \
	-out "$WORKDIR/certs/leaf.crt" -days 1 -notext >/dev/null 2>&1
openssl ca -batch -config "$WORKDIR/ca.cnf" -revoke "$WORKDIR/certs/leaf.crt" >/dev/null 2>&1
openssl ca -batch -config "$WORKDIR/ca.cnf" -gencrl -out "$WORKDIR/ca.crl" >/dev/null 2>&1

cat > "$CONF" <<EOF
log_level 0;
entrypoint { tcp 1111; }
x509 {
  name rev-certs;
  path $WORKDIR/certs;
  match .crt;
  ca_file $WORKDIR/certs/ca.crt;
  crl_file $WORKDIR/ca.crl;
  period 1s;
}
EOF

$APPDIR/bin/alligator "$CONF" &
sleep 8
TEXT=`curl -s localhost:1111`
echo "$TEXT" | grep 'x509_cert_valid' | grep 'reason="revoked"' >/dev/null 2>&1 \
	&& success "x509_cert_valid revoked" || error "$TEXT" "x509_cert_valid revoked"
echo "$TEXT" | grep 'x509_cert_revocation_status' >/dev/null 2>&1 \
	&& success "x509_cert_revocation_status" || error "$TEXT" "x509_cert_revocation_status"
kill %1
rm -rf "$WORKDIR"
