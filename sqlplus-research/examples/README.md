# Isolated-client examples

These scripts are templates for a personal test process. They do not modify
Oracle configuration, `oratab`, or the installed client. Run them as the
Oracle software owner against a separately verified client copy.

Before use:

1. set `ORACLE_HOME` to the client you intend to test;
2. build `../lfird-probe` with a compatible compiler and libc;
3. verify the Build IDs and call site expected by `lfird_probe.c`; and
4. use a private, permission-restricted directory for the completion cache.

The wrapper is process-scoped: it sets `LD_PRELOAD`, `LD_LIBRARY_PATH`, and the
completion-file path only for the SQL*Plus child. It does not copy over, patch,
or write any file under `ORACLE_HOME`. `SQLPLUS_LFIRD_REPLACE` defaults to `0`;
set it to `1` only after the client Build IDs and guarded call site have been
rechecked. Do not export `LD_PRELOAD` in a shared login profile.

Start the client with:

```text
ORACLE_HOME=/path/to/oracle/home ./sqlplus-test.sh /nolog
```

The recommended rollout is therefore:

1. build the hook and line editor in this research checkout;
2. refresh a private cache as the same database user and current schema;
3. run `sqlplus-test.sh` with replacement disabled and inspect the trace; and
4. enable replacement for that one process with
   `SQLPLUS_LFIRD_REPLACE=1` only after the guard matches.

Refresh visible object names from another terminal with a connection string
appropriate for the same user/session:

```text
ORACLE_HOME=/path/to/oracle/home ./refresh-objects.sh '/ as sysdba'
```

The refresh query is read-only, but its result is metadata from the connected
database. It records the current schema, visible objects, visible synonyms,
and visible non-hidden columns in a structured cache. Treat the cache as
sensitive and do not commit it to a public repository.
