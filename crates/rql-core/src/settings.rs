use std::fmt::{Display, Formatter};

use crate::error::{Result, RqlError};

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum OutputFormat {
    Pretty,
    Csv,
    Json,
}

impl Display for OutputFormat {
    fn fmt(&self, formatter: &mut Formatter<'_>) -> std::fmt::Result {
        formatter.write_str(match self {
            Self::Pretty => "pretty",
            Self::Csv => "csv",
            Self::Json => "json",
        })
    }
}

impl std::str::FromStr for OutputFormat {
    type Err = RqlError;

    fn from_str(value: &str) -> Result<Self> {
        match value.trim().to_ascii_lowercase().as_str() {
            "pretty" | "default" | "table" => Ok(Self::Pretty),
            "csv" => Ok(Self::Csv),
            "json" => Ok(Self::Json),
            _ => Err(RqlError::new(format!(
                "unknown output format '{value}'; use pretty, csv, or json"
            ))),
        }
    }
}

#[derive(Clone, Debug)]
pub struct Settings {
    pub timing: bool,
    pub heading: bool,
    pub feedback: bool,
    pub termout: bool,
    pub pagesize: usize,
    pub linesize: usize,
    pub max_rows: usize,
    pub null_value: String,
    pub format: OutputFormat,
    pub sqlprompt: String,
}

impl Default for Settings {
    fn default() -> Self {
        Self {
            timing: false,
            heading: true,
            feedback: true,
            termout: true,
            pagesize: 24,
            linesize: 120,
            max_rows: 1000,
            null_value: String::new(),
            format: OutputFormat::Pretty,
            sqlprompt: "{user}@{db}> ".to_owned(),
        }
    }
}

impl Settings {
    pub fn apply_set(&mut self, name: &str, value: &str) -> Result<String> {
        let normalized = name.trim().to_ascii_lowercase();
        let value = value.trim();

        match normalized.as_str() {
            "timing" => {
                self.timing = parse_bool(value)?;
                Ok(format!("timing {}", on_off(self.timing)))
            }
            "heading" | "head" => {
                self.heading = parse_bool(value)?;
                Ok(format!("heading {}", on_off(self.heading)))
            }
            "feedback" => {
                self.feedback = parse_bool(value)?;
                Ok(format!("feedback {}", on_off(self.feedback)))
            }
            "termout" => {
                self.termout = parse_bool(value)?;
                Ok(format!("termout {}", on_off(self.termout)))
            }
            "pagesize" | "pages" => {
                self.pagesize = parse_usize("pagesize", value)?;
                Ok(format!("pagesize {}", self.pagesize))
            }
            "linesize" | "line" => {
                self.linesize = parse_usize("linesize", value)?;
                Ok(format!("linesize {}", self.linesize))
            }
            "maxrows" | "max_rows" => {
                self.max_rows = parse_usize("maxrows", value)?;
                Ok(format!("maxrows {}", self.max_rows))
            }
            "null" => {
                self.null_value = value.to_owned();
                Ok(format!(
                    "null display value set to '{}'",
                    self.null_value
                ))
            }
            "format" | "sqlformat" => {
                self.format = value.parse()?;
                Ok(format!("format {}", self.format))
            }
            "sqlprompt" | "sqlp" => {
                if value.is_empty() {
                    return Err(RqlError::new(
                        "sqlprompt cannot be empty; use {user} and {db} placeholders",
                    ));
                }
                self.sqlprompt = value.to_owned();
                Ok(format!("sqlprompt {}", self.sqlprompt))
            }
            "" => Err(RqlError::new("SET requires an option")),
            _ => Err(RqlError::new(format!(
                "unknown SET option '{name}'; use HELP for supported options"
            ))),
        }
    }

    pub fn show(&self, name: &str) -> Result<String> {
        let name = name.trim().to_ascii_lowercase();
        if name.is_empty() || name == "all" {
            return Ok(format!(
                "timing    {}\nheading   {}\nfeedback  {}\ntermout   {}\npagesize  {}\nlinesize  {}\nmaxrows   {}\nnull      {:?}\nformat    {}\nsqlprompt {:?}",
                on_off(self.timing),
                on_off(self.heading),
                on_off(self.feedback),
                on_off(self.termout),
                self.pagesize,
                self.linesize,
                self.max_rows,
                self.null_value,
                self.format,
                self.sqlprompt,
            ));
        }

        let value = match name.as_str() {
            "timing" => on_off(self.timing).to_owned(),
            "heading" | "head" => on_off(self.heading).to_owned(),
            "feedback" => on_off(self.feedback).to_owned(),
            "termout" => on_off(self.termout).to_owned(),
            "pagesize" | "pages" => self.pagesize.to_string(),
            "linesize" | "line" => self.linesize.to_string(),
            "maxrows" | "max_rows" => self.max_rows.to_string(),
            "null" => self.null_value.clone(),
            "format" | "sqlformat" => self.format.to_string(),
            "sqlprompt" | "sqlp" => self.sqlprompt.clone(),
            _ => {
                return Err(RqlError::new(format!(
                    "unknown SHOW option '{name}'"
                )));
            }
        };
        Ok(format!("{name} {value}"))
    }
}

fn parse_bool(value: &str) -> Result<bool> {
    match value.to_ascii_lowercase().as_str() {
        "on" | "true" | "yes" | "1" => Ok(true),
        "off" | "false" | "no" | "0" => Ok(false),
        _ => Err(RqlError::new(format!(
            "expected ON or OFF, got '{value}'"
        ))),
    }
}

fn parse_usize(name: &str, value: &str) -> Result<usize> {
    value
        .parse::<usize>()
        .map_err(|_| RqlError::new(format!("{name} expects a non-negative integer")))
}

fn on_off(value: bool) -> &'static str {
    if value { "on" } else { "off" }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn applies_common_options() {
        let mut settings = Settings::default();
        settings.apply_set("timing", "on").unwrap();
        settings.apply_set("maxrows", "25").unwrap();
        settings.apply_set("format", "json").unwrap();
        assert!(settings.timing);
        assert_eq!(settings.max_rows, 25);
        assert_eq!(settings.format, OutputFormat::Json);
    }
}

