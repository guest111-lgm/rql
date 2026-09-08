use std::error::Error;
use std::fmt::{Display, Formatter};

/// Error type presented by the core library.
#[derive(Debug, Clone, PartialEq, Eq)]
pub struct RqlError {
    message: String,
}

impl RqlError {
    pub fn new(message: impl Into<String>) -> Self {
        Self {
            message: message.into(),
        }
    }

    pub fn message(&self) -> &str {
        &self.message
    }
}

impl Display for RqlError {
    fn fmt(&self, formatter: &mut Formatter<'_>) -> std::fmt::Result {
        formatter.write_str(&self.message)
    }
}

impl Error for RqlError {}

impl From<oracledb::Error> for RqlError {
    fn from(error: oracledb::Error) -> Self {
        Self::new(error.to_string())
    }
}

pub type Result<T> = std::result::Result<T, RqlError>;

