# Contributing

## Workflow

1. Create a focused branch from main.
2. Keep database behavior in rql-core and terminal concerns in rql-cli.
3. Add or update unit tests for parser, buffer, settings, and rendering changes.
4. Run the workspace checks before opening a pull request.

## Oracle integration tests

Tests that require Oracle Database should be opt-in. They must not make the
default test suite depend on a developer workstation, an Oracle Client
installation, or a reachable database.

Do not commit passwords, connect descriptors containing secrets, generated
history files, or local Oracle configuration.

## Compatibility changes

When adding a SQL*Plus or SQLcl command, document the supported syntax and
explicitly record unsupported edge cases. Compatibility is measured by
behavior, not only by accepting a command name.
