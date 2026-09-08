use crate::error::{Result, RqlError};

/// Connection information accepted by the SQL*Plus-style command line.
#[derive(Clone, Debug, PartialEq, Eq)]
pub struct ConnectRequest {
    pub username: String,
    pub password: Option<String>,
    pub connect_string: String,
    pub sysdba: bool,
}

impl ConnectRequest {
    pub fn new(
        username: impl Into<String>,
        password: Option<String>,
        connect_string: impl Into<String>,
        sysdba: bool,
    ) -> Self {
        Self {
            username: username.into(),
            password,
            connect_string: connect_string.into(),
            sysdba,
        }
    }
}

/// Parse forms such as sys/password@host:1521/service as sysdba.
///
/// A password is optional so the CLI can prompt without echoing it. The
/// parser deliberately does not try to interpret an unescaped @ in a
/// password; users with such passwords can use the long options instead.
pub fn parse_connect_spec(input: &str) -> Result<ConnectRequest> {
    let mut spec = input.trim().to_owned();
    if spec.is_empty() {
        return Err(RqlError::new(
            "empty connect string; expected user[/password]@host:port/service",
        ));
    }

    let sysdba_suffix = " AS SYSDBA";
    let sysdba = spec.to_ascii_uppercase().ends_with(sysdba_suffix);
    if sysdba {
        let new_len = spec.len() - sysdba_suffix.len();
        spec.truncate(new_len);
        spec = spec.trim().to_owned();
    }

    if spec.starts_with('/') {
        return Err(RqlError::new(
            "OS authentication '/ as sysdba' is not supported by the current rust-oracledb thin driver; use user/password authentication for now",
        ));
    }

    let (credentials, connect_string) = spec.split_once('@').ok_or_else(|| {
        RqlError::new(
            "invalid connect string; expected user[/password]@host:port/service",
        )
    })?;

    if credentials.is_empty() || connect_string.trim().is_empty() {
        return Err(RqlError::new(
            "invalid connect string; both credentials and destination are required",
        ));
    }

    let (username, password) = match credentials.split_once('/') {
        Some((username, password)) if !username.is_empty() => {
            (username.to_owned(), Some(password.to_owned()))
        }
        Some(_) => {
            return Err(RqlError::new("invalid connect string; username is empty"));
        }
        None => (credentials.to_owned(), None),
    };

    Ok(ConnectRequest::new(
        username,
        password,
        connect_string.trim(),
        sysdba,
    ))
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn parses_sqlplus_style_sysdba_spec() {
        let request = parse_connect_spec(
            "sys/oracle@10.10.180.201:1521/hmy as sysdba",
        )
        .expect("connect spec should parse");

        assert_eq!(request.username, "sys");
        assert_eq!(request.password.as_deref(), Some("oracle"));
        assert_eq!(request.connect_string, "10.10.180.201:1521/hmy");
        assert!(request.sysdba);
    }

    #[test]
    fn password_can_be_prompted() {
        let request = parse_connect_spec("sys@db.example/hmy").unwrap();
        assert_eq!(request.password, None);
        assert!(!request.sysdba);
    }

    #[test]
    fn rejects_local_os_auth() {
        let error = parse_connect_spec("/ as sysdba").unwrap_err();
        assert!(error.message().contains("OS authentication"));
    }
}

