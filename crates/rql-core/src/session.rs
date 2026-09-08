use std::time::{Duration, Instant};

use oracledb::{Config, Connection, Row};

use crate::connect::ConnectRequest;
use crate::error::{Result, RqlError};

#[derive(Clone, Debug, PartialEq, Eq)]
pub struct Column {
    pub name: String,
    pub type_name: String,
}

#[derive(Clone, Debug, PartialEq, Eq)]
pub enum Cell {
    Null,
    Text(String),
    Bytes(Vec<u8>),
    Unsupported(String),
}

#[derive(Clone, Debug)]
pub struct QueryResult {
    pub columns: Vec<Column>,
    pub rows: Vec<Vec<Cell>>,
    pub elapsed: Duration,
    pub truncated: bool,
}

#[derive(Clone, Debug)]
pub enum ExecutionResult {
    Query(QueryResult),
    Dml {
        rows_affected: u64,
        elapsed: Duration,
    },
}

/// A connected Oracle session backed by the official pure-Rust driver.
pub struct Session {
    connection: Connection,
    username: String,
    target: String,
    sysdba: bool,
}

impl Session {
    pub fn connect(request: &ConnectRequest) -> Result<Self> {
        let password = request.password.as_deref().ok_or_else(|| {
            RqlError::new("a database password is required before connecting")
        })?;

        let mut config = Config::default()
            .set_credentials(&request.username, password)
            .set_connect_string(&request.connect_string)?
            .set_driver_name("rql");

        if request.sysdba {
            config = config.set_auth_mode(oracledb::AUTH_MODE_SYSDBA);
        }

        let connection = oracledb::connect(config)?;
        let target = connection
            .service_name()
            .ok()
            .filter(|service| !service.is_empty())
            .map(str::to_owned)
            .unwrap_or_else(|| request.connect_string.clone());

        Ok(Self {
            connection,
            username: request.username.clone(),
            target,
            sysdba: request.sysdba,
        })
    }

    pub fn username(&self) -> &str {
        &self.username
    }

    pub fn target(&self) -> &str {
        &self.target
    }

    pub fn is_sysdba(&self) -> bool {
        self.sysdba
    }

    pub fn prompt_label(&self) -> String {
        let privilege = if self.sysdba { " [SYSDBA]" } else { "" };
        format!("{}@{}{}", self.username, self.target, privilege)
    }

    pub fn commit(&self) -> Result<()> {
        self.connection.commit()?;
        Ok(())
    }

    pub fn rollback(&self) -> Result<()> {
        self.connection.rollback()?;
        Ok(())
    }

    pub fn execute(&self, input_sql: &str, max_rows: usize) -> Result<ExecutionResult> {
        let sql = normalize_sql(input_sql);
        if sql.is_empty() {
            return Err(RqlError::new("cannot execute an empty SQL statement"));
        }

        let started = Instant::now();
        if is_query(&sql) {
            let mut cursor = self.connection.query(&sql, &[])?;
            let columns = cursor
                .columns()
                .iter()
                .map(|metadata| Column {
                    name: metadata.name().to_owned(),
                    type_name: metadata.db_type().name().to_owned(),
                })
                .collect::<Vec<_>>();

            let mut rows = Vec::new();
            let mut truncated = false;
            for row_result in &mut cursor {
                let row = row_result?;
                if max_rows != 0 && rows.len() >= max_rows {
                    truncated = true;
                    break;
                }
                rows.push(read_row(&row, &columns)?);
            }

            Ok(ExecutionResult::Query(QueryResult {
                columns,
                rows,
                elapsed: started.elapsed(),
                truncated,
            }))
        } else {
            let result = self.connection.execute(&sql, &[])?;
            Ok(ExecutionResult::Dml {
                rows_affected: result.rows_affected(),
                elapsed: started.elapsed(),
            })
        }
    }

    /// A first implementation of SQL*Plus DESC using the data dictionary.
    /// The table name is bound as a value rather than interpolated into SQL.
    pub fn describe(&self, table: &str, max_rows: usize) -> Result<ExecutionResult> {
        let table = table.trim();
        if table.is_empty() {
            return Err(RqlError::new("DESC requires a table name"));
        }

        let started = Instant::now();
        let table_name = table.to_owned();
        let sql = "select column_name, data_type, data_length, nullable \
                   from user_tab_columns \
                   where table_name = upper(:1) \
                   order by column_id";
        let mut cursor = self.connection.query(sql, &[&table_name])?;
        let columns = cursor
            .columns()
            .iter()
            .map(|metadata| Column {
                name: metadata.name().to_owned(),
                type_name: metadata.db_type().name().to_owned(),
            })
            .collect::<Vec<_>>();
        let mut rows = Vec::new();
        let mut truncated = false;
        for row_result in &mut cursor {
            let row = row_result?;
            if max_rows != 0 && rows.len() >= max_rows {
                truncated = true;
                break;
            }
            rows.push(read_row(&row, &columns)?);
        }

        Ok(ExecutionResult::Query(QueryResult {
            columns,
            rows,
            elapsed: started.elapsed(),
            truncated,
        }))
    }
}

fn read_row(row: &Row, columns: &[Column]) -> Result<Vec<Cell>> {
    columns
        .iter()
        .enumerate()
        .map(|(index, column)| read_cell(row, index, column))
        .collect()
}

fn read_cell(row: &Row, index: usize, column: &Column) -> Result<Cell> {
    let cell = match column.type_name.as_str() {
        "DB_TYPE_CHAR"
        | "DB_TYPE_CLOB"
        | "DB_TYPE_LONG"
        | "DB_TYPE_LONG_NVARCHAR"
        | "DB_TYPE_NCHAR"
        | "DB_TYPE_NCLOB"
        | "DB_TYPE_NVARCHAR"
        | "DB_TYPE_ROWID"
        | "DB_TYPE_UROWID"
        | "DB_TYPE_VARCHAR" => row
            .get::<Option<String>>(index)?
            .map_or(Cell::Null, Cell::Text),
        "DB_TYPE_NUMBER" => row
            .get::<Option<oracledb::OracleNumber>>(index)?
            .map_or(Cell::Null, |value| Cell::Text(value.to_string())),
        "DB_TYPE_DATE"
        | "DB_TYPE_TIMESTAMP"
        | "DB_TYPE_TIMESTAMP_LTZ"
        | "DB_TYPE_TIMESTAMP_TZ" => row
            .get::<Option<oracledb::OracleTimestamp>>(index)?
            .map_or(Cell::Null, |value| Cell::Text(value.to_string())),
        "DB_TYPE_BINARY_DOUBLE" => row
            .get::<Option<f64>>(index)?
            .map_or(Cell::Null, |value| Cell::Text(value.to_string())),
        "DB_TYPE_BINARY_FLOAT" => row
            .get::<Option<f32>>(index)?
            .map_or(Cell::Null, |value| Cell::Text(value.to_string())),
        "DB_TYPE_BOOLEAN" => row
            .get::<Option<bool>>(index)?
            .map_or(Cell::Null, |value| Cell::Text(value.to_string())),
        "DB_TYPE_RAW" | "DB_TYPE_LONG_RAW" | "DB_TYPE_BLOB" => row
            .get::<Option<Vec<u8>>>(index)?
            .map_or(Cell::Null, Cell::Bytes),
        "DB_TYPE_INTERVAL_DS" => row
            .get::<Option<oracledb::OracleIntervalDS>>(index)?
            .map_or(Cell::Null, |value| Cell::Text(value.to_string())),
        "DB_TYPE_INTERVAL_YM" => row
            .get::<Option<oracledb::OracleIntervalYM>>(index)?
            .map_or(Cell::Null, |value| Cell::Text(value.to_string())),
        "DB_TYPE_JSON" => row
            .get::<Option<oracledb::JsonValue>>(index)?
            .map_or(Cell::Null, |value| Cell::Text(format!("{value:?}"))),
        other => Cell::Unsupported(other.to_owned()),
    };

    Ok(cell)
}

fn is_query(sql: &str) -> bool {
    matches!(
        first_keyword(sql).as_str(),
        "SELECT" | "WITH" | "TABLE" | "VALUES" | "EXPLAIN"
    )
}

fn is_plsql(sql: &str) -> bool {
    let upper = sql.trim_start().to_ascii_uppercase();
    upper.starts_with("BEGIN")
        || upper.starts_with("DECLARE")
        || (upper.starts_with("CREATE") && upper.contains(" OR REPLACE"))
}

fn normalize_sql(input: &str) -> String {
    let mut sql = input.trim().to_owned();
    if sql.ends_with('/') {
        sql.pop();
        sql = sql.trim_end().to_owned();
    }
    if !is_plsql(&sql) && sql.ends_with(';') {
        sql.pop();
        sql = sql.trim_end().to_owned();
    }
    sql
}

fn first_keyword(sql: &str) -> String {
    sql.split_whitespace()
        .next()
        .unwrap_or_default()
        .trim_matches(|character: char| !character.is_ascii_alphabetic())
        .to_ascii_uppercase()
}

