# Source manifest

This publication intentionally contains source code only. It does not include
Oracle client binaries, generated shared objects, executable test artifacts,
object-name caches, credentials, database output, or machine-specific paths.

The `lfird` guard contains the Build IDs and call-site assumptions of the
client copy used for the experiment. Those values are evidence for this
research build, not a portable compatibility guarantee; revalidate them before
using the hook with another client.

Generated artifacts are ignored by the directory-level `.gitignore`. Build
with the toolchain available on the target host rather than copying binaries
between libc or Oracle client environments.

