# Coverage Baseline

Generate the report:

```bash
cd src
. .venv-conan/bin/activate
cmake --build build --target alligator_tests --parallel
./build/alligator_tests pass
./tests/coverage/run_coverage.sh
```

Current baseline (local): **31.82% line coverage** (69,967 tests pass; re-run `run_coverage.sh` to confirm).

Scope: `src/**/*.c`, excluding `src/tests/**` and `src/external/**`.

## Bugs fixed while expanding tests

| Area | Issue | Fix |
|------|-------|-----|
| `action/action.c` | Cassandra action called `string_tokens_json(ms)` with `ms == NULL` → segfault | Guard with `ms ? string_tokens_json(ms) : json_array()`; free `json_dumps` output; `json_decref` |
| `common/selector.c` | `string_tokens_json(NULL)` dereferenced `st->l` | Return empty JSON array when `st` is NULL |
| `tests/unit2/tests.c` | Double `query_context_free(mqc)` after `action_run_process` (already frees `mqc`) | Removed redundant free in action tests |
| `tests/unit2/tests.c` | `metric_delete` test used wrong namespace for `metric_add_labels` | Pass `context_arg.namespace` + `labels_hash_insert` |
| Earlier session | `labels.c` groupkey loop, `transform.c` replace_all, `redis.c` bounds | See prior commits in branch |

## CI ramp policy

Staged `MIN_LINE_COVERAGE` bumps in `.gitlab-ci.yml`:

| Stage | Threshold | Notes |
|-------|-----------|-------|
| 1 | 23% | Initial gate |
| 2 | 30% | **Done** (31.82%) |
| 3 | 32% | Next CI bump |
| 4 | 35% | Target — ~3.2% / ~1,800 lines remaining |
| 5 | 40% | Follow-up |
| 6 | 50% | Long-term lock |

Increase only after a stage is stable under `run_coverage.sh` (tests must run **outside** sandbox; profiling can segfault in restricted environments).

## Plan to reach 35%

**Gap to 35%:** ~3.2 percentage points ≈ **~1,800 lines** on ~55,675 total instrumented lines.

### Pass-mode stability notes

Some scenarios segfault or abort under `./build/alligator_tests pass` (stderr redirected) even when verbose mode passes:

- `api_v1` probe/scheduler/cluster pushes via plain→JSON
- Dedicated ClickHouse/PG serializer tests passing `&ms` to `metric_query_deserialize` (matrix tests in `test_serializer_context_matrix` are OK)
- `squid_forward_handler` multi-pool fixtures

Prefer small, isolated `http_api_v1` JSON chunks and plain-config parse-only tests for the next push.

### Phase 1 — Quick wins (+1.5–2%, ~800–1000 lines)

1. **Redis parser** (bugs fixed in `redis_handler` bounds + `redis_query` NULL-key guard)
   - `api_test_parser_redis_info` — INFO sections (Memory, CPU, Commandstats, Keyspace)
   - `redis_query` in dynatrace/redis test
2. **`config/plain.c`** — probe/query/action edge blocks, grok quantiles, aggregate TLS/env variants
3. **`metric/labels.c`** — `labels_gen_metric`, `labels_match`, groupkey via query/gen paths
4. **`metric/transform.c`** — more `update_label` scenarios
5. **`parsers/multicollector.c`** — statsd, pushgateway, exemplar edges (one scenario per test)

### Phase 2 — Parser expansion (+1.5–2%, ~800–1000 lines)

1. **`parsers/squid.c`**, **`parsers/named.c`** — larger realistic fixtures
2. **`parsers/dynatrace.c`** — invalid-line / error JSON branches
3. **`parsers/json.c`** — `json_handler` with `pquery` on `context_arg`
4. Re-enable **`api_test_parser_lighttpd`** if stable under profiling

### Phase 3 — API & serializers (+1–1.5%, ~500–800 lines)

1. **`api/api_v1.c`** — router handlers with mock `context_arg` / JSON bodies (largest single-file yield)
2. **`metric/serializer.c`** — ClickHouse, PG, Cassandra (**one format per test**, no nested matrices)
3. **`metric/metrictree.c`** — delete/find/expire via namespace metrics

### Phase 4 — Harder modules (optional buffer toward 35%)

1. **`events/filetailer.c`** — mock `file_handle`, state foreach without live libuv
2. **`events/client.c`** — expand registry/shutdown paths
3. **`grok/grok.c`** — minimal `grok_push` with pre-seeded `grok_ds`

### Test hygiene rules

- Verify after each batch: `LLVM_PROFILE_FILE=... ./build/alligator_tests pass` then `run_coverage.sh`
- Avoid serializer format×datatype matrices in one test (profiling instability)
- Avoid `tcp_client()` / live network in unit2
- One heavy parser or serializer per test function when possible

## High-yield under-covered files

Prioritize by missed lines × testability:

| File | ~Line % | Missed | Notes |
|------|---------|--------|-------|
| `api/api_v1.c` | ~23% | ~1000+ | Mock HTTP/context |
| `config/plain.c` | ~56–62% | ~750 | Plain + `config_get` runtime |
| `metric/labels.c` | ~30–44% | ~800 | Query/gen paths |
| `metric/serializer.c` | ~47–55% | ~630 | One format per test |
| `events/filetailer.c` | ~14% | ~560 | Needs UV mocks |
| `parsers/squid.c` | ~13% | ~570 | Fixture growth |
| `parsers/ipmi.c` | ~18–26% | ~500 | Existing `api_test_parser_ipmi` |
| `parsers/snmp.c`, `otlp.c` | 0% | 1200+ | Need mock payloads |

## Bugs fixed during coverage push

| Issue | File | Fix |
|-------|------|-----|
| Buffer overread in INFO parser | `parsers/redis.c` `redis_handler` | Bound loops to `remain = end - tmp`, guard `strncmp` lengths, NULL-safe `tmp2` |
| NULL deref when response has extra pairs | `parsers/redis.c` `redis_query` | Break when `redis_keys[j]` is NULL; clear `carg->data` after free |
| `free()` on stack key array in tests | `api_test_parser_redis_query` | `carg->data` must be heap `calloc`'d; `redis_keys_free` frees the array |
| Infinite loop on comma-separated group keys | `metric/labels.c` `labels_to_groupkey`, `labels_to_hash` | Advance `i` by token length and optional comma after each field |
| macOS PCRE JIT | `common/selector.c` | `pcre_exec` instead of `pcre_jit_exec` |
| Profiling segfaults | tests | Split serializer/redis tests; no nested format matrices |
