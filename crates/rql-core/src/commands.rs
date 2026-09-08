/// Commands handled locally by the interactive shell.
#[derive(Debug, PartialEq, Eq)]
pub enum Command {
    Exit,
    Help,
    Clear,
    List,
    Run,
    Commit,
    Rollback,
    Connect(String),
    Describe(String),
    Set { name: String, value: String },
    Show(String),
}

/// Return a local command, or None when the input should be treated as SQL.
pub fn parse_meta_command(line: &str) -> Option<Command> {
    let trimmed = line.trim();
    if trimmed.is_empty() {
        return None;
    }

    let (command, rest) = split_first_word(trimmed);
    let command = command.to_ascii_lowercase();
    let rest = rest.trim().to_owned();

    Some(match command.as_str() {
        "exit" | "quit" | "bye" => Command::Exit,
        "help" | "?" => Command::Help,
        "clear" | "cl" => Command::Clear,
        "list" | "l" => Command::List,
        "run" | "r" => Command::Run,
        "commit" if rest.is_empty() => Command::Commit,
        "rollback" if rest.is_empty() => Command::Rollback,
        "connect" => Command::Connect(rest),
        "describe" | "desc" => Command::Describe(rest),
        "set" => {
            let (name, value) = split_first_word(&rest);
            Command::Set {
                name: name.to_owned(),
                value: value.trim().to_owned(),
            }
        }
        "show" => Command::Show(rest),
        _ => return None,
    })
}

fn split_first_word(input: &str) -> (&str, &str) {
    match input.find(|character: char| character.is_whitespace()) {
        Some(index) => (&input[..index], &input[index..]),
        None => (input, ""),
    }
}

pub fn help_text() -> &'static str {
    "rql commands:\n\
  CONNECT user[/password]@host:port/service [AS SYSDBA]\n\
  SET TIMING ON|OFF                 Toggle elapsed time\n\
  SET FORMAT PRETTY|CSV|JSON        Select result format\n\
  SET MAXROWS n                     Limit fetched rows (0 = unlimited)\n\
  SET SQLPROMPT {user}@{db}>        Change prompt template\n\
  SHOW ALL                           Show shell settings\n\
  DESC table                         Describe a table\n\
  LIST                               Show the current SQL buffer\n\
  RUN or /                           Execute the buffer / repeat last SQL\n\
  CLEAR                              Clear the current SQL buffer\n\
  COMMIT / ROLLBACK                  Transaction control\n\
  EXIT or QUIT                       Leave rql\n\
\n\
SQL is submitted with a trailing semicolon. PL/SQL blocks are submitted with a\n\
standalone slash on the following line."
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn recognizes_settings() {
        assert_eq!(
            parse_meta_command("set timing on"),
            Some(Command::Set {
                name: "timing".to_owned(),
                value: "on".to_owned(),
            })
        );
    }

    #[test]
    fn unknown_input_is_sql() {
        assert_eq!(parse_meta_command("select 1 from dual"), None);
    }
}

