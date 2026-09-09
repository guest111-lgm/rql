#define _GNU_SOURCE

#include "completion_cache.h"

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

#define LE_CACHE_LINE_MAX 4096
#define LE_CACHE_FIELD_MAX 512
#define LE_CACHE_FIELDS_MAX 8
#define LE_CACHE_OBJECTS_MAX 32768
#define LE_CACHE_SYNONYMS_MAX 32768
#define LE_CACHE_COLUMNS_MAX 131072
#define LE_CACHE_WORDS_MAX 16384

#define LE_CACHE_NONE ((size_t)-1)

static int ascii_equal_fold(unsigned char left, unsigned char right)
{
    if (left >= 'a' && left <= 'z')
        left = (unsigned char)(left - 'a' + 'A');
    if (right >= 'a' && right <= 'z')
        right = (unsigned char)(right - 'a' + 'A');
    return left == right;
}

static int text_equal_fold(const char *left, const char *right)
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

static void cache_zero(le_cache *cache)
{
    memset(cache, 0, sizeof(*cache));
    cache->current_schema = LE_CACHE_NONE;
}

void le_cache_init(le_cache *cache)
{
    if (cache != NULL)
        cache_zero(cache);
}

void le_cache_release(le_cache *cache)
{
    if (cache == NULL)
        return;
    free(cache->strings);
    free(cache->objects);
    free(cache->synonyms);
    free(cache->columns);
    free(cache->words);
    cache_zero(cache);
}

const char *le_cache_text(const le_cache *cache, size_t offset)
{
    if (cache == NULL || offset == LE_CACHE_NONE ||
        offset >= cache->strings_length)
        return "";
    return cache->strings + offset;
}

const char *le_cache_current_schema(const le_cache *cache)
{
    if (cache == NULL || !cache->current_schema_valid)
        return "";
    return le_cache_text(cache, cache->current_schema);
}

static int grow_array(void **array, size_t *capacity, size_t element_size,
                      size_t count, size_t maximum)
{
    size_t new_capacity;
    void *new_array;

    if (count >= maximum)
        return 0;
    if (*capacity != 0 && count < *capacity)
        return 1;
    new_capacity = *capacity == 0 ? 64 : *capacity * 2;
    if (new_capacity < count + 1)
        new_capacity = count + 1;
    if (new_capacity > maximum)
        new_capacity = maximum;
    if (new_capacity > SIZE_MAX / element_size)
        return 0;
    new_array = realloc(*array, new_capacity * element_size);
    if (new_array == NULL)
        return 0;
    *array = new_array;
    *capacity = new_capacity;
    return 1;
}

static size_t add_string(le_cache *cache, const char *text)
{
    size_t length;
    size_t needed;
    size_t new_capacity;
    char *new_strings;
    size_t offset;

    if (cache == NULL || text == NULL)
        return LE_CACHE_NONE;
    length = strlen(text);
    if (length > LE_CACHE_FIELD_MAX - 1)
        return LE_CACHE_NONE;
    if (cache->strings_length > SIZE_MAX - length - 1)
        return LE_CACHE_NONE;
    needed = cache->strings_length + length + 1;
    if (needed > cache->strings_capacity) {
        new_capacity = cache->strings_capacity == 0 ? 4096
                                                     : cache->strings_capacity;
        while (new_capacity < needed) {
            if (new_capacity > SIZE_MAX / 2)
                return LE_CACHE_NONE;
            new_capacity *= 2;
        }
        new_strings = realloc(cache->strings, new_capacity);
        if (new_strings == NULL)
            return LE_CACHE_NONE;
        cache->strings = new_strings;
        cache->strings_capacity = new_capacity;
    }
    offset = cache->strings_length;
    memcpy(cache->strings + offset, text, length + 1);
    cache->strings_length = needed;
    return offset;
}

static int add_word(le_cache *cache, const char *word)
{
    size_t i;
    size_t name;

    if (word == NULL || word[0] == '\0')
        return 1;
    for (i = 0; i < cache->word_count; ++i) {
        if (text_equal_fold(le_cache_text(cache, cache->words[i].name), word))
            return 1;
    }
    if (!grow_array((void **)&cache->words, &cache->word_capacity,
                    sizeof(*cache->words), cache->word_count,
                    LE_CACHE_WORDS_MAX))
        return 0;
    name = add_string(cache, word);
    if (name == LE_CACHE_NONE)
        return 0;
    cache->words[cache->word_count++].name = name;
    return 1;
}

static int add_object(le_cache *cache, const char *owner, const char *name,
                      const char *type)
{
    size_t i;
    le_cache_object *object;

    if (owner[0] == '\0' || name[0] == '\0')
        return 1;
    for (i = 0; i < cache->object_count; ++i) {
        object = &cache->objects[i];
        if (text_equal_fold(le_cache_text(cache, object->owner), owner) &&
            text_equal_fold(le_cache_text(cache, object->name), name) &&
            text_equal_fold(le_cache_text(cache, object->type), type))
            return 1;
    }
    if (!grow_array((void **)&cache->objects, &cache->object_capacity,
                    sizeof(*cache->objects), cache->object_count,
                    LE_CACHE_OBJECTS_MAX))
        return 0;
    object = &cache->objects[cache->object_count];
    object->owner = add_string(cache, owner);
    object->name = add_string(cache, name);
    object->type = add_string(cache, type);
    if (object->owner == LE_CACHE_NONE || object->name == LE_CACHE_NONE ||
        object->type == LE_CACHE_NONE)
        return 0;
    ++cache->object_count;
    return 1;
}

static int add_synonym(le_cache *cache, const char *owner, const char *name,
                       const char *table_owner, const char *table_name,
                       const char *db_link)
{
    size_t i;
    le_cache_synonym *synonym;

    if (owner[0] == '\0' || name[0] == '\0' || table_owner[0] == '\0' ||
        table_name[0] == '\0')
        return 1;
    for (i = 0; i < cache->synonym_count; ++i) {
        synonym = &cache->synonyms[i];
        if (text_equal_fold(le_cache_text(cache, synonym->owner), owner) &&
            text_equal_fold(le_cache_text(cache, synonym->name), name) &&
            text_equal_fold(le_cache_text(cache, synonym->table_owner),
                            table_owner) &&
            text_equal_fold(le_cache_text(cache, synonym->table_name),
                            table_name) &&
            text_equal_fold(le_cache_text(cache, synonym->db_link), db_link))
            return 1;
    }
    if (!grow_array((void **)&cache->synonyms, &cache->synonym_capacity,
                    sizeof(*cache->synonyms), cache->synonym_count,
                    LE_CACHE_SYNONYMS_MAX))
        return 0;
    synonym = &cache->synonyms[cache->synonym_count];
    synonym->owner = add_string(cache, owner);
    synonym->name = add_string(cache, name);
    synonym->table_owner = add_string(cache, table_owner);
    synonym->table_name = add_string(cache, table_name);
    synonym->db_link = add_string(cache, db_link);
    if (synonym->owner == LE_CACHE_NONE || synonym->name == LE_CACHE_NONE ||
        synonym->table_owner == LE_CACHE_NONE ||
        synonym->table_name == LE_CACHE_NONE ||
        synonym->db_link == LE_CACHE_NONE)
        return 0;
    ++cache->synonym_count;
    return 1;
}

static int add_column(le_cache *cache, const char *owner, const char *table,
                      const char *name, unsigned long column_id)
{
    size_t i;
    le_cache_column *column;

    if (owner[0] == '\0' || table[0] == '\0' || name[0] == '\0')
        return 1;
    for (i = 0; i < cache->column_count; ++i) {
        column = &cache->columns[i];
        if (text_equal_fold(le_cache_text(cache, column->owner), owner) &&
            text_equal_fold(le_cache_text(cache, column->table_name), table) &&
            text_equal_fold(le_cache_text(cache, column->name), name))
            return 1;
    }
    if (!grow_array((void **)&cache->columns, &cache->column_capacity,
                    sizeof(*cache->columns), cache->column_count,
                    LE_CACHE_COLUMNS_MAX))
        return 0;
    column = &cache->columns[cache->column_count];
    column->owner = add_string(cache, owner);
    column->table_name = add_string(cache, table);
    column->name = add_string(cache, name);
    column->column_id = column_id;
    if (column->owner == LE_CACHE_NONE || column->table_name == LE_CACHE_NONE ||
        column->name == LE_CACHE_NONE)
        return 0;
    ++cache->column_count;
    return 1;
}

static int hex_digit(unsigned char value)
{
    if (value >= '0' && value <= '9')
        return value - '0';
    if (value >= 'a' && value <= 'f')
        return value - 'a' + 10;
    if (value >= 'A' && value <= 'F')
        return value - 'A' + 10;
    return -1;
}

static int decode_field(const char *source, char *target, size_t target_size)
{
    size_t source_pos = 0;
    size_t target_pos = 0;

    while (source[source_pos] != '\0') {
        unsigned char value = (unsigned char)source[source_pos++];
        if (value == '\\') {
            int high;
            int low;
            if (source[source_pos] == 't') {
                ++source_pos;
                value = '\t';
            } else if (source[source_pos] == 'n') {
                ++source_pos;
                value = '\n';
            } else if (source[source_pos] == 'r') {
                ++source_pos;
                value = '\r';
            } else if (source[source_pos] == '\\') {
                ++source_pos;
                value = '\\';
            } else if (source[source_pos] == 'x') {
                ++source_pos;
                if (source[source_pos] == '\0' ||
                    source[source_pos + 1] == '\0')
                    return 0;
                high = hex_digit((unsigned char)source[source_pos++]);
                low = hex_digit((unsigned char)source[source_pos++]);
                if (high < 0 || low < 0)
                    return 0;
                value = (unsigned char)((high << 4) | low);
            } else {
                return 0;
            }
        }
        if (value == '\0' || target_pos + 1 >= target_size)
            return 0;
        target[target_pos++] = (char)value;
    }
    target[target_pos] = '\0';
    return 1;
}

static int split_line(const char *line, char fields[][LE_CACHE_FIELD_MAX],
                      size_t *field_count)
{
    char raw[LE_CACHE_FIELD_MAX];
    size_t raw_length = 0;
    size_t count = 0;
    size_t i;

    raw[0] = '\0';
    if (strchr(line, '\t') == NULL) {
        if (strlen(line) >= LE_CACHE_FIELD_MAX ||
            !decode_field(line, fields[0], LE_CACHE_FIELD_MAX))
            return 0;
        *field_count = 1;
        return 1;
    }
    for (i = 0;; ++i) {
        unsigned char value = (unsigned char)line[i];
        if (value == '\t' || value == '\0') {
            if (count == LE_CACHE_FIELDS_MAX || raw_length >= sizeof(raw) ||
                !decode_field(raw, fields[count], LE_CACHE_FIELD_MAX))
                return 0;
            ++count;
            raw_length = 0;
            raw[0] = '\0';
            if (value == '\0')
                break;
        } else {
            if (raw_length + 1 >= sizeof(raw))
                return 0;
            raw[raw_length++] = (char)value;
            raw[raw_length] = '\0';
        }
    }
    *field_count = count;
    return 1;
}

static int parse_unsigned(const char *text, unsigned long *value)
{
    char *end;
    unsigned long parsed;

    if (text[0] == '\0') {
        *value = 0;
        return 1;
    }
    errno = 0;
    parsed = strtoul(text, &end, 10);
    if (errno != 0 || *end != '\0')
        return 0;
    *value = parsed;
    return 1;
}

static int parse_line(le_cache *cache, char *line)
{
    char fields[LE_CACHE_FIELDS_MAX][LE_CACHE_FIELD_MAX];
    size_t field_count;
    size_t line_length;
    unsigned long column_id;

    while (*line == ' ' || *line == '\r')
        ++line;
    if (*line == '\0' || *line == '#')
        return 1;
    line_length = strlen(line);
    if (line_length != 0 && line[line_length - 1] == '\r')
        line[line_length - 1] = '\0';
    if (!split_line(line, fields, &field_count) || field_count == 0)
        return 1;

    /* A bare line is the legacy one-word cache format. */
    if (field_count == 1)
        return add_word(cache, fields[0]);
    if (text_equal_fold(fields[0], "WORD"))
        return field_count >= 2 ? add_word(cache, fields[1]) : 1;
    if (text_equal_fold(fields[0], "META")) {
        if (field_count >= 3 &&
            text_equal_fold(fields[1], "CURRENT_SCHEMA")) {
            size_t schema = add_string(cache, fields[2]);
            if (schema == LE_CACHE_NONE)
                return 0;
            cache->current_schema = schema;
            cache->current_schema_valid = fields[2][0] != '\0';
        }
        return 1;
    }
    if (text_equal_fold(fields[0], "OBJECT")) {
        if (field_count < 4)
            return 1;
        return add_object(cache, fields[1], fields[2], fields[3]);
    }
    if (text_equal_fold(fields[0], "SYNONYM")) {
        if (field_count < 5)
            return 1;
        return add_synonym(cache, fields[1], fields[2], fields[3], fields[4],
                           field_count >= 6 ? fields[5] : "");
    }
    if (text_equal_fold(fields[0], "COLUMN")) {
        if (field_count < 4 || !parse_unsigned(field_count >= 5 ? fields[4] : "",
                                                &column_id))
            return 1;
        return add_column(cache, fields[1], fields[2], fields[3], column_id);
    }
    return 1;
}

static int load_file(le_cache *cache, int fd)
{
    unsigned char buffer[4096];
    char line[LE_CACHE_LINE_MAX];
    size_t line_length = 0;
    int overflow = 0;

    for (;;) {
        ssize_t count = read(fd, buffer, sizeof(buffer));
        ssize_t i;

        if (count == 0)
            break;
        if (count < 0) {
            if (errno == EINTR)
                continue;
            return 0;
        }
        for (i = 0; i < count; ++i) {
            unsigned char value = buffer[i];
            if (value == '\n') {
                if (!overflow) {
                    line[line_length] = '\0';
                    if (!parse_line(cache, line))
                        return 0;
                }
                line_length = 0;
                overflow = 0;
            } else if (!overflow) {
                if (line_length + 1 >= sizeof(line))
                    overflow = 1;
                else
                    line[line_length++] = (char)value;
            }
        }
    }
    if (!overflow && line_length != 0) {
        line[line_length] = '\0';
        if (!parse_line(cache, line))
            return 0;
    }
    return 1;
}

static int stamp_matches(const le_cache *cache, const char *path,
                         const struct stat *status)
{
    return cache->stamp_valid && strcmp(cache->path, path) == 0 &&
           cache->device == (unsigned long long)status->st_dev &&
           cache->inode == (unsigned long long)status->st_ino &&
           cache->size == (long long)status->st_size &&
           cache->modified == (long long)status->st_mtime;
}

int le_cache_ensure(le_cache *cache, const char *path)
{
    struct stat status;
    le_cache loaded;
    int fd;

    if (cache == NULL)
        return 0;
    if (path == NULL || path[0] == '\0' || strlen(path) >= sizeof(cache->path) ||
        stat(path, &status) != 0 || !S_ISREG(status.st_mode)) {
        if (cache->stamp_valid || cache->object_count != 0 ||
            cache->synonym_count != 0 || cache->column_count != 0 ||
            cache->word_count != 0) {
            le_cache_release(cache);
            return 1;
        }
        return 0;
    }
    if (stamp_matches(cache, path, &status))
        return 0;

    fd = open(path, O_RDONLY | O_CLOEXEC);
    if (fd < 0)
        return 0;
    le_cache_init(&loaded);
    if (!load_file(&loaded, fd)) {
        close(fd);
        le_cache_release(&loaded);
        return 0;
    }
    close(fd);
    loaded.stamp_valid = 1;
    loaded.device = (unsigned long long)status.st_dev;
    loaded.inode = (unsigned long long)status.st_ino;
    loaded.size = (long long)status.st_size;
    loaded.modified = (long long)status.st_mtime;
    memcpy(loaded.path, path, strlen(path) + 1);
    le_cache_release(cache);
    *cache = loaded;
    return 1;
}
