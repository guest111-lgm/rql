#include "sql_context.h"

#include <stdio.h>
#include <string.h>

static int expect_object_context(void)
{
    const char line[] = "FROM APP.CLI";
    le_sql_context context;

    if (!le_sql_context_analyze(line, strlen(line), &context) ||
        context.kind != LE_CONTEXT_OBJECT || context.component_count != 2 ||
        strcmp(context.components[0], "APP") != 0 ||
        strcmp(context.components[1], "CLI") != 0)
        return 0;
    return 1;
}

static int expect_alias_column_context(void)
{
    const char line[] = "SELECT c.NA FROM APP.CLIENTS c";
    const size_t cursor = strlen("SELECT c.NA");
    le_sql_context context;

    if (!le_sql_context_analyze(line, cursor, &context) ||
        context.kind != LE_CONTEXT_COLUMN || context.component_count != 2 ||
        context.relation_count != 1 ||
        strcmp(context.components[0], "c") != 0 ||
        strcmp(context.components[1], "NA") != 0 ||
        strcmp(context.relations[0].owner, "APP") != 0 ||
        strcmp(context.relations[0].name, "CLIENTS") != 0 ||
        strcmp(context.relations[0].alias, "C") != 0)
        return 0;
    return 1;
}

static int expect_ignored_text(void)
{
    const char string_line[] = "SELECT 'FROM AP";
    const char comment_line[] = "SELECT /* FROM AP";
    le_sql_context context;

    if (!le_sql_context_analyze(string_line, strlen(string_line), &context) ||
        !context.disabled)
        return 0;
    if (!le_sql_context_analyze(comment_line, strlen(comment_line), &context) ||
        !context.disabled)
        return 0;
    return 1;
}

int main(void)
{
    if (!expect_object_context() || !expect_alias_column_context() ||
        !expect_ignored_text()) {
        fprintf(stderr, "SQL context test: FAIL\n");
        return 1;
    }
    puts("SQL context test: PASS");
    return 0;
}
