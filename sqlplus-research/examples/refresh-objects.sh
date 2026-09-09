#!/bin/sh

set -eu

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
set trimspool on tab off linesize 32767
whenever sqlerror exit sql.sqlcode
whenever oserror exit failure
select object_name
from (
    select distinct object_name
    from all_objects
    where object_type in ('TABLE', 'VIEW', 'MATERIALIZED VIEW', 'SEQUENCE')
    union
    select distinct synonym_name
    from all_synonyms
)
where regexp_like(object_name, '^[A-Za-z][A-Za-z0-9_$#]*$')
order by object_name;
exit
SQL

awk 'NF { print $1 }' "$TMP_FILE" > "$CLEAN_FILE"
mv -f -- "$CLEAN_FILE" "$CACHE_FILE"
echo "object completion cache refreshed: $CACHE_FILE"

