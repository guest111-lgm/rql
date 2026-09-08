//! Database and terminal-independent building blocks for rql.
//!
//! The CLI is intentionally a thin shell around this crate. Keeping the SQL
//! buffer, settings, result model, and Oracle session here makes it possible
//! to add a non-interactive mode and other front ends without duplicating
//! behavior.

mod buffer;
mod commands;
mod connect;
mod error;
mod render;
mod session;
mod settings;

pub use buffer::{BufferAction, SqlBuffer};
pub use commands::{help_text, parse_meta_command, Command};
pub use connect::{parse_connect_spec, ConnectRequest};
pub use error::{Result, RqlError};
pub use render::render_result;
pub use session::{Cell, Column, ExecutionResult, QueryResult, Session};
pub use settings::{OutputFormat, Settings};

