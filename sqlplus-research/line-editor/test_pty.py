#!/usr/bin/env python3
"""Small local PTY regression test for the line-editor prototype."""

import os
import pty
import select
import subprocess
import sys
import tempfile
import time


def read_for(master, timeout=0.8):
    chunks = []
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        wait = min(0.05, max(0.0, deadline - time.monotonic()))
        ready, _, _ = select.select([master], [], [], wait)
        if not ready:
            continue
        try:
            chunks.append(os.read(master, 4096))
        except OSError:
            break
    return b"".join(chunks)


def wait_for(master, needle, timeout=2.0):
    data = b""
    deadline = time.monotonic() + timeout
    while needle not in data and time.monotonic() < deadline:
        data += read_for(master, 0.15)
    if needle not in data:
        raise AssertionError("did not receive %r; got %r" % (needle, data))
    return data


def main():
    executable = os.path.join(os.path.dirname(__file__), "sqlplus-line-editor")
    cache_file = tempfile.NamedTemporaryFile("w", delete=False)
    try:
        cache_file.write("HMY_DYNAMIC_TABLE\n")
        cache_file.close()
    except Exception:
        cache_file.close()
        os.unlink(cache_file.name)
        raise
    environment = os.environ.copy()
    environment["SQLPLUS_LFIRD_COMPLETION_FILE"] = cache_file.name
    master, slave = pty.openpty()
    process = subprocess.Popen(
        [executable], stdin=slave, stdout=slave, stderr=slave,
        start_new_session=True, env=environment,
    )
    os.close(slave)
    try:
        wait_for(master, b"SQL> ")

        os.write(master, b"SELEC\t\r")
        wait_for(master, b"submitted: SELECT\r\n")

        os.write(master, b"HMY_DYNAMIC_\t\r")
        wait_for(master, b"submitted: HMY_DYNAMIC_TABLE\r\n")

        os.write(master, b"abcd\x1b[D\x1b[3~\r")
        wait_for(master, b"submitted: abc\r\n")

        os.write(master, b"abc" + b"\x1b[D" + b"X" + b"\x1b[C" + b"\r")
        wait_for(master, b"submitted: abXc\r\n")

        os.write(master, b"scratch\x1b[A\x1b[B\r")
        wait_for(master, b"submitted: scratch\r\n")

        os.write(master, b"partial\x03")
        wait_for(master, b"^C\r\nSQL> ")

        os.write(master, b"\x04")
        process.wait(timeout=2)
        if process.returncode != 0:
            raise AssertionError("editor exited with %d" % process.returncode)
    finally:
        if process.poll() is None:
            process.terminate()
            process.wait(timeout=2)
        os.close(master)
        os.unlink(cache_file.name)
    print("PTY line-editor test: PASS")


if __name__ == "__main__":
    try:
        main()
    except Exception as error:
        print("PTY line-editor test: FAIL: %s" % error, file=sys.stderr)
        sys.exit(1)
