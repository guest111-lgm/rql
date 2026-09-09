# Local runtime check

The supplied client-core object has a `libaio.so.1` DT_NEEDED entry, but the
research container does not provide that package. `libaio.so.1` here is an
empty, local test stub only; the inspected core has no unresolved `libaio`
symbols. `load_core` verifies that the supplied
`libclntshcore.so.19.1` loads and that `lfird` resolves. It is not a SQL*Plus
runtime and must not be copied into an Oracle installation.
