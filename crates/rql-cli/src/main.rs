use std::path::PathBuf;

use clap::{Parser, ValueEnum};
use rql_core::{
    BufferAction, Command, ConnectRequest, OutputFormat, Result, RqlError, Session, Settings,
    SqlBuffer, parse_connect_spec, render_result,
};
use rustyline::error::ReadlineError;

#[derive(Clone, Copy, Debug, ValueEnum)]
enum CliFormat {
    Pretty,
    Csv,
    Json,
}

impl From<CliFormat> for OutputFormat {
    fn from(value: CliFormat) -> Self {
        match value {
            CliFormat::Pretty => Self::Pretty,
            CliFormat::Csv => Self::Csv,
            CliFormat::Json => Self::Json,
        }
    }
}

#[derive(Debug, Parser)]
#[command(name = "rql", version, about = "Fast Oracle terminal client")]
struct Args {
    /// SQL*Plus-style connect spec, for example sys/password@host:1521/service as sysdba
    #[arg(value_name = "CONNECT", trailing_var_arg = true)]
    connect_spec: Vec<String>,

    /// Database user; use with --connect-string to avoid putting credentials in shell history
    #[arg(long)]
    user: Option<String>,

    /// Database password; if omitted, rql prompts without echoing it
    #[arg(long, conflicts_with = "connect_spec")]
    password: Option<String>,

    /// Easy Connect string or full Oracle connect descriptor
    #[arg(long, value_name = "DEST", conflicts_with = "connect_spec")]
    connect_string: Option<String>,

    /// Request SYSDBA privilege
    #[arg(long, conflicts_with = "connect_spec")]
    sysdba: bool,

    /// Execute one SQL statement and exit
    #[arg(short = 'e', long, value_name = "SQL")]
    execute: Option<String>,

    /// Result output format
    #[arg(long, value_enum, default_value_t = CliFormat::Pretty)]
    format: CliFormat,

    /// Maximum rows fetched per query; zero means unlimited
    #[arg(long, default_value_t = 1000)]
    max_rows: usize,

    /// Do not load or save the local readline history
    #[arg(long)]
    no_history: bool,
}

fn main() {
    if let Err(error) = run() {
        eprintln!("rql: {error}");
        std::process::exit(1);
    }
}

fn run() -> Result<()> {
    let args = Args::parse();
    let mut settings = Settings {
        format: args.format.into(),
        max_rows: args.max_rows,
        ..Settings::default()
    };

    let mut session = match initial_request(&args)? {
        Some(request) => match connect(request) {
            Ok(session) => {
                println!("Connected to {}", session.prompt_label());
                Some(session)
            }
            Err(error) => {
                eprintln!("Connection failed: {error}");
                None
            }
        },
        None => None,
    };

    if let Some(sql) = args.execute.as_deref() {
        let session = session.ok_or_else(|| {
            RqlError::new("--execute requires a successful database connection")
        })?;
        execute_sql(&session, sql, &settings)?;
        return Ok(());
    }

    let mut editor = rustyline::DefaultEditor::new()
        .map_err(|error| RqlError::new(format!("cannot initialize terminal: {error}")))?;
    let history_path = history_path();
    if !args.no_history {
        if let Some(path) = history_path.as_ref() {
            let _ = editor.load_history(path);
        }
    }

    let mut buffer = SqlBuffer::default();
    let result = loop {
        let prompt = make_prompt(&session, &settings, &buffer);
        match editor.readline(&prompt) {
            Ok(line) => {
                if !line.trim().is_empty() && !args.no_history {
                    let _ = editor.add_history_entry(line.as_str());
                }

                if buffer.is_empty() {
                    if let Some(command) = rql_core::parse_meta_command(&line) {
                        match handle_command(
                            command,
                            &mut session,
                            &mut buffer,
                            &mut settings,
                        )? {
                            LoopAction::Continue => continue,
                            LoopAction::Exit => break Ok(()),
                        }
                    }
                } else if matches!(
                    rql_core::parse_meta_command(&line),
                    Some(Command::List | Command::Run | Command::Clear)
                ) {
                    let command = rql_core::parse_meta_command(&line).expect("command matched");
                    match handle_command(
                        command,
                        &mut session,
                        &mut buffer,
                        &mut settings,
                    )? {
                        LoopAction::Continue => continue,
                        LoopAction::Exit => break Ok(()),
                    }
                }

                match buffer.accept_line(&line) {
                    BufferAction::Waiting => {}
                    BufferAction::Execute(sql) => {
                        execute_sql_option(session.as_ref(), &sql, &settings)
                    }
                }
            }
            Err(ReadlineError::Interrupted) => {
                buffer.clear();
                println!("^C");
            }
            Err(ReadlineError::Eof) => break Ok(()),
            Err(error) => break Err(RqlError::new(format!("terminal input failed: {error}"))),
        }
    };

    if !args.no_history {
        if let Some(path) = history_path {
            let _ = editor.save_history(&path);
        }
    }
    result
}

enum LoopAction {
    Continue,
    Exit,
}

fn handle_command(
    command: Command,
    session: &mut Option<Session>,
    buffer: &mut SqlBuffer,
    settings: &mut Settings,
) -> Result<LoopAction> {
    match command {
        Command::Exit => Ok(LoopAction::Exit),
        Command::Help => {
            println!("{}", rql_core::help_text());
            Ok(LoopAction::Continue)
        }
        Command::Clear => {
            buffer.clear();
            Ok(LoopAction::Continue)
        }
        Command::List => {
            println!("{}", buffer.listing());
            Ok(LoopAction::Continue)
        }
        Command::Run => {
            if let BufferAction::Execute(sql) = buffer.run() {
                execute_sql_option(session.as_ref(), &sql, settings);
            }
            Ok(LoopAction::Continue)
        }
        Command::Commit => {
            if let Some(current) = session.as_ref() {
                match current.commit() {
                    Ok(()) => println!("Commit complete."),
                    Err(error) => eprintln!("Error: {error}"),
                }
            } else {
                eprintln!("Not connected.");
            }
            Ok(LoopAction::Continue)
        }
        Command::Rollback => {
            if let Some(current) = session.as_ref() {
                match current.rollback() {
                    Ok(()) => println!("Rollback complete."),
                    Err(error) => eprintln!("Error: {error}"),
                }
            } else {
                eprintln!("Not connected.");
            }
            Ok(LoopAction::Continue)
        }
        Command::Connect(spec) => {
            match parse_connect_spec(&spec)
                .and_then(request_with_password)
                .and_then(connect)
            {
                Ok(new_session) => {
                    println!("Connected to {}", new_session.prompt_label());
                    *session = Some(new_session);
                }
                Err(error) => eprintln!("Connection failed: {error}"),
            }
            Ok(LoopAction::Continue)
        }
        Command::Describe(table) => {
            if let Some(current) = session.as_ref() {
                match current.describe(&table, settings.max_rows) {
                    Ok(result) => println!("{}", render_result(&result, settings)),
                    Err(error) => eprintln!("Error: {error}"),
                }
            } else {
                eprintln!("Not connected.");
            }
            Ok(LoopAction::Continue)
        }
        Command::Set { name, value } => {
            match settings.apply_set(&name, &value) {
                Ok(message) => println!("{message}"),
                Err(error) => eprintln!("Error: {error}"),
            }
            Ok(LoopAction::Continue)
        }
        Command::Show(name) => {
            match settings.show(&name) {
                Ok(message) => println!("{message}"),
                Err(error) => eprintln!("Error: {error}"),
            }
            Ok(LoopAction::Continue)
        }
    }
}

fn initial_request(args: &Args) -> Result<Option<ConnectRequest>> {
    if !args.connect_spec.is_empty() {
        return Ok(Some(request_with_password(parse_connect_spec(
            &args.connect_spec.join(" "),
        )?)?));
    }

    match (&args.user, &args.connect_string) {
        (Some(user), Some(connect_string)) => Ok(Some(request_with_password(
            ConnectRequest::new(
                user,
                args.password.clone(),
                connect_string,
                args.sysdba,
            ),
        )?)),
        (None, None) if args.password.is_none() && !args.sysdba => Ok(None),
        _ => Err(RqlError::new(
            "use --user USER --connect-string DEST, or provide a SQL*Plus-style connect spec",
        )),
    }
}

fn request_with_password(mut request: ConnectRequest) -> Result<ConnectRequest> {
    if request.password.is_none() {
        let prompt = format!("Enter password for {}: ", request.username);
        let password = rpassword::prompt_password(prompt)
            .map_err(|error| RqlError::new(format!("cannot read password: {error}")))?;
        request.password = Some(password);
    }
    Ok(request)
}

fn connect(request: ConnectRequest) -> Result<Session> {
    Session::connect(&request)
}

fn execute_sql(session: &Session, sql: &str, settings: &Settings) -> Result<()> {
    let result = session.execute(sql, settings.max_rows)?;
    println!("{}", render_result(&result, settings));
    Ok(())
}

fn execute_sql_option(session: Option<&Session>, sql: &str, settings: &Settings) {
    let Some(session) = session else {
        eprintln!("Not connected.");
        return;
    };

    if settings.termout {
        if let Err(error) = execute_sql(session, sql, settings) {
            eprintln!("Error: {error}");
        }
    } else if let Err(error) = session.execute(sql, settings.max_rows) {
        eprintln!("Error: {error}");
    }
}

fn make_prompt(
    session: &Option<Session>,
    settings: &Settings,
    buffer: &SqlBuffer,
) -> String {
    if !buffer.is_empty() {
        return format!("  {}  ", buffer.line_count() + 1);
    }

    match session {
        Some(session) => settings
            .sqlprompt
            .replace("{user}", session.username())
            .replace("{db}", session.target())
            .replace(
                "{privilege}",
                if session.is_sysdba() { "SYSDBA" } else { "" },
            ),
        None => "rql> ".to_owned(),
    }
}

fn history_path() -> Option<PathBuf> {
    dirs::home_dir().map(|directory| directory.join(".rql_history"))
}
