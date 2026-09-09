#!/bin/sh

set -eu

TEST_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
ORACLE_HOME=${ORACLE_HOME:?Set ORACLE_HOME to the isolated Oracle client home}
PATH_VALUE=$ORACLE_HOME/bin:${PATH:-/usr/bin:/bin}
LD_LIBRARY_PATH_VALUE=$ORACLE_HOME/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}
PROBE_PATH=$TEST_DIR/../lfird-probe/liblfird_probe.so
COMPLETION_FILE=${SQLPLUS_LFIRD_COMPLETION_FILE:-$TEST_DIR/objects.cache}

if [ "$(id -u)" -eq 0 ]; then
    echo "Run this example as the Oracle software owner, not as root." >&2
    exit 1
fi

if [ ! -x "$ORACLE_HOME/bin/sqlplus" ]; then
    echo "SQL*Plus was not found under ORACLE_HOME=$ORACLE_HOME" >&2
    exit 1
fi

if [ "$#" -eq 0 ]; then
    set -- /nolog
fi

exec env \
    ORACLE_HOME="$ORACLE_HOME" \
    PATH="$PATH_VALUE" \
    LD_LIBRARY_PATH="$LD_LIBRARY_PATH_VALUE" \
    LD_PRELOAD="$PROBE_PATH" \
    SQLPLUS_LFIRD_REPLACE=${SQLPLUS_LFIRD_REPLACE:-1} \
    SQLPLUS_LFIRD_TRACE=${SQLPLUS_LFIRD_TRACE:-0} \
    SQLPLUS_LFIRD_PROMPT=${SQLPLUS_LFIRD_PROMPT:-'SQL> '} \
    SQLPLUS_LFIRD_COMPLETION_FILE="$COMPLETION_FILE" \
    "$ORACLE_HOME/bin/sqlplus" "$@"

