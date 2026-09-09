# Findings and experimental boundaries

## Scope

The goal was to determine whether an Oracle 19c SQL*Plus session can gain
responsive Up/Down history, cursor editing, and completion without changing
the Oracle installation or replacing the original client files. The work was
performed against isolated copies and local test doubles.

## What the experiments show

### Input ownership

The small `sqlplus` executable is primarily a launcher. The interactive input
path is in the SQL*Plus and Oracle client shared libraries. When no line editor
owns the terminal, an escape sequence from an arrow key reaches SQL*Plus as
raw bytes; it is not interpreted as cursor movement.

### `lfird` call shape

On the inspected 64-bit SysV ABI build, the exported symbol was successfully
interposed with the following working declaration:

```c
long lfird(void *lfi_context, void *input_object,
           void *buffer, long capacity);
```

The fourth argument behaves like a capacity or read-size value in the observed
calls. Different input paths use different values, so it must not be treated
as a universal line length. The hook records arguments and return values
without dereferencing Oracle-owned pointers by default.

This declaration is an experimental inference, not an Oracle API contract.

### Safe interception boundary

The hook defaults to pass-through through `dlsym(RTLD_NEXT, "lfird")`. The
replacement path is enabled only when all of the following are true:

1. replacement is explicitly enabled with an environment variable;
2. the loaded Oracle images match the recorded Build IDs; and
3. the return address is the inspected main line-read call site in
   `libsqlplus.so`.

The call-site check prevents the editor from taking over one-byte reads,
history internals, or unrelated uses of `lfird`. A different Oracle patch,
architecture, compiler layout, or loader arrangement requires a fresh
disassembly and a new guard decision.

### Line editor prototype

The editor uses `termios` raw mode and one-byte reads. It currently provides:

- Up/Down in-memory history;
- Left/Right, Home/End, Backspace/Delete;
- Ctrl-A, Ctrl-E, Ctrl-W, Ctrl-U, and Ctrl-K;
- unique-match insertion for static keywords; and
- ambiguous-match display when the editor owns the prompt.

The renderer is deliberately small. It assumes the edited line fits on one
terminal row and is byte-oriented; UTF-8 display widths, wrapped lines,
multiline prompts, and persistent history are follow-up work.

### Dynamic completion

The editor can merge static candidates with one candidate per line from a
local cache named by `SQLPLUS_LFIRD_COMPLETION_FILE`. It fingerprints the file
and reloads it when the file changes, so a running SQL*Plus process can see a
new cache on the next Tab without restarting.

The supplied refresh example performs read-only metadata queries for names in
`ALL_OBJECTS` and `ALL_SYNONYMS`. A separate `ALL_TAB_COLUMNS` query is shown
for column completion. These views expose metadata according to the session's
visibility and privileges; a cache refreshed under another account may give
surprising or incomplete results. The cache is intentionally external to the
Oracle installation.

## Verification matrix

- A fake `lfird` provider verifies symbol interposition and pass-through.
- A local PTY regression test exercises editing, history, completion, and
  Ctrl-C without starting Oracle.
- An isolated Oracle client copy was used to verify prompt-visible operation,
  guarded replacement, history replay, and static completion.
- A cache-only test verified dynamic completion without querying a database.
- The original Oracle binaries were kept read-only throughout the experiment.

The last two items are intentionally separate: validating the editor does not
require access to a live database, and refreshing names should be an explicit
operator action.

## Recommended next steps

1. Re-run the Build ID and call-site checks after every Oracle client patch.
2. Add a configuration file or generated manifest instead of hard-coding one
   client build into a production launcher.
3. Make terminal rendering width-aware and robust for long or multibyte lines.
4. Add SQL-aware context parsing for `OWNER.TABLE`, aliases, quoted names,
   and column completion.
5. Define cache ownership, permissions, refresh cadence, and stale-data
   behavior before using dynamic completion in a shared environment.
6. Test under the exact libc, compiler, terminal multiplexer, and Oracle
   client combinations that users will run.

## Safety and licensing notes

This directory contains experimental code and no Oracle software. Use only
with a properly licensed client and an isolated process-level environment.
There is no claim of compatibility or support from Oracle. Review the license
and redistribution terms of every dependency and client component before
sharing or deploying the result.

