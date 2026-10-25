#!/usr/bin/env python3
"""Generate delimited io.prometheus.client.MetricFamily fixtures (pure stdlib).

Usage: gen_fixture.py [OUTDIR]   (default: directory of this script)
Writes:
  fixture.pb      valid: gauge pgpb_sys_gauge{env="sys"} 42 + histogram pgpb_sys_hist
  fixture-bad.pb  malformed framing: length prefix larger than the remaining bytes
"""
import os
import struct
import sys

COUNTER, GAUGE, SUMMARY, UNTYPED, HISTOGRAM = 0, 1, 2, 3, 4


def varint(n):
    out = bytearray()
    while True:
        b = n & 0x7F
        n >>= 7
        if n:
            out.append(b | 0x80)
        else:
            out.append(b)
            return bytes(out)


def tag(field, wire):
    return varint((field << 3) | wire)


def f_varint(field, v):
    return tag(field, 0) + varint(v)


def f_double(field, v):
    return tag(field, 1) + struct.pack("<d", v)


def f_bytes(field, b):
    if isinstance(b, str):
        b = b.encode()
    return tag(field, 2) + varint(len(b)) + b


def label(name, value):
    return f_bytes(1, f_bytes(1, name) + f_bytes(2, value))


def gauge_metric(labels, value):
    m = b"".join(label(k, v) for k, v in labels)
    return m + f_bytes(2, f_double(1, value))


def histogram_metric(labels, count, total, buckets):
    m = b"".join(label(k, v) for k, v in labels)
    h = f_varint(1, count) + f_double(2, total)
    for cum, ub in buckets:
        h += f_bytes(3, f_varint(1, cum) + f_double(2, ub))
    return m + f_bytes(7, h)


def family(name, help_, mtype, metrics):
    body = f_bytes(1, name) + f_bytes(2, help_) + f_varint(3, mtype)
    for m in metrics:
        body += f_bytes(4, m)
    return body


def delimited(msgs):
    return b"".join(varint(len(m)) + m for m in msgs)


def main():
    here = sys.argv[1] if len(sys.argv) > 1 else os.path.dirname(os.path.abspath(__file__))
    gauge = family("pgpb_sys_gauge", "system test gauge", GAUGE,
                   [gauge_metric([("env", "sys")], 42.0)])
    hist = family("pgpb_sys_hist", "system test histogram", HISTOGRAM,
                  [histogram_metric([("env", "sys")], 4, 3.5,
                                    [(1, 0.5), (3, 1.0), (4, float("inf"))])])
    good = delimited([gauge, hist])
    with open(os.path.join(here, "fixture.pb"), "wb") as f:
        f.write(good)

    # Malformed framing: declared length exceeds what is actually present.
    bad = varint(len(gauge) + 50) + gauge
    with open(os.path.join(here, "fixture-bad.pb"), "wb") as f:
        f.write(bad)


if __name__ == "__main__":
    main()
