#ifndef SQLPLUS_RESEARCH_LINE_EDITOR_H
#define SQLPLUS_RESEARCH_LINE_EDITOR_H

#include <stddef.h>

#define LE_HISTORY_MAX 100
#define LE_LINE_MAX 8192

enum le_result {
    LE_ERROR = -1,
    LE_INTERRUPT = -2,
    LE_EOF = 0
};

typedef struct le_history {
    char entries[LE_HISTORY_MAX][LE_LINE_MAX];
    size_t count;
} le_history;

void le_history_init(le_history *history);

/*
 * Read one logical line.  The returned byte count includes the trailing '\n'
 * on success and excludes the terminating NUL written to out.  A non-NULL
 * prompt is printed before reading; pass NULL when the host already printed
 * its prompt (the lfird hook uses this mode).
 */
int le_readline(int input_fd, int output_fd, const char *prompt,
                le_history *history, char *out, size_t out_cap);

#endif
