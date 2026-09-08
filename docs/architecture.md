# Architecture

## Layers

The workspace has two crates:

- rql-core owns the Oracle session, connect-spec parsing, SQL buffer, settings,
  result model, and output renderers.
- rql-cli owns clap argument parsing, password prompting, readline history,
  prompt construction, and the interactive event loop.

The dependency direction is one-way: the CLI depends on the core library. This
keeps terminal rendering decisions out of the database session and makes a
future script runner possible without duplicating SQL execution code.

## Execution path

~~~text
input line
   |
   +-- local command? --> settings / transaction / connection action
   |
   +-- SQL buffer ------> complete SQL or PL/SQL
                              |
                              v
                       rust-oracledb session
                              |
                              v
                       typed result model
                              |
                              v
                    pretty, CSV, or JSON renderer
~~~

## Driver boundary

The project is pinned to the current crates.io release of the official
rust-oracledb driver. The driver uses a pure Rust implementation of the Oracle
network protocol, so the first connection path is username/password
authentication over Easy Connect or a full descriptor.

Local operating-system authentication and Oracle Client-specific BEQ/OCI
features are intentionally isolated as future connection backends. They
should not leak special cases into the interactive command loop.

## Performance principles

- Create one session and reuse it for the lifetime of the shell.
- Keep statement caching in the driver rather than rebuilding a client per SQL.
- Fetch rows incrementally and stop at MAXROWS.
- Render one complete output buffer per statement to reduce terminal writes.
- Keep startup configuration small and do not read SQL*Plus glogin.sql or
  login.sql implicitly.
