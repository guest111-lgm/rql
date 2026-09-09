#include "line_editor.h"

#include <unistd.h>

int main(void)
{
    le_history history;
    char line[LE_LINE_MAX];

    le_history_init(&history);
    for (;;) {
        int result = le_readline(STDIN_FILENO, STDOUT_FILENO, "SQL> ",
                                 &history, line, sizeof(line));
        if (result == LE_EOF)
            break;
        if (result == LE_INTERRUPT)
            continue;
        if (result < 0)
            return 1;
        write(STDOUT_FILENO, "submitted: ", 11);
        write(STDOUT_FILENO, line, (size_t)result);
    }
    return 0;
}
