#ifndef SQLPLUS_RESEARCH_SQL_CONTEXT_H
#define SQLPLUS_RESEARCH_SQL_CONTEXT_H

#include <stddef.h>

#define LE_CONTEXT_NAME_MAX 256
#define LE_CONTEXT_COMPONENTS_MAX 3
#define LE_CONTEXT_RELATIONS_MAX 128

enum le_context_kind {
    LE_CONTEXT_GENERIC = 0,
    LE_CONTEXT_OBJECT,
    LE_CONTEXT_COLUMN
};

typedef struct le_context_relation {
    char owner[LE_CONTEXT_NAME_MAX];
    char name[LE_CONTEXT_NAME_MAX];
    char alias[LE_CONTEXT_NAME_MAX];
} le_context_relation;

typedef struct le_sql_context {
    enum le_context_kind kind;
    int disabled;
    size_t component_start;
    size_t qualified_start;
    size_t component_count;
    char components[LE_CONTEXT_COMPONENTS_MAX][LE_CONTEXT_NAME_MAX];

    le_context_relation relations[LE_CONTEXT_RELATIONS_MAX];
    size_t relation_count;
} le_sql_context;

/* Analyze only the current input line; it never executes or contacts Oracle. */
int le_sql_context_analyze(const char *line, size_t cursor,
                           le_sql_context *context);

#endif
