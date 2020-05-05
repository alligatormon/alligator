nc -lp 1212 < tests/system/http_chunk
curl -s localhost:1111 | grep 'consul_runtime_total_gc_pause_ns 70526850000'
