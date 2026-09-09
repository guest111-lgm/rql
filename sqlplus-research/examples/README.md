# Isolated-client examples

These scripts are templates for a personal test process. They do not modify
Oracle configuration, `oratab`, or the installed client. Run them as the
Oracle software owner against a separately verified client copy.

Before use:

1. set `ORACLE_HOME` to the client you intend to test;
2. build `../lfird-probe` with a compatible compiler and libc;
3. verify the Build IDs and call site expected by `lfird_probe.c`; and
4. use a private, permission-restricted directory for the completion cache.

Start the client with:

```text
ORACLE_HOME=/path/to/oracle/home ./sqlplus-test.sh /nolog
```

Refresh visible object names from another terminal with a connection string
appropriate for the same user/session:

```text
ORACLE_HOME=/path/to/oracle/home ./refresh-objects.sh '/ as sysdba'
```

The refresh query is read-only, but its result is metadata from the connected
database. Treat the cache as sensitive and do not commit it to a public
repository.

