# Mtail Module in Alligator

This document describes the new `mtail` module in Alligator.

The module is implemented in `src/amtail` and uses the embedded `amtail` library from [amtail](https://github.com/alligatormon/amtail). It provides a Google mtail-like execution model for turning log lines into metrics directly inside the Alligator C codebase.

## What It Does

- Compiles mtail programs from files (`.mtail` scripts)
- Stores compiled programs as named contexts
- Processes incoming log lines with a selected program
- Maintains per-context parser state between lines
- Exports counters/gauges/histograms as regular Alligator metrics

## Runtime Model

There are two main parts:

1. **Program registry** (`mtail` contexts):
2. **Log ingestion path** (`entrypoint` with `handler mtail`):
   - Input data is split by newline
   - Each complete line is executed in the VM
   - Incomplete line tails are buffered and prepended to the next chunk
   - VM variables are converted to Alligator metrics
   - Full variable export (for metric **TTL** refresh on idle series) runs on the first chunk and then at least every **`mtail_full_export_interval`** seconds; between those, only touched variables are exported (see [configuration](configuration.md#mtail_full_export_interval))

## How Script Selection Works

When handling an input stream:

1. If `entrypoint.mtail` is set, that value is used as a program name.
2. If no named context is found but `entrypoint.mtail` is present, Alligator attempts lazy load using that name and script path.
3. If still not found, it falls back to any available loaded mtail context.
4. If there is no compiled script, parsing fails for that payload.

## Labels

If an mtail variable uses `by ...`, the module builds labels from the variable key.

Current implementation supports:

- constant label values
- variable substitutions (for example, `$httpcode`) resolved from VM variables

This allows mtail scripts to produce dimensional metrics that appear as normal labeled Alligator metrics.

## Thread Safety

Each loaded mtail context has its own mutex.
Execution of one context is serialized by this lock to keep bytecode state and line-tail buffering safe.

## Logging and Debugging

The mtail context supports per-stage log levels:

- `log_level_parser`
- `log_level_lexer`
- `log_level_generator`
- `log_level_compiler`
- `log_level_vm`
- `log_level_pcre`

Additionally, if entrypoint log level is debug, Alligator can dump mtail VM variables during processing.

## Related Documents

- [mtail configuration](https://github.com/alligatormon/alligator/blob/master/doc/mtail/configuration.md) - full configuration reference
- [mtail examples](https://github.com/alligatormon/alligator/blob/master/doc/mtail/examples.md) - practical end-to-end examples
- [Google mtail language reference](https://google.github.io/mtail/Language.html) - syntax and semantics of mtail programs
