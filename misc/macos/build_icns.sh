#!/bin/sh
# Build Alligator.icns for CPack Bundle (macOS only). See:
# https://cmake.org/cmake/help/latest/cpack_gen/bundle.html
set -eu
ROOT="$(CDPATH= cd -- "$(dirname "$0")" && pwd)"
ICNS="${ROOT}/Alligator.icns"
MASTER="${ROOT}/icon-master.png"

if [ "$(uname -s)" != "Darwin" ]; then
	echo "build_icns.sh: macOS only" >&2
	exit 1
fi

python3 - "${MASTER}" <<'PY'
import struct
import sys
import zlib

def chunk(tag, data):
    return struct.pack(">I", len(data)) + tag + data + struct.pack(">I", zlib.crc32(tag + data) & 0xFFFFFFFF)

w, h = 512, 512
raw = bytearray()
for y in range(h):
    raw.append(0)
    for x in range(w):
        raw.extend((40, 90, 160, 255))
png = b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 6, 0, 0, 0)) + chunk(b"IDAT", zlib.compress(bytes(raw), 9)) + chunk(b"IEND", b"")
with open(sys.argv[1], "wb") as f:
    f.write(png)
PY

sips -s format icns "${MASTER}" --out "${ICNS}" >/dev/null
echo "Wrote ${ICNS}"
