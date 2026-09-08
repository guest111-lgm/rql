# rql

一个面向 Oracle Database 的快速 Rust 终端客户端。项目目标是保留 SQL*Plus 的核心交互习惯，同时逐步加入 SQLcl 的现代输出和脚本能力。

当前版本是可运行的 MVP：核心连接、SQL/PLSQL 缓冲、结果渲染和命令行交互已经打通，接口还会随着兼容性测试继续扩展。

## 为什么用 Rust

- 进程启动不需要 JVM。
- 终端输入使用轻量 readline，不加载 SQLcl 的 Java 渲染层。
- Oracle 会话直接基于官方 rust-oracledb 驱动。
- SQL 缓冲和结果模型与终端前端分离，后续可以复用到脚本模式或其他 UI。

数据库网络延迟仍然是数据库操作本身的延迟；性能目标是先消除客户端启动、初始化和回显路径上的额外开销，而不是掩盖网络耗时。

## 快速开始

需要 Rust 1.89 或更高版本。

~~~sh
cargo run -p rql -- --user sys --connect-string 10.10.180.201:1521/hmy
~~~

程序会隐藏输入密码。连接 SYSDBA：

~~~sh
cargo run -p rql -- --user sys --connect-string 10.10.180.201:1521/hmy --sysdba
~~~

也支持 SQL*Plus 风格的连接串：

~~~sh
cargo run -p rql -- sys/password@10.10.180.201:1521/hmy as sysdba
~~~

为了避免密码进入 shell history，日常建议使用前一种写法。

一次性执行：

~~~sh
cargo run -p rql -- --user system --connect-string 10.10.180.201:1521/hmy -e "select 1 from dual"
~~~

## 当前交互能力

- CONNECT：支持 user/password@host:port/service 和远程 SYSDBA。
- 多行 SQL：以分号提交普通 SQL。
- 多行 PL/SQL：以单独一行的斜杠提交。
- LIST、RUN、CLEAR、EXIT、HELP。
- SET 和 SHOW：TIMING、HEADING、FEEDBACK、TERMOUT、PAGESIZE、LINESIZE、MAXROWS、NULL、FORMAT、SQLPROMPT。
- COMMIT 和 ROLLBACK。
- DESC table。
- PRETTY、CSV、JSON 三种结果格式。
- readline 上下文历史，默认保存到用户目录下的 .rql_history。

示例：

~~~text
sys@hmy> set timing on
sys@hmy> select user, sysdate from dual;
...
sys@hmy> set format json
sys@hmy> select 1 as value from dual;
...
sys@hmy> exit
~~~

## 兼容性边界

当前 MVP 还没有宣称完整替代 SQL*Plus 或 SQLcl，以下能力会通过兼容性测试逐项加入：

- 本地操作系统认证形式 / as sysdba。
- Oracle 客户端 BEQ、OCI 专有能力。
- @script.sql、START、SPOOL 和脚本变量。
- 绑定变量、替换变量、DEFINE、ACCEPT。
- DBMS_OUTPUT 和 PL/SQL OUT 变量的交互展示。
- 更完整的 SQL*Plus COLUMN、FORMAT、PAGESIZE 和 LINESIZE 语义。
- SQLcl 的 Liquibase、DBeaver 风格导出及扩展命令。

其中 / as sysdba 依赖 Oracle 客户端的本地认证路径；当前使用的官方纯 Rust thin driver 走网络协议，第一阶段先把远程密码认证和远程 SYSDBA 做稳定。

## 项目结构

~~~text
crates/
  rql-core/    连接会话、命令解析、SQL 缓冲、结果模型和渲染
  rql-cli/     readline 终端和命令行参数
docs/          设计和兼容性说明
.github/       CI
~~~

## 开发

~~~sh
cargo check --workspace
cargo test --workspace
cargo fmt --all
cargo clippy --workspace --all-targets -- -D warnings
~~~

Oracle 集成测试需要真实数据库，默认单元测试不要求数据库在线。后续会增加可选的环境变量配置和集成测试 job。

## 许可证

当前 crate 元数据采用 Apache License 2.0；如项目后续需要与 Oracle 驱动保持一致的双许可证策略，再单独调整仓库许可证文件。
