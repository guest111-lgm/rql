/*
 * The supplied Oracle core declares libaio.so.1 as a dependency but this
 * container does not have that package.  The inspected core has no unresolved
 * libaio symbols, so this empty SONAME-compatible test stub is sufficient to
 * test loading the local core.  It is not an Oracle library and is never used
 * to run SQL*Plus.
 */

int sqlplus_research_libaio_stub_marker(void)
{
    return 0;
}
