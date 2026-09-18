**Language / Язык:** [English](testing.md) | [Русский](ru/testing.md)

# Unit tests and coverage

The `src/tests/unit2/` suite is organized by feature headers (for example `netlib.h`, `http.h`, `parsers.h`) and is intended to keep test coverage close to the related production code.

> Note: building tests requires project dependencies (for example `jansson`) and initialized external sources/submodules.

## Running coverage

Coverage flow (scope: `src/**/*.c`, excluding `src/tests/**`, `src/external/**`, and `src/build/**`):

```
cd src
./tests/coverage/run_coverage.sh
```

Artifacts:

- `src/tests/coverage/coverage_report.txt` - full `llvm-cov report` output
- `src/tests/coverage/coverage_top15.txt` - 15 lowest-covered C files in scope
- [coverage-baseline.md](coverage-baseline.md) - baseline snapshot and threshold ramp

To enforce a minimum in local runs:

```
cd src
MIN_LINE_COVERAGE=50 ./tests/coverage/run_coverage.sh
```

# Unit Testing Principles

This document defines the default testing contract for unit tests in `src/tests/unit2`.

## Scope and framework

- New unit tests must be added to `src/tests/unit2`.
- The legacy `src/tests/unit` suite is not a target for new tests.
- Unit tests should be grouped by production subsystem (events/config/parsers/common).

## Determinism and isolation

- Unit tests must not depend on external network services.
- Avoid fixed sleeps in unit tests.
- Prefer in-memory fixtures or local static test fixtures under `src/tests/mock`.
- If a test writes files, use deterministic temporary paths and clean up artifacts.
- Each test should initialize only the state it needs and should free/reset state when done.

## Assertions and behavior

- Validate both success paths and failure paths.
- Assert semantic behavior (return values, flags, selected fields), not only non-null pointers.
- Include boundary cases (empty input, malformed input, length boundaries).
- For parser-oriented tests, validate parser status and important output payload fragments.

## Coverage policy

- Coverage is measured with `llvm-cov` via `src/tests/coverage/run_coverage.sh`.
- Coverage scope is production C files under `src/` and excludes test/external/build paths.
- CI coverage gate must enforce a minimum total line coverage threshold.

## Pull request expectation

- New or modified production code should include or update unit tests in the same change.
- Changes that add parser/config/events behavior should include at least one edge/error-path test.
