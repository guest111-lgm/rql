#include "sql_context.h"

#include <ctype.h>
#include <string.h>

#define LE_SQL_TOKENS_MAX 512
#define LE_SQL_TOKEN_TEXT_MAX 256

enum sql_token_kind {
    SQL_TOKEN_WORD,
    SQL_TOKEN_DOT,
    SQL_TOKEN_SYMBOL
};

typedef struct sql_token {
    enum sql_token_kind kind;
    size_t start;
    size_t end;
    char text[LE_SQL_TOKEN_TEXT_MAX];
} sql_token;

static int identifier_byte(unsigned char value)
{
    return isalnum(value) || value == '_' || value == '$' || value == '#';
}

static char ascii_upper(unsigned char value)
{
    if (value >= 'a' && value <= 'z')
        return (char)(value - 'a' + 'A');
    return (char)value;
}

static int text_equal(const char *left, const char *right)
{
    size_t i;

    if (strlen(left) != strlen(right))
        return 0;
    for (i = 0; left[i] != '\0'; ++i) {
        if (ascii_upper((unsigned char)left[i]) !=
            ascii_upper((unsigned char)right[i]))
            return 0;
    }
    return 1;
}

static void copy_text(char *target, size_t target_size, const char *source)
{
    size_t length = strlen(source);
    if (length >= target_size)
        length = target_size - 1;
    memcpy(target, source, length);
    target[length] = '\0';
}

static int keyword(const char *text)
{
    static const char *const words[] = {
        "ALTER", "ANALYZE", "AS", "BEGIN", "BY", "CASE", "COLUMN",
        "COMMENT", "COMMIT", "CONNECT", "CREATE", "DELETE", "DESC",
        "DESCRIBE", "DISTINCT", "DROP", "ELSE", "END", "EXEC",
        "EXECUTE", "FETCH", "FOR", "FROM", "FULL", "GROUP", "HAVING",
        "IN", "INNER", "INSERT", "INTO", "JOIN", "LEFT", "LIKE",
        "LOCK", "MERGE", "NOT", "NULL", "ON", "OPTION", "OR", "ORDER",
        "OUTER", "RETURNING", "RIGHT", "SELECT", "SET", "SPOOL",
        "TABLE", "THEN", "TRUNCATE", "UNION", "UPDATE", "USING", "VALUES",
        "VIEW", "WHEN", "WHERE", "WITH", "AND", "MODEL", "PIVOT",
        "UNPIVOT", "START", "PRIOR", "QUALIFY", "WINDOW"
    };
    size_t i;

    for (i = 0; i < sizeof(words) / sizeof(words[0]); ++i) {
        if (text_equal(text, words[i]))
            return 1;
    }
    return 0;
}

static int relation_keyword(const char *text)
{
    return text_equal(text, "FROM") || text_equal(text, "JOIN") ||
           text_equal(text, "UPDATE") || text_equal(text, "INTO") ||
           text_equal(text, "USING") || text_equal(text, "TABLE");
}

static int clause_ending(const char *text)
{
    return text_equal(text, "WHERE") || text_equal(text, "GROUP") ||
           text_equal(text, "ORDER") || text_equal(text, "HAVING") ||
           text_equal(text, "RETURNING") || text_equal(text, "SET") ||
           text_equal(text, "VALUES") || text_equal(text, "ON") ||
           text_equal(text, "UNION") || text_equal(text, "MODEL") ||
           text_equal(text, "PIVOT") || text_equal(text, "UNPIVOT");
}

static int column_clause(const char *text)
{
    return text_equal(text, "SELECT") || text_equal(text, "WHERE") ||
           text_equal(text, "ON") || text_equal(text, "HAVING") ||
           text_equal(text, "GROUP") || text_equal(text, "ORDER") ||
           text_equal(text, "BY") || text_equal(text, "SET") ||
           text_equal(text, "VALUES") || text_equal(text, "RETURNING") ||
           text_equal(text, "AND") || text_equal(text, "OR");
}

static size_t copy_upper(char *target, size_t target_size, const char *source,
                         size_t length)
{
    size_t count = length < target_size - 1 ? length : target_size - 1;
    size_t i;

    for (i = 0; i < count; ++i)
        target[i] = ascii_upper((unsigned char)source[i]);
    target[count] = '\0';
    return count;
}

static size_t tokenize(const char *line, sql_token *tokens, size_t capacity)
{
    size_t length = strlen(line);
    size_t position = 0;
    size_t count = 0;
    int block_comment = 0;

    while (position < length && count < capacity) {
        unsigned char value = (unsigned char)line[position];
        size_t start;

        if (block_comment) {
            if (value == '*' && position + 1 < length && line[position + 1] == '/') {
                block_comment = 0;
                position += 2;
            } else {
                ++position;
            }
            continue;
        }
        if (value == '-' && position + 1 < length && line[position + 1] == '-')
            break;
        if (value == '/' && position + 1 < length && line[position + 1] == '*') {
            block_comment = 1;
            position += 2;
            continue;
        }
        if (value == '\'' || value == '"') {
            unsigned char quote = value;
            start = position++;
            while (position < length) {
                if ((unsigned char)line[position] == quote) {
                    if (position + 1 < length && line[position + 1] == (char)quote) {
                        position += 2;
                        continue;
                    }
                    ++position;
                    break;
                }
                ++position;
            }
            if (quote == '"') {
                tokens[count].kind = SQL_TOKEN_WORD;
                tokens[count].start = start;
                tokens[count].end = position;
                if (position > start + 1 && line[position - 1] == '"')
                    copy_upper(tokens[count].text, sizeof(tokens[count].text),
                               line + start + 1, position - start - 2);
                else
                    tokens[count].text[0] = '\0';
                ++count;
            }
            continue;
        }
        if (isspace(value)) {
            ++position;
            continue;
        }
        if (identifier_byte(value)) {
            start = position++;
            while (position < length &&
                   identifier_byte((unsigned char)line[position]))
                ++position;
            tokens[count].kind = SQL_TOKEN_WORD;
            tokens[count].start = start;
            tokens[count].end = position;
            copy_upper(tokens[count].text, sizeof(tokens[count].text),
                       line + start, position - start);
            ++count;
            continue;
        }
        tokens[count].kind = value == '.' ? SQL_TOKEN_DOT : SQL_TOKEN_SYMBOL;
        tokens[count].start = position;
        tokens[count].end = position + 1;
        tokens[count].text[0] = (char)value;
        tokens[count].text[1] = '\0';
        ++count;
        ++position;
    }
    return count;
}

static int inside_ignored_text(const char *line, size_t cursor)
{
    size_t position = 0;
    int block_comment = 0;
    int single_quote = 0;

    while (position < cursor) {
        unsigned char value = (unsigned char)line[position];
        if (block_comment) {
            if (value == '*' && position + 1 < cursor && line[position + 1] == '/') {
                block_comment = 0;
                position += 2;
            } else {
                ++position;
            }
            continue;
        }
        if (single_quote) {
            if (value == '\'' && position + 1 < cursor && line[position + 1] == '\'')
                position += 2;
            else if (value == '\'') {
                single_quote = 0;
                ++position;
            } else {
                ++position;
            }
            continue;
        }
        if (value == '-' && position + 1 < cursor && line[position + 1] == '-')
            return 1;
        if (value == '/' && position + 1 < cursor && line[position + 1] == '*') {
            block_comment = 1;
            position += 2;
        } else if (value == '\'') {
            single_quote = 1;
            ++position;
        } else {
            ++position;
        }
    }
    return block_comment || single_quote;
}

static void current_components(const char *line, size_t cursor,
                               le_sql_context *context)
{
    size_t start = cursor;
    size_t component_start = cursor;
    size_t position;
    size_t count = 0;
    size_t part_start;

    while (start != 0 && identifier_byte((unsigned char)line[start - 1]))
        --start;
    component_start = start;
    while (component_start != 0 && line[component_start - 1] == '.') {
        size_t owner_start = component_start - 1;
        while (owner_start != 0 &&
               identifier_byte((unsigned char)line[owner_start - 1]))
            --owner_start;
        component_start = owner_start;
    }
    context->component_start = start;
    context->qualified_start = component_start;
    part_start = component_start;
    position = component_start;
    while (position <= cursor && count < LE_CONTEXT_COMPONENTS_MAX) {
        if (position == cursor || line[position] == '.') {
            size_t part_length = position - part_start;
            if (part_length >= LE_CONTEXT_NAME_MAX)
                part_length = LE_CONTEXT_NAME_MAX - 1;
            memcpy(context->components[count], line + part_start, part_length);
            context->components[count][part_length] = '\0';
            ++count;
            part_start = position + 1;
        }
        ++position;
    }
    if (part_start <= cursor && count < LE_CONTEXT_COMPONENTS_MAX) {
        size_t part_length = cursor - part_start;
        if (part_length >= LE_CONTEXT_NAME_MAX)
            part_length = LE_CONTEXT_NAME_MAX - 1;
        memcpy(context->components[count], line + part_start, part_length);
        context->components[count][part_length] = '\0';
        ++count;
    }
    context->component_count = count == 0 ? 1 : count;
    if (count == 0)
        context->components[0][0] = '\0';
}

static int token_is_word(const sql_token *token)
{
    return token->kind == SQL_TOKEN_WORD && token->text[0] != '\0';
}

static void add_relation(le_sql_context *context, const char *owner,
                         const char *name, const char *alias)
{
    le_context_relation *relation;
    size_t i;

    if (context->relation_count == LE_CONTEXT_RELATIONS_MAX || name[0] == '\0')
        return;
    for (i = 0; i < context->relation_count; ++i) {
        relation = &context->relations[i];
        if (text_equal(relation->owner, owner) &&
            text_equal(relation->name, name) && text_equal(relation->alias, alias))
            return;
    }
    relation = &context->relations[context->relation_count++];
    copy_text(relation->owner, sizeof(relation->owner), owner);
    copy_text(relation->name, sizeof(relation->name), name);
    copy_text(relation->alias, sizeof(relation->alias), alias);
}

static size_t parse_relation_name(const sql_token *tokens, size_t count,
                                  size_t index, char *owner, size_t owner_size,
                                  char *name, size_t name_size)
{
    size_t next = index;

    owner[0] = '\0';
    name[0] = '\0';
    if (next >= count || !token_is_word(&tokens[next]) || keyword(tokens[next].text))
        return index;
    copy_text(name, name_size, tokens[next].text);
    ++next;
    if (next + 1 < count && tokens[next].kind == SQL_TOKEN_DOT &&
        token_is_word(&tokens[next + 1])) {
        copy_text(owner, owner_size, name);
        copy_text(name, name_size, tokens[next + 1].text);
        next += 2;
    }
    return next;
}

static void collect_relations(const sql_token *tokens, size_t count,
                              le_sql_context *context)
{
    size_t i = 0;
    int in_relation_list = 0;

    while (i < count) {
        char owner[LE_CONTEXT_NAME_MAX];
        char name[LE_CONTEXT_NAME_MAX];
        char alias[LE_CONTEXT_NAME_MAX];
        size_t next;

        if (token_is_word(&tokens[i]) && relation_keyword(tokens[i].text)) {
            in_relation_list = 1;
            ++i;
            continue;
        }
        if (!in_relation_list) {
            ++i;
            continue;
        }
        if (tokens[i].kind == SQL_TOKEN_SYMBOL && tokens[i].text[0] == ',') {
            ++i;
            continue;
        }
        if (token_is_word(&tokens[i]) && clause_ending(tokens[i].text)) {
            in_relation_list = 0;
            continue;
        }
        if (tokens[i].kind != SQL_TOKEN_WORD) {
            /* A subquery or expression is not a safely resolvable relation. */
            in_relation_list = 0;
            ++i;
            continue;
        }
        next = parse_relation_name(tokens, count, i, owner, sizeof(owner),
                                   name, sizeof(name));
        if (next == i) {
            ++i;
            continue;
        }
        alias[0] = '\0';
        if (next < count && token_is_word(&tokens[next]) &&
            text_equal(tokens[next].text, "AS")) {
            if (next + 1 < count && token_is_word(&tokens[next + 1]) &&
                !keyword(tokens[next + 1].text)) {
                copy_text(alias, sizeof(alias), tokens[next + 1].text);
                next += 2;
            }
        } else if (next < count && token_is_word(&tokens[next]) &&
                   !keyword(tokens[next].text)) {
            copy_text(alias, sizeof(alias), tokens[next].text);
            ++next;
        }
        add_relation(context, owner, name, alias);
        i = next;
    }
}

static void classify_context(const sql_token *tokens, size_t count,
                             le_sql_context *context)
{
    size_t i;
    int mode = 0; /* 0 unknown, 1 relation/object, 2 column */
    size_t last_before = 0;
    int have_last_before = 0;

    for (i = 0; i < count; ++i) {
        if (tokens[i].end <= context->qualified_start) {
            last_before = i;
            have_last_before = 1;
        }
        if (tokens[i].start >= context->qualified_start)
            break;
        if (tokens[i].kind == SQL_TOKEN_SYMBOL && tokens[i].text[0] == ';') {
            mode = 0;
        } else if (token_is_word(&tokens[i])) {
            if (relation_keyword(tokens[i].text))
                mode = 1;
            else if (column_clause(tokens[i].text))
                mode = 2;
            if (clause_ending(tokens[i].text) && !column_clause(tokens[i].text))
                mode = 0;
        }
    }
    if (have_last_before && token_is_word(&tokens[last_before])) {
        if (relation_keyword(tokens[last_before].text) ||
            text_equal(tokens[last_before].text, "DESC") ||
            text_equal(tokens[last_before].text, "DESCRIBE"))
            mode = 1;
        else if (column_clause(tokens[last_before].text))
            mode = 2;
    }
    if (have_last_before && tokens[last_before].kind == SQL_TOKEN_SYMBOL &&
        tokens[last_before].text[0] == ',') {
        mode = 2;
        for (i = last_before; i != 0;) {
            --i;
            if (token_is_word(&tokens[i]) && relation_keyword(tokens[i].text)) {
                mode = 1;
                break;
            }
            if (token_is_word(&tokens[i]) && clause_ending(tokens[i].text))
                break;
        }
    }
    if (context->component_count > 1 && mode != 1)
        mode = 2;
    context->kind = mode == 1 ? LE_CONTEXT_OBJECT
                              : mode == 2 ? LE_CONTEXT_COLUMN
                                          : LE_CONTEXT_GENERIC;
}

int le_sql_context_analyze(const char *line, size_t cursor,
                           le_sql_context *context)
{
    sql_token tokens[LE_SQL_TOKENS_MAX];
    size_t count;

    if (line == NULL || context == NULL || cursor > strlen(line))
        return 0;
    memset(context, 0, sizeof(*context));
    current_components(line, cursor, context);
    if (inside_ignored_text(line, cursor)) {
        context->disabled = 1;
        context->kind = LE_CONTEXT_GENERIC;
        return 1;
    }
    count = tokenize(line, tokens, LE_SQL_TOKENS_MAX);
    collect_relations(tokens, count, context);
    classify_context(tokens, count, context);
    return 1;
}
