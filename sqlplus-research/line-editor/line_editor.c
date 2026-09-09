#define _GNU_SOURCE

#include "line_editor.h"

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <poll.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <termios.h>
#include <unistd.h>

#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

#define LE_DYNAMIC_WORD_MAX 256
#define LE_DYNAMIC_WORDS_MAX 4096

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

static int is_identifier_byte(unsigned char value)
{
    return isalnum(value) || value == '_' || value == '$' || value == '#';
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

static char dynamic_words[LE_DYNAMIC_WORDS_MAX][LE_DYNAMIC_WORD_MAX];
static size_t dynamic_word_count;

struct dynamic_cache_stamp {
    int valid;
    dev_t device;
    ino_t inode;
    off_t size;
    time_t modified;
    char path[PATH_MAX];
};

static struct dynamic_cache_stamp dynamic_cache_stamp;

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

static int completion_word_start(unsigned char value)
{
    return (value >= 'A' && value <= 'Z') ||
           (value >= 'a' && value <= 'z') || value == '_';
}

static void add_dynamic_word(const char *word, size_t length)
{
    char candidate[LE_DYNAMIC_WORD_MAX];
    size_t start = 0;
    size_t end = length;
    size_t i;

    while (start < end && (word[start] == ' ' || word[start] == '\t'))
        ++start;
    while (end > start && (word[end - 1] == ' ' || word[end - 1] == '\t'))
        --end;
    length = end - start;
    if (length == 0 || length >= LE_DYNAMIC_WORD_MAX ||
        !completion_word_start((unsigned char)word[start]))
        return;
    for (i = 0; i < length; ++i) {
        if (!is_identifier_byte((unsigned char)word[start + i]))
            return;
    }
    memcpy(candidate, word + start, length);
    candidate[length] = '\0';
    for (i = 0; i < dynamic_word_count; ++i) {
        if (completion_word_equal(dynamic_words[i], candidate))
            return;
    }
    if (dynamic_word_count == LE_DYNAMIC_WORDS_MAX)
        return;
    memcpy(dynamic_words[dynamic_word_count], candidate, length + 1);
    ++dynamic_word_count;
}

static int dynamic_stamp_matches(const char *path, const struct stat *status)
{
    return dynamic_cache_stamp.valid &&
           strcmp(dynamic_cache_stamp.path, path) == 0 &&
           dynamic_cache_stamp.device == status->st_dev &&
           dynamic_cache_stamp.inode == status->st_ino &&
           dynamic_cache_stamp.size == status->st_size &&
           dynamic_cache_stamp.modified == status->st_mtime;
}

static void load_dynamic_words(void)
{
    const char *path = getenv("SQLPLUS_LFIRD_COMPLETION_FILE");
    struct stat status;
    char word[LE_DYNAMIC_WORD_MAX];
    unsigned char buffer[4096];
    size_t word_length = 0;
    int overflow = 0;
    int fd;

    if (path == NULL || path[0] == '\0') {
        dynamic_word_count = 0;
        memset(&dynamic_cache_stamp, 0, sizeof(dynamic_cache_stamp));
        return;
    }
    if (strlen(path) >= sizeof(dynamic_cache_stamp.path) ||
        stat(path, &status) != 0 || !S_ISREG(status.st_mode)) {
        dynamic_word_count = 0;
        memset(&dynamic_cache_stamp, 0, sizeof(dynamic_cache_stamp));
        strncpy(dynamic_cache_stamp.path, path,
                sizeof(dynamic_cache_stamp.path) - 1);
        return;
    }
    if (dynamic_stamp_matches(path, &status))
        return;

    dynamic_word_count = 0;
    fd = open(path, O_RDONLY);
    if (fd >= 0) {
        for (;;) {
            ssize_t count = read(fd, buffer, sizeof(buffer));
            ssize_t i;

            if (count == 0)
                break;
            if (count < 0) {
                if (errno == EINTR)
                    continue;
                break;
            }
            for (i = 0; i < count; ++i) {
                unsigned char value = buffer[i];
                if (value == '\n' || value == '\r') {
                    if (!overflow)
                        add_dynamic_word(word, word_length);
                    word_length = 0;
                    overflow = 0;
                } else if (!overflow) {
                    if (word_length + 1 < sizeof(word))
                        word[word_length++] = (char)value;
                    else
                        overflow = 1;
                }
            }
        }
        if (!overflow && word_length != 0)
            add_dynamic_word(word, word_length);
        close(fd);
    }

    dynamic_cache_stamp.valid = fd >= 0;
    dynamic_cache_stamp.device = status.st_dev;
    dynamic_cache_stamp.inode = status.st_ino;
    dynamic_cache_stamp.size = status.st_size;
    dynamic_cache_stamp.modified = status.st_mtime;
    strncpy(dynamic_cache_stamp.path, path,
            sizeof(dynamic_cache_stamp.path) - 1);
    dynamic_cache_stamp.path[sizeof(dynamic_cache_stamp.path) - 1] = '\0';
}

static int word_has_prefix(const char *word, const char *line,
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

static void consider_completion(const char *word, const char *line,
                                size_t start, size_t cursor,
                                const char **match, size_t *match_count)
{
    if (!word_has_prefix(word, line, start, cursor))
        return;
    if (*match == NULL) {
        *match = word;
        *match_count = 1;
    } else if (!completion_word_equal(*match, word)) {
        ++*match_count;
    }
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

static int complete_word(char *line, size_t *length, size_t *cursor,
                         size_t capacity, int output_fd, const char *prompt,
                         size_t *old_cursor)
{
    size_t start = *cursor;
    size_t i;
    const char *match = NULL;
    size_t match_count = 0;
    size_t word_length;
    size_t prefix_length;

    while (start != 0 && is_identifier_byte((unsigned char)line[start - 1]))
        --start;
    prefix_length = *cursor - start;
    if (prefix_length == 0) {
        bell(output_fd);
        return 0;
    }

    load_dynamic_words();
    for (i = 0; i < static_word_count(); ++i) {
        consider_completion(static_words[i], line, start, *cursor,
                            &match, &match_count);
    }
    for (i = 0; i < dynamic_word_count; ++i) {
        consider_completion(dynamic_words[i], line, start, *cursor,
                            &match, &match_count);
    }

    if (match_count != 1) {
        if (match_count > 1 && prompt != NULL && old_cursor != NULL) {
            cursor_left(output_fd, *old_cursor);
            write_literal(output_fd, "\033[K\r\n");
            for (i = 0; i < static_word_count(); ++i) {
                if (word_has_prefix(static_words[i], line, start, *cursor)) {
                    write_all(output_fd, static_words[i],
                              strlen(static_words[i]));
                    write_literal(output_fd, "  ");
                }
            }
            for (i = 0; i < dynamic_word_count; ++i) {
                if (word_has_prefix(dynamic_words[i], line, start, *cursor)) {
                    write_all(output_fd, dynamic_words[i],
                              strlen(dynamic_words[i]));
                    write_literal(output_fd, "  ");
                }
            }
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

    word_length = strlen(match);
    if (word_length == prefix_length)
        return 1;
    if (!insert_bytes(line, length, cursor, capacity,
                      match + prefix_length, word_length - prefix_length)) {
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
