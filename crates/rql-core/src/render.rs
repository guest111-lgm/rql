use crate::session::{Cell, ExecutionResult, QueryResult};
use crate::settings::{OutputFormat, Settings};

pub fn render_result(result: &ExecutionResult, settings: &Settings) -> String {
    match settings.format {
        OutputFormat::Pretty => render_pretty(result, settings),
        OutputFormat::Csv => render_csv(result),
        OutputFormat::Json => render_json(result),
    }
}

fn render_pretty(result: &ExecutionResult, settings: &Settings) -> String {
    match result {
        ExecutionResult::Dml {
            rows_affected,
            elapsed,
        } => {
            let mut output = if settings.feedback {
                format!("{rows_affected} rows affected.")
            } else {
                String::new()
            };
            append_timing(&mut output, *elapsed, settings);
            output
        }
        ExecutionResult::Query(result) => render_pretty_query(result, settings),
    }
}

fn render_pretty_query(result: &QueryResult, settings: &Settings) -> String {
    let values = result
        .rows
        .iter()
        .map(|row| {
            row.iter()
                .map(|cell| cell_text(cell, settings))
                .collect::<Vec<_>>()
        })
        .collect::<Vec<_>>();

    let mut widths = result
        .columns
        .iter()
        .map(|column| column.name.chars().count())
        .collect::<Vec<_>>();
    for row in &values {
        for (index, value) in row.iter().enumerate() {
            if let Some(width) = widths.get_mut(index) {
                *width = (*width).max(value.chars().count());
            }
        }
    }

    let mut lines = Vec::new();
    if settings.heading && !result.columns.is_empty() {
        lines.push(format_row(
            &result
                .columns
                .iter()
                .map(|column| column.name.clone())
                .collect::<Vec<_>>(),
            &widths,
        ));
        lines.push(
            widths
                .iter()
                .map(|width| "-".repeat(*width))
                .collect::<Vec<_>>()
                .join("  "),
        );
    }

    for row in &values {
        lines.push(format_row(row, &widths));
    }

    if result.rows.is_empty() && settings.feedback {
        lines.push("no rows selected.".to_owned());
    } else if settings.feedback {
        let noun = if result.rows.len() == 1 { "row" } else { "rows" };
        lines.push(format!("{} {noun} selected.", result.rows.len()));
    }
    if result.truncated {
        lines.push("output truncated by SET MAXROWS.".to_owned());
    }

    let mut output = lines.join("\n");
    append_timing(&mut output, result.elapsed, settings);
    output
}

fn render_csv(result: &ExecutionResult) -> String {
    match result {
        ExecutionResult::Dml { rows_affected, .. } => {
            format!("rows_affected\n{rows_affected}")
        }
        ExecutionResult::Query(result) => {
            let mut lines = Vec::with_capacity(result.rows.len() + 1);
            lines.push(
                result
                    .columns
                    .iter()
                    .map(|column| csv_escape(&column.name))
                    .collect::<Vec<_>>()
                    .join(","),
            );
            for row in &result.rows {
                lines.push(
                    row.iter()
                        .map(|cell| csv_escape(&cell_text(cell, &Settings::default())))
                        .collect::<Vec<_>>()
                        .join(","),
                );
            }
            lines.join("\n")
        }
    }
}

fn render_json(result: &ExecutionResult) -> String {
    let value = match result {
        ExecutionResult::Dml {
            rows_affected,
            elapsed,
        } => serde_json::json!({
            "kind": "dml",
            "rowsAffected": rows_affected,
            "elapsedMs": elapsed.as_secs_f64() * 1000.0,
        }),
        ExecutionResult::Query(result) => {
            let rows = result
                .rows
                .iter()
                .map(|row| {
                    row.iter()
                        .map(|cell| match cell {
                            Cell::Null => serde_json::Value::Null,
                            _ => serde_json::Value::String(cell_text(
                                cell,
                                &Settings::default(),
                            )),
                        })
                        .collect::<Vec<_>>()
                })
                .collect::<Vec<_>>();
            serde_json::json!({
                "kind": "query",
                "columns": result.columns.iter().map(|column| serde_json::json!({
                    "name": &column.name,
                    "type": &column.type_name,
                })).collect::<Vec<_>>(),
                "rows": rows,
                "truncated": result.truncated,
                "elapsedMs": result.elapsed.as_secs_f64() * 1000.0,
            })
        }
    };

    serde_json::to_string_pretty(&value).unwrap_or_else(|_| "{}".to_owned())
}

fn format_row(row: &[String], widths: &[usize]) -> String {
    row.iter()
        .enumerate()
        .map(|(index, value)| {
            let width = widths.get(index).copied().unwrap_or(value.chars().count());
            format!("{value:<width$}")
        })
        .collect::<Vec<_>>()
        .join("  ")
        .trim_end()
        .to_owned()
}

fn cell_text(cell: &Cell, settings: &Settings) -> String {
    match cell {
        Cell::Null => settings.null_value.clone(),
        Cell::Text(value) => value.clone(),
        Cell::Bytes(value) => {
            let mut output = String::with_capacity(value.len() * 2 + 2);
            output.push_str("0x");
            for byte in value {
                output.push_str(&format!("{byte:02X}"));
            }
            output
        }
        Cell::Unsupported(type_name) => format!("<{type_name}>"),
    }
}

fn csv_escape(value: &str) -> String {
    if value.contains(',')
        || value.contains('"')
        || value.contains('\n')
        || value.contains('\r')
    {
        format!("\"{}\"", value.replace('"', "\"\""))
    } else {
        value.to_owned()
    }
}

fn append_timing(
    output: &mut String,
    elapsed: std::time::Duration,
    settings: &Settings,
) {
    if settings.timing {
        if !output.is_empty() {
            output.push('\n');
        }
        output.push_str(&format!("Elapsed: {:.3}s", elapsed.as_secs_f64()));
    }
}

