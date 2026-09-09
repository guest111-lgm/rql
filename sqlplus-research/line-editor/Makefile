CC ?= gcc
CFLAGS ?= -O2 -g -Wall -Wextra -Wpedantic

.PHONY: all clean test

all: sqlplus-line-editor

test: sqlplus-line-editor
	python3 test_pty.py

sqlplus-line-editor: sqlplus-line-editor.c line_editor.c line_editor.h
	$(CC) $(CFLAGS) -o $@ sqlplus-line-editor.c line_editor.c

clean:
	 rm -f sqlplus-line-editor
