# Isolated line-editor prototype

This directory contains a small dependency-free terminal editor intended to
sit outside Oracle's binaries. It uses `termios` raw mode and one-byte reads,
so cursor keys are handled locally instead of being sent as escape text to
SQL*Plus. It includes:

- Up/down in-memory history (100 entries).
- Left/right/Home/End, Backspace/Delete, Ctrl-A/Ctrl-E, Ctrl-W, Ctrl-U, and
  Ctrl-K.
- Static SQL/SQL*Plus keyword completion. A unique match is inserted; an
  ambiguous match is listed when the editor owns the prompt and otherwise
  rings the bell.
- Structured, read-only metadata completion from
  `SQLPLUS_LFIRD_COMPLETION_FILE`: tables, views, materialized views,
  sequences, synonyms, `OWNER.TABLE`, and columns through aliases or table
  qualifiers.
- A small SQL-context recognizer for `FROM`/`JOIN`/`UPDATE`/`INSERT INTO`,
  `SELECT`, `WHERE`, `ON`, `GROUP BY`, `ORDER BY`, `SET`, and `RETURNING`.

Build and run the standalone demonstration:

```text
make
./sqlplus-line-editor
```

The PTY regression test is local and does not start Oracle:

```text
make test
```

## Metadata cache

The editor never opens a database connection. An external, read-only SQL*Plus
refresh creates an atomically replaced cache and the editor reloads it on the
next Tab press. The cache accepts the original one-word-per-line format for
backward compatibility and also accepts these tab-separated records:

```text
META    CURRENT_SCHEMA  APP
OBJECT  APP             CLIENTS       TABLE
SYNONYM APP             CLIENT_ALIAS  APP            CLIENTS
COLUMN  APP             CLIENTS       CLIENT_ID      1
```

Fields are separated by a literal tab. Backslash escapes `\\t`, `\\n`,
`\\r`, `\\\\`, and `\\xHH` are accepted for generated caches. The supplied
refresh example currently filters to ordinary unquoted Oracle identifiers so
the cache remains safe for the byte-oriented editor.

The metadata is privilege- and session-dependent. A stale or incomplete
cache can only affect suggestions; it does not change SQL execution. A
snapshot generated for one database user or current schema must not be reused
as though it represented another session.

The editor's output buffer includes the newline expected by the SQL*Plus
reader. The `lfird` hook reuses it, passes a NULL prompt because SQL*Plus has
already displayed its prompt, and only enables replacement at the exact
`libsqlplus.so+0x22210` return site identified in the inspected copy.

The current renderer assumes the edited line fits on one terminal row and is
byte-oriented. UTF-8 display widths, wrapped lines, multiline prompt capture,
and persistent history are deliberate follow-up work. Context recognition is
line-local: it does not attempt to reconstruct SQL*Plus's multi-line buffer,
full CTE/subquery grammar, quoted identifiers with embedded punctuation, or
remote database-link columns.
