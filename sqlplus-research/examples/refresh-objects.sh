#!/bin/sh

set -eu
umask 077

TEST_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
ORACLE_HOME=${ORACLE_HOME:?Set ORACLE_HOME to the isolated Oracle client home}
CACHE_FILE=${SQLPLUS_LFIRD_COMPLETION_FILE:-$TEST_DIR/objects.cache}
TMP_FILE=$CACHE_FILE.tmp.$$
CLEAN_FILE=$CACHE_FILE.clean.$$

if [ "$#" -gt 0 ]; then
    CONNECT_STRING=$1
else
    CONNECT_STRING='/ as sysdba'
fi

trap 'rm -f -- "$TMP_FILE" "$CLEAN_FILE"' EXIT HUP INT TERM

run_sqlplus()
{
    if [ "$CONNECT_STRING" = '/ as sysdba' ]; then
        "$ORACLE_HOME/bin/sqlplus" -s -L / as sysdba
    else
        "$ORACLE_HOME/bin/sqlplus" -s -L "$CONNECT_STRING"
    fi
}

run_sqlplus > "$TMP_FILE" <<'SQL'
set heading off feedback off pagesize 0 verify off echo off termout off
set trimspool on tab off define off linesize 32767
whenever sqlerror exit sql.sqlcode
whenever oserror exit failure
select 'META'||chr(9)||'CURRENT_SCHEMA'||chr(9)||
       sys_context('USERENV', 'CURRENT_SCHEMA')
from dual;

select 'OBJECT'||chr(9)||owner||chr(9)||object_name||chr(9)||object_type
from all_objects
where object_type in ('TABLE', 'VIEW', 'MATERIALIZED VIEW', 'SEQUENCE')
and regexp_like(owner, '^[A-Za-z][A-Za-z0-9_$#]*$')
and regexp_like(object_name, '^[A-Za-z][A-Za-z0-9_$#]*$')
order by owner, object_name, object_type;

select 'SYNONYM'||chr(9)||owner||chr(9)||synonym_name||chr(9)||
       table_owner||chr(9)||table_name||chr(9)||nvl(db_link, '')
from all_synonyms
where regexp_like(owner, '^[A-Za-z][A-Za-z0-9_$#]*$')
and regexp_like(synonym_name, '^[A-Za-z][A-Za-z0-9_$#]*$')
and regexp_like(table_owner, '^[A-Za-z][A-Za-z0-9_$#]*$')
and regexp_like(table_name, '^[A-Za-z][A-Za-z0-9_$#]*$')
order by owner, synonym_name, table_owner, table_name;

select 'COLUMN'||chr(9)||owner||chr(9)||table_name||chr(9)||column_name||
       chr(9)||to_char(column_id, 'FM9999999990')
from all_tab_columns
where regexp_like(owner, '^[A-Za-z][A-Za-z0-9_$#]*$')
and regexp_like(table_name, '^[A-Za-z][A-Za-z0-9_$#]*$')
and regexp_like(column_name, '^[A-Za-z][A-Za-z0-9_$#]*$')
order by owner, table_name, column_id;
exit
SQL

awk -F '\t' '$1 == "META" || $1 == "OBJECT" || $1 == "SYNONYM" || $1 == "COLUMN" { print }' \
    "$TMP_FILE" > "$CLEAN_FILE"
mv -f -- "$CLEAN_FILE" "$CACHE_FILE"
echo "structured object/column completion cache refreshed: $CACHE_FILE"
