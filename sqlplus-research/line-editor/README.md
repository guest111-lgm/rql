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

Build and run the standalone demonstration:

```text
make
./sqlplus-line-editor
```

The PTY regression test is local and does not start Oracle:

```text
make test
```

The editor's output buffer includes the newline expected by the SQL*Plus
reader. The `lfird` hook reuses it, passes a NULL prompt because SQL*Plus has
already displayed its prompt, and only enables replacement at the exact
`libsqlplus.so+0x22210` return site identified in the inspected copy.

The current renderer assumes the edited line fits on one terminal row and is
byte-oriented. UTF-8 display widths, wrapped lines, multiline prompt capture,
and persistent history are deliberate follow-up work.
