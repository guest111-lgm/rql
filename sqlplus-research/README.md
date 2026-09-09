# Oracle 19c SQL*Plus line-editing research

This directory contains an isolated proof of concept for adding low-latency
terminal editing to an unmodified Oracle 19c SQL*Plus client. It explores
history, cursor movement, Tab completion, and an `lfird` interposition hook.

The repository contains source code and test fixtures only. Oracle executables,
shared libraries, credentials, database dumps, and environment-specific
deployment files are intentionally not included.

## Current result

- The standalone editor handles arrow keys, history, common editing controls,
  and static SQL/SQL*Plus keyword completion.
- A narrowly guarded `LD_PRELOAD` hook can replace the main line-read call with
  the editor while delegating all other `lfird` calls to Oracle.
- Dynamic object and column completion is implemented as a local cache. A
  separate, read-only SQL*Plus helper can refresh names visible to the
  connected user. The cache carries owner, object, synonym target, column,
  and current-schema records; the editor never queries the database.
- The hook is build-specific: it checks the inspected Oracle image Build IDs
  and the expected `libsqlplus.so` call site before replacing input.

## Try the isolated editor

```text
cd line-editor
make
make test
./sqlplus-line-editor
```

The PTY test does not start Oracle. It checks editing, history, static
completion, dynamic cache completion, and interrupt handling.

## Inspect the hook with a fake provider

```text
cd lfird-probe
make
SQLPLUS_LFIRD_TRACE=1 LD_PRELOAD=$PWD/liblfird_probe.so ./lfird_interpose_test
```

The fake provider is local test code; it does not load or modify an Oracle
binary.

## Use with an isolated client copy

`examples/sqlplus-test.sh` is a parameterized launcher. Run it as the Oracle
software owner, set `ORACLE_HOME`, and point `SQLPLUS_LFIRD_COMPLETION_FILE` at
an optional cache. It sets `LD_PRELOAD` for that process only and does not edit
Oracle configuration. Do not enable replacement until the target client has
been independently checked against the guard in `lfird_probe.c`.

`examples/refresh-objects.sh` refreshes a structured cache using read-only
queries against `ALL_OBJECTS`, `ALL_SYNONYMS`, and `ALL_TAB_COLUMNS`. The cache
is session/privilege dependent and may contain names that should not be
committed to a public repository. The editor accepts the previous plain-word
cache format as well.

## Important limitations

This is reverse-engineering research, not a supported Oracle extension. The
inferred ABI, call site, terminal renderer, and buffer assumptions must be
revalidated for every client patch and platform. The editor is byte-oriented
and assumes a one-row prompt. It now has a line-local SQL context recognizer
and resolves unqualified names through the cache's current schema,
public/private synonyms, and a bounded synonym chain. It deliberately does
not claim full SQL parsing: multiline SQL*Plus buffers, complex CTE/subquery
grammar, quoted identifiers containing punctuation, and remote database-link
columns remain outside the prototype.

See [RESEARCH.md](RESEARCH.md) for the evidence, safety boundaries, and next
steps.
