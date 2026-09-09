CC ?= gcc
CFLAGS ?= -O2 -g -Wall -Wextra -Wpedantic

.PHONY: all clean test context-test

all: sqlplus-line-editor

test: sqlplus-line-editor context-test
	python3 test_pty.py
	./context-test

context-test: sql_context_test.c sql_context.c sql_context.h
	$(CC) $(CFLAGS) -o $@ sql_context_test.c sql_context.c

sqlplus-line-editor: sqlplus-line-editor.c line_editor.c line_editor.h completion_cache.c completion_cache.h sql_context.c sql_context.h
	$(CC) $(CFLAGS) -o $@ sqlplus-line-editor.c line_editor.c completion_cache.c sql_context.c

clean:
	 rm -f sqlplus-line-editor context-test
