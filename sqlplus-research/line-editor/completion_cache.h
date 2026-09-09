#ifndef SQLPLUS_RESEARCH_COMPLETION_CACHE_H
#define SQLPLUS_RESEARCH_COMPLETION_CACHE_H

#include <stddef.h>

/*
 * The cache is deliberately a process-local, read-only view of metadata.  A
 * refresh process may replace the file atomically while an editor is running;
 * the editor notices the new inode/stat tuple on the next Tab press.
 */

typedef struct le_cache_object {
    size_t owner;
    size_t name;
    size_t type;
} le_cache_object;

typedef struct le_cache_synonym {
    size_t owner;
    size_t name;
    size_t table_owner;
    size_t table_name;
    size_t db_link;
} le_cache_synonym;

typedef struct le_cache_column {
    size_t owner;
    size_t table_name;
    size_t name;
    unsigned long column_id;
} le_cache_column;

typedef struct le_cache_word {
    size_t name;
} le_cache_word;

typedef struct le_cache {
    char *strings;
    size_t strings_length;
    size_t strings_capacity;

    le_cache_object *objects;
    size_t object_count;
    size_t object_capacity;

    le_cache_synonym *synonyms;
    size_t synonym_count;
    size_t synonym_capacity;

    le_cache_column *columns;
    size_t column_count;
    size_t column_capacity;

    le_cache_word *words;
    size_t word_count;
    size_t word_capacity;

    size_t current_schema;
    int current_schema_valid;

    int stamp_valid;
    unsigned long long device;
    unsigned long long inode;
    long long size;
    long long modified;
    char path[4096];
} le_cache;

void le_cache_init(le_cache *cache);
void le_cache_release(le_cache *cache);

/* Return 1 when the file was loaded or the cache was cleared, 0 when reused. */
int le_cache_ensure(le_cache *cache, const char *path);

const char *le_cache_text(const le_cache *cache, size_t offset);
const char *le_cache_current_schema(const le_cache *cache);

#endif
