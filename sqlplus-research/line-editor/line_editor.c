#define _GNU_SOURCE

#include "completion_cache.h"
#include "line_editor.h"
#include "sql_context.h"

#include <errno.h>
#include <poll.h>
#include <stdlib.h>
#include <string.h>
#include <termios.h>
#include <unistd.h>

static void write_all(int fd, const void *data, size_t length)
{
    const char *p = (const char *)data;

    while (length != 0) {
        ssize_t written = write(fd, p, length);
        if (written > 0) {
            p += (size_t)written;
            length -= (size_t)written;
            continue;
        }
        if (written < 0 && errno == EINTR)
            continue;
        return;
    }
}

static void write_literal(int fd, const char *text)
{
    write_all(fd, text, strlen(text));
}

static void write_csi_number(int fd, size_t number, char command)
{
    char sequence[40];
    size_t pos = 0;
    char digits[24];
    size_t digit_count = 0;

    sequence[pos++] = '\033';
    sequence[pos++] = '[';
    if (number == 0) {
        digits[digit_count++] = '0';
    } else {
        while (number != 0 && digit_count < sizeof(digits)) {
            digits[digit_count++] = (char)('0' + number % 10);
            number /= 10;
        }
    }
    while (digit_count != 0)
        sequence[pos++] = digits[--digit_count];
    sequence[pos++] = command;
    write_all(fd, sequence, pos);
}

static void cursor_left(int fd, size_t count)
{
    if (count != 0)
        write_csi_number(fd, count, 'D');
}

static void bell(int fd)
{
    static const char character = '\a';
    write_all(fd, &character, 1);
}

static int read_byte_timeout(int fd, unsigned char *value, int timeout_ms)
{
    for (;;) {
        struct pollfd poll_fd;
        int ready;
        ssize_t count;

        poll_fd.fd = fd;
        poll_fd.events = POLLIN;
        poll_fd.revents = 0;
        ready = poll(&poll_fd, 1, timeout_ms);
        if (ready == 0)
            return 0;
        if (ready < 0) {
            if (errno == EINTR)
                continue;
            return -1;
        }
        count = read(fd, value, 1);
        if (count == 1)
            return 1;
        if (count == 0)
            return -1;
        if (errno != EINTR)
            return -1;
    }
}

static int ascii_equal_fold(unsigned char left, unsigned char right)
{
    if (left >= 'a' && left <= 'z')
        left = (unsigned char)(left - 'a' + 'A');
    if (right >= 'a' && right <= 'z')
        right = (unsigned char)(right - 'a' + 'A');
    return left == right;
}

static const char *static_words[] = {
    "ALTER", "ANALYZE", "BEGIN", "COLUMN", "COMMIT", "CONNECT",
    "CREATE", "DECLARE", "DELETE", "DESC", "DESCRIBE", "DISCONNECT",
    "DROP", "EDIT", "EXEC", "EXECUTE", "EXIT", "GRANT", "HELP",
    "HISTORY", "HOST", "INSERT", "LIST", "MERGE", "PROMPT", "QUIT",
    "RECOVER", "RENAME", "REVOKE", "ROLLBACK", "SAVEPOINT", "SELECT",
    "SET", "SHOW", "SPOOL", "START", "TRUNCATE", "UNDEFINE", "UPDATE",
    "VARIABLE", "WHENEVER", "WITH", "FROM", "WHERE", "GROUP", "HAVING",
    "ORDER", "BY", "DISTINCT", "JOIN", "LEFT", "RIGHT", "FULL", "INNER",
    "OUTER", "ON", "AS", "AND", "OR", "NOT", "NULL", "IS", "IN",
    "LIKE", "BETWEEN", "UNION", "ALL", "CASE", "WHEN", "THEN", "ELSE",
    "END", "VALUES", "INTO", "RETURNING", "FETCH", "OFFSET", "FOR",
    "PACKAGE", "PROCEDURE", "FUNCTION", "TRIGGER",
    "TABLE", "VIEW", "INDEX", "SEQUENCE", "SYNONYM", "DATABASE",
    "SCHEMA", "PRIVILEGES", "USER", "DUAL"
};

static size_t static_word_count(void)
{
    return sizeof(static_words) / sizeof(static_words[0]);
}

enum {
    LE_COMPLETION_CANDIDATES_MAX = 256,
    LE_RESOLVED_TARGETS_MAX = 128,
    LE_RESOLUTION_VISITED_MAX = 32
};

typedef struct le_completion_candidate {
    const char *display;
    size_t display_length;
    const char *insert;
    size_t insert_length;
    int display_dot;
    int insert_dot;
} le_completion_candidate;

typedef struct le_completion_set {
    le_completion_candidate items[LE_COMPLETION_CANDIDATES_MAX];
    size_t count;
    int overflow;
} le_completion_set;

typedef struct le_resolved_target {
    char owner[LE_CONTEXT_NAME_MAX];
    char name[LE_CONTEXT_NAME_MAX];
} le_resolved_target;

typedef struct le_resolution_state {
    const le_cache *cache;
    le_resolved_target targets[LE_RESOLVED_TARGETS_MAX];
    size_t target_count;
    le_resolved_target visited[LE_RESOLUTION_VISITED_MAX];
    size_t visited_count;
} le_resolution_state;

static le_cache completion_cache;
static int completion_cache_initialized;

static int completion_word_equal(const char *left, const char *right)
{
    size_t left_length = strlen(left);
    size_t right_length = strlen(right);
    size_t i;

    if (left_length != right_length)
        return 0;
    for (i = 0; i < left_length; ++i) {
        if (!ascii_equal_fold((unsigned char)left[i],
                              (unsigned char)right[i]))
            return 0;
    }
    return 1;
}

static int text_has_prefix(const char *word, const char *line,
                           size_t start, size_t cursor)
{
    size_t word_length = strlen(word);
    size_t prefix_length = cursor - start;
    size_t i;

    if (word_length < prefix_length)
        return 0;
    for (i = 0; i < prefix_length; ++i) {
        if (!ascii_equal_fold((unsigned char)word[i],
                              (unsigned char)line[start + i]))
            return 0;
    }
    return 1;
}

static void completion_set_add(le_completion_set *set, const char *display,
                               size_t display_length, const char *insert,
                               size_t insert_length, int display_dot,
                               int insert_dot)
{
    size_t i;

    if (display == NULL || display_length == 0)
        return;
    for (i = 0; i < set->count; ++i) {
        if (set->items[i].display_length == display_length &&
            set->items[i].display_dot == display_dot &&
            completion_word_equal(set->items[i].display, display))
            return;
    }
    if (set->count == LE_COMPLETION_CANDIDATES_MAX) {
        set->overflow = 1;
        return;
    }
    set->items[set->count].display = display;
    set->items[set->count].display_length = display_length;
    set->items[set->count].insert = insert;
    set->items[set->count].insert_length = insert_length;
    set->items[set->count].display_dot = display_dot;
    set->items[set->count].insert_dot = insert_dot;
    ++set->count;
}

static int add_matching_candidate(le_completion_set *set, const char *word,
                                  const char *line, size_t start, size_t cursor,
                                  int display_dot, int insert_dot)
{
    size_t prefix_length = cursor - start;
    size_t word_length = strlen(word);

    if (!text_has_prefix(word, line, start, cursor))
        return 0;
    completion_set_add(set, word, word_length, word + prefix_length,
                       word_length - prefix_length, display_dot, insert_dot);
    return 1;
}

static int insert_bytes(char *line, size_t *length, size_t *cursor,
                        size_t capacity, const char *bytes, size_t count)
{
    if (count > capacity - 1 - *length)
        return 0;
    memmove(line + *cursor + count, line + *cursor, *length - *cursor);
    memcpy(line + *cursor, bytes, count);
    *length += count;
    *cursor += count;
    line[*length] = '\0';
    return 1;
}

static int object_type_is_completable(const char *type)
{
    return completion_word_equal(type, "TABLE") ||
           completion_word_equal(type, "VIEW") ||
           completion_word_equal(type, "MATERIALIZED VIEW") ||
           completion_word_equal(type, "SEQUENCE");
}

static void ensure_completion_cache(void)
{
    if (!completion_cache_initialized) {
        le_cache_init(&completion_cache);
        completion_cache_initialized = 1;
    }
    le_cache_ensure(&completion_cache,
                    getenv("SQLPLUS_LFIRD_COMPLETION_FILE"));
}

static void add_legacy_words(le_completion_set *set, const le_sql_context *context,
                             const char *line, size_t cursor)
{
    size_t i;

    for (i = 0; i < completion_cache.word_count; ++i)
        add_matching_candidate(set,
                               le_cache_text(&completion_cache,
                                             completion_cache.words[i].name),
                               line, context->component_start, cursor, 0, 0);
}

static void add_generic_objects(le_completion_set *set,
                                const le_sql_context *context,
                                const char *line, size_t cursor)
{
    size_t i;

    for (i = 0; i < completion_cache.object_count; ++i) {
        const le_cache_object *object = &completion_cache.objects[i];
        if (object_type_is_completable(
                le_cache_text(&completion_cache, object->type)))
            add_matching_candidate(
                set, le_cache_text(&completion_cache, object->name), line,
                context->component_start, cursor, 0, 0);
    }
    for (i = 0; i < completion_cache.synonym_count; ++i) {
        const le_cache_synonym *synonym = &completion_cache.synonyms[i];
        add_matching_candidate(
            set, le_cache_text(&completion_cache, synonym->name), line,
            context->component_start, cursor, 0, 0);
    }
}

static void add_static_candidates(le_completion_set *set,
                                  const le_sql_context *context,
                                  const char *line, size_t cursor)
{
    size_t i;

    for (i = 0; i < static_word_count(); ++i)
        add_matching_candidate(set, static_words[i], line,
                               context->component_start, cursor, 0, 0);
}

static void add_object_candidates(le_completion_set *set,
                                  const le_sql_context *context,
                                  const char *line, size_t cursor)
{
    const char *owner = NULL;
    size_t i;
    int structured_match = 0;

    if (context->component_count == 2)
        owner = context->components[0];
    if (context->component_count <= 2) {
        for (i = 0; i < completion_cache.object_count; ++i) {
            const le_cache_object *object = &completion_cache.objects[i];
            const char *object_owner =
                le_cache_text(&completion_cache, object->owner);
            if (!object_type_is_completable(
                    le_cache_text(&completion_cache, object->type)) ||
                (owner != NULL && !completion_word_equal(object_owner, owner)))
                continue;
            if (add_matching_candidate(
                    set, le_cache_text(&completion_cache, object->name), line,
                    context->component_start, cursor, 0, 0))
                structured_match = 1;
        }
        for (i = 0; i < completion_cache.synonym_count; ++i) {
            const le_cache_synonym *synonym = &completion_cache.synonyms[i];
            const char *synonym_owner =
                le_cache_text(&completion_cache, synonym->owner);
            if (owner != NULL && !completion_word_equal(synonym_owner, owner))
                continue;
            if (add_matching_candidate(
                    set, le_cache_text(&completion_cache, synonym->name), line,
                    context->component_start, cursor, 0, 0))
                structured_match = 1;
        }
    }

    /* If the first component is an owner, completing it adds the dot. */
    if (context->component_count == 1 && !structured_match) {
        for (i = 0; i < completion_cache.object_count; ++i) {
            const char *object_owner = le_cache_text(
                &completion_cache, completion_cache.objects[i].owner);
            add_matching_candidate(set, object_owner, line,
                                   context->component_start, cursor, 1, 1);
        }
        for (i = 0; i < completion_cache.synonym_count; ++i) {
            const char *synonym_owner = le_cache_text(
                &completion_cache, completion_cache.synonyms[i].owner);
            add_matching_candidate(set, synonym_owner, line,
                                   context->component_start, cursor, 1, 1);
        }
    }

    add_legacy_words(set, context, line, cursor);
    /* DUAL is useful even when the cache contains no object row for it. */
    if (context->component_count == 1)
        add_matching_candidate(set, "DUAL", line, context->component_start,
                               cursor, 0, 0);
}

static int target_equal(const le_resolved_target *target, const char *owner,
                        const char *name)
{
    return completion_word_equal(target->owner, owner) &&
           completion_word_equal(target->name, name);
}

static void add_target(le_resolution_state *state, const char *owner,
                       const char *name)
{
    size_t i;

    if (owner[0] == '\0' || name[0] == '\0')
        return;
    for (i = 0; i < state->target_count; ++i) {
        if (target_equal(&state->targets[i], owner, name))
            return;
    }
    if (state->target_count == LE_RESOLVED_TARGETS_MAX)
        return;
    strncpy(state->targets[state->target_count].owner, owner,
            sizeof(state->targets[0].owner) - 1);
    state->targets[state->target_count].owner[
        sizeof(state->targets[0].owner) - 1] = '\0';
    strncpy(state->targets[state->target_count].name, name,
            sizeof(state->targets[0].name) - 1);
    state->targets[state->target_count].name[
        sizeof(state->targets[0].name) - 1] = '\0';
    ++state->target_count;
}

static int visited_target(const le_resolution_state *state, const char *owner,
                          const char *name)
{
    size_t i;

    for (i = 0; i < state->visited_count; ++i) {
        if (target_equal(&state->visited[i], owner, name))
            return 1;
    }
    return 0;
}

static void resolve_exact(le_resolution_state *state, const char *owner,
                          const char *name, size_t depth)
{
    const le_cache *cache = state->cache;
    size_t i;

    if (owner[0] == '\0' || name[0] == '\0' ||
        depth > LE_RESOLUTION_VISITED_MAX || visited_target(state, owner, name))
        return;
    if (state->visited_count == LE_RESOLUTION_VISITED_MAX)
        return;
    strncpy(state->visited[state->visited_count].owner, owner,
            sizeof(state->visited[0].owner) - 1);
    state->visited[state->visited_count].owner[
        sizeof(state->visited[0].owner) - 1] = '\0';
    strncpy(state->visited[state->visited_count].name, name,
            sizeof(state->visited[0].name) - 1);
    state->visited[state->visited_count].name[
        sizeof(state->visited[0].name) - 1] = '\0';
    ++state->visited_count;

    for (i = 0; i < cache->object_count; ++i) {
        const le_cache_object *object = &cache->objects[i];
        if (completion_word_equal(le_cache_text(cache, object->owner), owner) &&
            completion_word_equal(le_cache_text(cache, object->name), name) &&
            object_type_is_completable(le_cache_text(cache, object->type)) &&
            !completion_word_equal(le_cache_text(cache, object->type),
                                   "SEQUENCE")) {
            add_target(state, owner, name);
        }
    }
    for (i = 0; i < cache->synonym_count; ++i) {
        const le_cache_synonym *synonym = &cache->synonyms[i];
        const char *synonym_owner = le_cache_text(cache, synonym->owner);
        const char *synonym_name = le_cache_text(cache, synonym->name);
        const char *table_owner = le_cache_text(cache, synonym->table_owner);
        const char *table_name = le_cache_text(cache, synonym->table_name);
        if (!completion_word_equal(synonym_owner, owner) ||
            !completion_word_equal(synonym_name, name) ||
            le_cache_text(cache, synonym->db_link)[0] != '\0')
            continue;
        add_target(state, table_owner, table_name);
        if (depth < LE_RESOLUTION_VISITED_MAX)
            resolve_exact(state, table_owner, table_name, depth + 1);
    }
}

static void resolve_relation(le_resolution_state *state, const char *owner,
                             const char *name)
{
    const le_cache *cache = state->cache;
    const char *current_schema = le_cache_current_schema(cache);
    size_t before = state->target_count;
    size_t i;

    if (owner[0] != '\0') {
        resolve_exact(state, owner, name, 0);
        return;
    }
    if (current_schema[0] != '\0')
        resolve_exact(state, current_schema, name, 0);
    if (state->target_count != before)
        return;
    resolve_exact(state, "PUBLIC", name, 0);
    if (state->target_count != before)
        return;
    for (i = 0; i < cache->object_count; ++i) {
        const le_cache_object *object = &cache->objects[i];
        if (completion_word_equal(le_cache_text(cache, object->name), name))
            resolve_exact(state, le_cache_text(cache, object->owner), name, 0);
    }
    for (i = 0; i < cache->synonym_count; ++i) {
        const le_cache_synonym *synonym = &cache->synonyms[i];
        if (completion_word_equal(le_cache_text(cache, synonym->name), name))
            resolve_exact(state, le_cache_text(cache, synonym->owner), name, 0);
    }
}

static int relation_qualifier_matches(const le_context_relation *relation,
                                      const char *qualifier)
{
    return completion_word_equal(relation->alias, qualifier) ||
           (relation->alias[0] == '\0' &&
            completion_word_equal(relation->name, qualifier));
}

static void add_columns_for_target(le_completion_set *set,
                                   const le_resolved_target *target,
                                   const le_sql_context *context,
                                   const char *line, size_t cursor)
{
    size_t i;

    for (i = 0; i < completion_cache.column_count; ++i) {
        const le_cache_column *column = &completion_cache.columns[i];
        if (!completion_word_equal(
                le_cache_text(&completion_cache, column->owner),
                target->owner) ||
            !completion_word_equal(
                le_cache_text(&completion_cache, column->table_name),
                target->name))
            continue;
        add_matching_candidate(
            set, le_cache_text(&completion_cache, column->name), line,
            context->component_start, cursor, 0, 0);
    }
}

static void add_column_candidates(le_completion_set *set,
                                  const le_sql_context *context,
                                  const char *line, size_t cursor)
{
    le_resolution_state state;
    size_t i;
    int relation_matched = 0;

    memset(&state, 0, sizeof(state));
    state.cache = &completion_cache;
    if (context->component_count == 1) {
        for (i = 0; i < context->relation_count; ++i)
            resolve_relation(&state, context->relations[i].owner,
                             context->relations[i].name);
    } else if (context->component_count == 2) {
        for (i = 0; i < context->relation_count; ++i) {
            if (relation_qualifier_matches(&context->relations[i],
                                           context->components[0])) {
                resolve_relation(&state, context->relations[i].owner,
                                 context->relations[i].name);
                relation_matched = 1;
            }
        }
        if (!relation_matched)
            resolve_relation(&state, "", context->components[0]);
    } else if (context->component_count == 3) {
        resolve_relation(&state, context->components[0],
                         context->components[1]);
    }
    for (i = 0; i < state.target_count; ++i)
        add_columns_for_target(set, &state.targets[i], context, line, cursor);
}

static void write_candidate(int output_fd,
                            const le_completion_candidate *candidate)
{
    write_all(output_fd, candidate->display, candidate->display_length);
    if (candidate->display_dot)
        write_literal(output_fd, ".");
    write_literal(output_fd, "  ");
}

static int complete_word(char *line, size_t *length, size_t *cursor,
                         size_t capacity, int output_fd, const char *prompt,
                         size_t *old_cursor)
{
    le_sql_context context;
    le_completion_set set;
    size_t i;

    memset(&set, 0, sizeof(set));
    if (!le_sql_context_analyze(line, *cursor, &context) ||
        context.disabled ||
        (context.component_start == *cursor && *cursor != 0 &&
         line[*cursor - 1] != ' ' && line[*cursor - 1] != '\t' &&
         line[*cursor - 1] != '.')) {
        bell(output_fd);
        return 0;
    }
    ensure_completion_cache();
    if (context.kind == LE_CONTEXT_OBJECT)
        add_object_candidates(&set, &context, line, *cursor);
    else if (context.kind == LE_CONTEXT_COLUMN)
        add_column_candidates(&set, &context, line, *cursor);
    else {
        add_static_candidates(&set, &context, line, *cursor);
        add_legacy_words(&set, &context, line, *cursor);
        add_generic_objects(&set, &context, line, *cursor);
    }

    if (set.count != 1 || set.overflow) {
        if ((set.count > 1 || set.overflow) && prompt != NULL &&
            old_cursor != NULL) {
            cursor_left(output_fd, *old_cursor);
            write_literal(output_fd, "\033[K\r\n");
            for (i = 0; i < set.count; ++i)
                write_candidate(output_fd, &set.items[i]);
            if (set.overflow)
                write_literal(output_fd, "...  ");
            write_literal(output_fd, "\r\n");
            write_all(output_fd, prompt, strlen(prompt));
            write_all(output_fd, line, *length);
            cursor_left(output_fd, *length - *cursor);
            *old_cursor = *cursor;
            return 1;
        }
        bell(output_fd);
        return 0;
    }
    if (set.items[0].insert_length == 0 && !set.items[0].insert_dot)
        return 1;
    if (!insert_bytes(line, length, cursor, capacity,
                      set.items[0].insert, set.items[0].insert_length)) {
        bell(output_fd);
        return 0;
    }
    if (set.items[0].insert_dot &&
        !insert_bytes(line, length, cursor, capacity, ".", 1)) {
        bell(output_fd);
        return 0;
    }
    return 1;
}

static void refresh_line(int output_fd, const char *line, size_t length,
                         size_t cursor, size_t *old_cursor)
{
    /* This preserves an already printed prompt by moving only across the old
     * input bytes.  It intentionally assumes one terminal row; long SQL text
     * needs a full screen-width-aware renderer in a later iteration. */
    cursor_left(output_fd, *old_cursor);
    write_literal(output_fd, "\033[K");
    write_all(output_fd, line, length);
    cursor_left(output_fd, length - cursor);
    *old_cursor = cursor;
}

static void copy_history_line(char *line, size_t *length, size_t *cursor,
                              const char *source)
{
    size_t n = strlen(source);
    if (n >= LE_LINE_MAX)
        n = LE_LINE_MAX - 1;
    memcpy(line, source, n);
    line[n] = '\0';
    *length = n;
    *cursor = n;
}

static void history_add(le_history *history, const char *line, size_t length)
{
    if (history == NULL || length == 0)
        return;
    if (length >= LE_LINE_MAX)
        length = LE_LINE_MAX - 1;
    if (history->count != 0 &&
        strlen(history->entries[history->count - 1]) == length &&
        memcmp(history->entries[history->count - 1], line, length) == 0)
        return;
    if (history->count == LE_HISTORY_MAX) {
        memmove(history->entries[0], history->entries[1],
                (LE_HISTORY_MAX - 1) * LE_LINE_MAX);
        history->count = LE_HISTORY_MAX - 1;
    }
    memcpy(history->entries[history->count], line, length);
    history->entries[history->count][length] = '\0';
    ++history->count;
}

static int handle_escape(int input_fd, int output_fd, char *line,
                         size_t *length, size_t *cursor, size_t capacity,
                         le_history *history, size_t *history_pos,
                         char *saved_line, size_t *saved_length,
                         int *saved_valid)
{
    unsigned char next;
    int status = read_byte_timeout(input_fd, &next, 30);

    (void)capacity;

    if (status <= 0) {
        bell(output_fd);
        return 0;
    }

    if (next == '[' || next == 'O') {
        unsigned char code;
        status = read_byte_timeout(input_fd, &code, 30);
        if (status <= 0) {
            bell(output_fd);
            return 0;
        }
        if (code == 'A' || code == 'B' || code == 'C' || code == 'D' ||
            code == 'H' || code == 'F') {
            /* xterm also uses ESC O A/B/C/D for cursor keys. */
        } else if (next == '[') {
            unsigned char final = code;
            while (final < 0x40 || final > 0x7e) {
                status = read_byte_timeout(input_fd, &final, 30);
                if (status <= 0) {
                    bell(output_fd);
                    return 0;
                }
            }
            if (final == '~') {
                if (code == '3') {
                    if (*cursor < *length) {
                        memmove(line + *cursor, line + *cursor + 1,
                                *length - *cursor - 1);
                        --*length;
                        line[*length] = '\0';
                    } else {
                        bell(output_fd);
                    }
                } else {
                    bell(output_fd);
                }
                return 0;
            }
            bell(output_fd);
            return 0;
        } else {
            bell(output_fd);
            return 0;
        }

        switch (code) {
        case 'A':
            if (history != NULL && *history_pos != 0) {
                if (*history_pos == history->count) {
                    memcpy(saved_line, line, *length + 1);
                    *saved_length = *length;
                    *saved_valid = 1;
                }
                --*history_pos;
                copy_history_line(line, length, cursor,
                                  history->entries[*history_pos]);
            } else {
                bell(output_fd);
            }
            break;
        case 'B':
            if (history != NULL && *history_pos < history->count) {
                ++*history_pos;
                if (*history_pos == history->count) {
                    if (*saved_valid) {
                        memcpy(line, saved_line, *saved_length + 1);
                        *length = *saved_length;
                        *cursor = *length;
                    } else {
                        line[0] = '\0';
                        *length = 0;
                        *cursor = 0;
                    }
                } else {
                    copy_history_line(line, length, cursor,
                                      history->entries[*history_pos]);
                }
            } else {
                bell(output_fd);
            }
            break;
        case 'C':
            if (*cursor < *length)
                ++*cursor;
            else
                bell(output_fd);
            break;
        case 'D':
            if (*cursor != 0)
                --*cursor;
            else
                bell(output_fd);
            break;
        case 'H':
            *cursor = 0;
            break;
        case 'F':
            *cursor = *length;
            break;
        default:
            bell(output_fd);
            break;
        }
        return 0;
    }

    bell(output_fd);
    return 0;
}

static int read_non_tty_line(int input_fd, char *out, size_t out_cap)
{
    size_t length = 0;
    unsigned char ch;

    if (out_cap < 2)
        return LE_ERROR;
    for (;;) {
        ssize_t count = read(input_fd, &ch, 1);
        if (count == 0) {
            if (length == 0)
                return LE_EOF;
            break;
        }
        if (count < 0) {
            if (errno == EINTR)
                continue;
            return LE_ERROR;
        }
        if (ch == '\n')
            break;
        if (length + 1 >= out_cap)
            return LE_ERROR;
        out[length++] = (char)ch;
    }
    out[length++] = '\n';
    out[length] = '\0';
    return (int)length;
}

void le_history_init(le_history *history)
{
    if (history != NULL)
        memset(history, 0, sizeof(*history));
}

int le_readline(int input_fd, int output_fd, const char *prompt,
                le_history *history, char *out, size_t out_cap)
{
    struct termios saved_termios;
    struct termios raw_termios;
    int raw_enabled = 0;
    char line[LE_LINE_MAX];
    char saved_line[LE_LINE_MAX];
    size_t length = 0;
    size_t cursor = 0;
    size_t old_cursor = 0;
    size_t line_capacity;
    size_t history_pos = history == NULL ? 0 : history->count;
    size_t saved_length = 0;
    int saved_valid = 0;

    if (out == NULL || out_cap < 2)
        return LE_ERROR;
    line_capacity = out_cap - 1;
    if (line_capacity > sizeof(line))
        line_capacity = sizeof(line);
    if (line_capacity < 2)
        return LE_ERROR;
    if (prompt != NULL)
        write_all(output_fd, prompt, strlen(prompt));
    line[0] = '\0';

    if (!isatty(input_fd))
        return read_non_tty_line(input_fd, out, out_cap);

    if (tcgetattr(input_fd, &saved_termios) != 0)
        return LE_ERROR;
    raw_termios = saved_termios;
    cfmakeraw(&raw_termios);
    raw_termios.c_cc[VMIN] = 1;
    raw_termios.c_cc[VTIME] = 0;
    if (tcsetattr(input_fd, TCSANOW, &raw_termios) != 0)
        return LE_ERROR;
    raw_enabled = 1;

    for (;;) {
        unsigned char ch;
        int status = read_byte_timeout(input_fd, &ch, -1);
        if (status < 0) {
            if (raw_enabled)
                tcsetattr(input_fd, TCSANOW, &saved_termios);
            return length == 0 ? LE_EOF : LE_ERROR;
        }
        if (status == 0)
            continue;

        if (ch == '\r' || ch == '\n') {
            if (length + 1 >= out_cap) {
                bell(output_fd);
                continue;
            }
            history_add(history, line, length);
            memcpy(out, line, length);
            out[length++] = '\n';
            out[length] = '\0';
            write_literal(output_fd, "\r\n");
            tcsetattr(input_fd, TCSANOW, &saved_termios);
            return (int)length;
        }
        if (ch == 3) {
            write_literal(output_fd, "^C\r\n");
            tcsetattr(input_fd, TCSANOW, &saved_termios);
            return LE_INTERRUPT;
        }
        if (ch == 4) {
            if (length == 0) {
                write_literal(output_fd, "\r\n");
                tcsetattr(input_fd, TCSANOW, &saved_termios);
                return LE_EOF;
            }
            if (cursor < length) {
                memmove(line + cursor, line + cursor + 1, length - cursor - 1);
                --length;
                line[length] = '\0';
            } else {
                bell(output_fd);
            }
        } else if (ch == 8 || ch == 127) {
            if (cursor != 0) {
                memmove(line + cursor - 1, line + cursor, length - cursor);
                --cursor;
                --length;
                line[length] = '\0';
            } else {
                bell(output_fd);
            }
        } else if (ch == 1) {
            cursor = 0;
        } else if (ch == 5) {
            cursor = length;
        } else if (ch == 11) {
            length = cursor;
            line[length] = '\0';
        } else if (ch == 21) {
            length = 0;
            cursor = 0;
            line[0] = '\0';
        } else if (ch == 23) {
            size_t old_cursor = cursor;
            while (cursor != 0 && line[cursor - 1] == ' ')
                --cursor;
            while (cursor != 0 && line[cursor - 1] != ' ')
                --cursor;
            memmove(line + cursor, line + old_cursor, length - old_cursor);
            length -= old_cursor - cursor;
            line[length] = '\0';
        } else if (ch == '\t') {
            complete_word(line, &length, &cursor, line_capacity, output_fd,
                          prompt, &old_cursor);
        } else if (ch == 27) {
            handle_escape(input_fd, output_fd, line, &length, &cursor,
                          sizeof(line), history, &history_pos, saved_line,
                          &saved_length, &saved_valid);
        } else if (ch >= 0x20 && ch != 0x7f) {
            if (!insert_bytes(line, &length, &cursor, line_capacity,
                              (const char *)&ch, 1))
                bell(output_fd);
            history_pos = history == NULL ? 0 : history->count;
            saved_valid = 0;
        }
        refresh_line(output_fd, line, length, cursor, &old_cursor);
    }
}
