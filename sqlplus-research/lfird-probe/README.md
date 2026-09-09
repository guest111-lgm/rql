# `lfird` interposition probe

`liblfird_probe.so` exports the inferred SysV AMD64 ABI:

```c
long lfird(void *lfi_context, void *input_object,
           void *buffer, long capacity);
```

Default behavior is pass-through using `dlsym(RTLD_NEXT, "lfird")`. Set
`SQLPLUS_LFIRD_TRACE=1` to log the four arguments and return value without
dereferencing Oracle-owned pointers. Set `SQLPLUS_LFIRD_REPLACE=1` to use the
local line editor, but only when the return address is exactly the normal line
read site in the inspected `libsqlplus.so` build (`+0x22210`, immediately after
the call at `+0x2220b`) and both loaded Oracle images carry the recorded Build
IDs. All other `lfird` calls, including the one-byte and history paths, are
delegated to Oracle.

The interposition test uses a tiny local fake provider and does not load or
modify an Oracle binary:

```text
make
SQLPLUS_LFIRD_TRACE=1 LD_PRELOAD=$PWD/liblfird_probe.so ./lfird_interpose_test
```

The replacement path is intentionally build-specific. Before using it with a
complete local Oracle client, re-check the `libsqlplus.so` Build ID and the
call-site disassembly; do not enable it against an unverified client build.
