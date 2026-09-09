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
    replacement_path = cache_file.name + ".next"
    try:
        cache_file.write(
            "# sqlplus-line-editor-cache-v1\n"
            "META\tCURRENT_SCHEMA\tAPP\n"
            "OBJECT\tAPP\tCLIENTS\tTABLE\n"
            "OBJECT\tAPP\tORDERS\tVIEW\n"
            "OBJECT\tOTHER\tCUSTOMERS\tTABLE\n"
            "SYNONYM\tAPP\tCUSTOMER_ALIAS\tAPP\tCLIENTS\t\n"
            "SYNONYM\tPUBLIC\tPUB_CUSTOMERS\tAPP\tCLIENTS\t\n"
            "COLUMN\tAPP\tCLIENTS\tCLIENT_ID\t1\n"
            "COLUMN\tAPP\tCLIENTS\tNAME\t2\n"
            "COLUMN\tAPP\tORDERS\tORDER_ID\t1\n"
            "HMY_DYNAMIC_TABLE\n"
        )
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

        with open(cache_file.name, "r", encoding="utf-8") as original:
            refreshed_cache = original.read()
        with open(replacement_path, "w", encoding="utf-8") as refreshed:
            refreshed.write(refreshed_cache)
            refreshed.write("OBJECT\tAPP\tINVOICES\tTABLE\n")
        os.replace(replacement_path, cache_file.name)
        os.write(master, b"FROM APP.INV\t\r")
        wait_for(master, b"submitted: FROM APP.INVOICES\r\n")

        os.write(master, b"FROM APP.CLI\t\r")
        wait_for(master, b"submitted: FROM APP.CLIENTS\r\n")

        os.write(master, b"FROM AP\t\r")
        wait_for(master, b"submitted: FROM APP.\r\n")

        os.write(master, b"FROM CUSTOMER_\t\r")
        wait_for(master, b"submitted: FROM CUSTOMER_ALIAS\r\n")

        query = b"SELECT c.CLI FROM APP.CLIENTS c"
        prefix = b"SELECT c.CLI"
        os.write(master, query + b"\x01" + b"\x1b[C" * len(prefix) + b"\t")
        os.write(master, b"\x1b[F\r")
        wait_for(master, b"submitted: SELECT c.CLIENT_ID FROM APP.CLIENTS c\r\n")

        query = b"SELECT c.NA FROM APP.CLIENTS c"
        prefix = b"SELECT c.NA"
        os.write(master, query + b"\x01" + b"\x1b[C" * len(prefix) + b"\t")
        os.write(master, b"\x1b[F\r")
        wait_for(master, b"submitted: SELECT c.NAME FROM APP.CLIENTS c\r\n")

        os.write(master, b"SELECT * FROM APP.CLIENTS c WHERE c.NA\t\r")
        wait_for(master, b"submitted: SELECT * FROM APP.CLIENTS c WHERE c.NAME\r\n")

        query = b"SELECT ca.CLI FROM APP.CUSTOMER_ALIAS ca"
        prefix = b"SELECT ca.CLI"
        os.write(master, query + b"\x01" + b"\x1b[C" * len(prefix) + b"\t")
        os.write(master, b"\x1b[F\r")
        wait_for(master, b"submitted: SELECT ca.CLIENT_ID FROM APP.CUSTOMER_ALIAS ca\r\n")

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
        for path in (cache_file.name, replacement_path):
            try:
                os.unlink(path)
            except FileNotFoundError:
                pass
    print("PTY line-editor test: PASS")


if __name__ == "__main__":
    try:
        main()
    except Exception as error:
        print("PTY line-editor test: FAIL: %s" % error, file=sys.stderr)
        sys.exit(1)
