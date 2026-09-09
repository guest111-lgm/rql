CC ?= gcc
CFLAGS ?= -O2 -g -Wall -Wextra -Wpedantic

.PHONY: all clean

all: liblfird_probe.so liblfird_fakecore.so lfird_interpose_test

liblfird_probe.so: lfird_probe.c ../line-editor/line_editor.c ../line-editor/line_editor.h
	$(CC) $(CFLAGS) -fPIC -shared -I../line-editor -o $@ lfird_probe.c ../line-editor/line_editor.c -ldl

liblfird_fakecore.so: fake_core.c
	$(CC) $(CFLAGS) -fPIC -shared -Wl,-soname,liblfird_fakecore.so -o $@ fake_core.c

lfird_interpose_test: interpose_test.c liblfird_fakecore.so
	$(CC) $(CFLAGS) -o $@ interpose_test.c -L. -llfird_fakecore -Wl,-rpath,'$$ORIGIN'

clean:
	 rm -f liblfird_probe.so liblfird_fakecore.so lfird_interpose_test
