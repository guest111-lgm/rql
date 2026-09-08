/// What the input loop should do after accepting one line.
#[derive(Debug, PartialEq, Eq)]
pub enum BufferAction {
    Waiting,
    Execute(String),
}

/// SQL*Plus-like multi-line SQL and PL/SQL buffer.
#[derive(Debug, Default)]
pub struct SqlBuffer {
    lines: Vec<String>,
    last_statement: Option<String>,
}

impl SqlBuffer {
    pub fn accept_line(&mut self, line: &str) -> BufferAction {
        if line.trim() == "/" {
            return self.take_or_repeat();
        }

        self.lines.push(line.to_owned());
        if !self.requires_slash() && has_terminating_semicolon(line) {
            self.take_current()
        } else {
            BufferAction::Waiting
        }
    }

    /// Execute the current buffer, or repeat the previous statement when the
    /// current buffer is empty.
    pub fn run(&mut self) -> BufferAction {
        self.take_or_repeat()
    }

    pub fn clear(&mut self) {
        self.lines.clear();
    }

    pub fn is_empty(&self) -> bool {
        self.lines.is_empty()
    }

    pub fn line_count(&self) -> usize {
        self.lines.len()
    }

    pub fn listing(&self) -> String {
        if self.lines.is_empty() {
            return "SQL buffer is empty.".to_owned();
        }

        self.lines
            .iter()
            .enumerate()
            .map(|(index, line)| format!("{:>4}  {}", index + 1, line))
            .collect::<Vec<_>>()
            .join("\n")
    }

    fn take_or_repeat(&mut self) -> BufferAction {
        if self.lines.is_empty() {
            return self
                .last_statement
                .clone()
                .map_or(BufferAction::Waiting, BufferAction::Execute);
        }

        self.take_current()
    }

    fn take_current(&mut self) -> BufferAction {
        let requires_slash = self.requires_slash();
        let mut statement = self.lines.join("\n").trim().to_owned();
        self.lines.clear();

        if !requires_slash && statement.ends_with(';') {
            statement.pop();
            statement = statement.trim_end().to_owned();
        }

        if statement.is_empty() {
            return BufferAction::Waiting;
        }

        self.last_statement = Some(statement.clone());
        BufferAction::Execute(statement)
    }

    fn requires_slash(&self) -> bool {
        let statement = self.lines.join("\n");
        let upper = statement.trim_start().to_ascii_uppercase();

        if upper.starts_with("BEGIN") || upper.starts_with("DECLARE") {
            return true;
        }

        if !upper.starts_with("CREATE") || !upper.contains(" OR REPLACE") {
            return false;
        }

        [
            " PROCEDURE",
            " FUNCTION",
            " PACKAGE",
            " TRIGGER",
            " TYPE",
            " JAVA",
        ]
        .iter()
        .any(|keyword| upper.contains(keyword))
    }
}

fn has_terminating_semicolon(line: &str) -> bool {
    let mut in_single_quote = false;
    let mut in_double_quote = false;
    let characters: Vec<char> = line.chars().collect();
    let mut index = 0;

    while index < characters.len() {
        match characters[index] {
            '\'' if !in_double_quote => {
                if in_single_quote && characters.get(index + 1) == Some(&'\'') {
                    index += 2;
                    continue;
                }
                in_single_quote = !in_single_quote;
            }
            '"' if !in_single_quote => {
                if in_double_quote && characters.get(index + 1) == Some(&'"') {
                    index += 2;
                    continue;
                }
                in_double_quote = !in_double_quote;
            }
            ';' if !in_single_quote && !in_double_quote => {
                return characters[index + 1..]
                    .iter()
                    .all(|remainder| remainder.is_whitespace());
            }
            _ => {}
        }
        index += 1;
    }

    false
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn executes_single_line_sql_at_semicolon() {
        let mut buffer = SqlBuffer::default();
        assert_eq!(
            buffer.accept_line("select 1 from dual;"),
            BufferAction::Execute("select 1 from dual".to_owned())
        );
    }

    #[test]
    fn keeps_plsql_until_slash() {
        let mut buffer = SqlBuffer::default();
        assert_eq!(buffer.accept_line("begin"), BufferAction::Waiting);
        assert_eq!(buffer.accept_line("  null;"), BufferAction::Waiting);
        assert_eq!(buffer.accept_line("end;"), BufferAction::Waiting);
        assert_eq!(
            buffer.accept_line("/"),
            BufferAction::Execute("begin\n  null;\nend;".to_owned())
        );
    }

    #[test]
    fn slash_repeats_last_statement() {
        let mut buffer = SqlBuffer::default();
        let _ = buffer.accept_line("select 1 from dual;");
        assert_eq!(
            buffer.accept_line("/"),
            BufferAction::Execute("select 1 from dual".to_owned())
        );
    }

    #[test]
    fn ignores_semicolons_inside_quoted_literals() {
        let mut buffer = SqlBuffer::default();
        assert_eq!(
            buffer.accept_line("select 'a; it''s fine' from dual;"),
            BufferAction::Execute("select 'a; it''s fine' from dual".to_owned())
        );
    }
}
