#!/usr/bin/env python3
"""A stand-in for nd100x --pipe, used by tests/test_nd100x_expect.py.

It reads the keyboard from stdin and answers like a very small SINTRAN:
  * ESC                 -> "ENTER "  (after $FAKE_IGNORE_ESC ESCs were ignored)
  * <name>CR <pw>CR     -> "PASSWORD: " then "@ "
  * <line>CR            -> "<line>" echoed, then "OK\\r\\n@ ";  "EXIT" ends the program
  * 0xFF <cmd> 0x0A     -> handled like pipe_handle_control() in
                           src/frontend/nd100x/nd100x.c, reply on stderr:
        mount <u> <path>  "[pipe-control] mount <u> '<path>' -> ok"
                          (or "-> FILE NOT FOUND" / "-> BAD UNIT")
        eject <u>         "[pipe-control] eject <u> -> ok"
Every typed byte that is not part of a control frame is echoed to stdout, so a
test can see exactly what reached the "keyboard". stderr also gets noise lines
for the log filter to drop. Command-line arguments are ignored.
"""

import os
import sys

IGNORE_ESC = int(os.environ.get("FAKE_IGNORE_ESC", "0"))


def out(text):
    os.write(1, text.encode("latin-1"))


def err(text):
    sys.stderr.write(text + "\n")
    sys.stderr.flush()


def control(cmd):
    parts = cmd.split(" ", 2)
    if parts[0] == "mount" and len(parts) == 3:
        unit = int(parts[1])
        if not 0 <= unit <= 2:
            err("[pipe-control] mount %d '%s' -> BAD UNIT" % (unit, parts[2]))
        elif not os.path.isfile(parts[2]):
            err("[pipe-control] mount %d '%s' -> FILE NOT FOUND" % (unit, parts[2]))
        else:
            err("[pipe-control] mount %d '%s' -> ok" % (unit, parts[2]))
    elif parts[0] == "eject" and len(parts) == 2:
        err("[pipe-control] eject %s -> ok" % parts[1])
    else:
        err("[pipe-control] unknown %r -> BAD COMMAND" % cmd)


def main():
    err("[DEBUG] scsi: register noise the log filter should drop")
    out("SINTRAN III - FAKE\r\nSINTRAN RUNNING\r\n")
    escs = 0
    state = "start"
    line = b""
    in_ctrl = False
    ctrl = b""
    while True:
        b = os.read(0, 1)
        if not b:
            return
        c = b[0]
        if in_ctrl:
            if c == 0x0A:
                in_ctrl = False
                control(ctrl.decode("ascii", "replace"))
                ctrl = b""
            else:
                ctrl += b
            continue
        if c == 0xFF:
            in_ctrl = True
            continue
        if c == 0x1B:
            escs += 1
            if escs > IGNORE_ESC:
                state = "user"
                line = b""
                out("\r\nENTER ")
            continue
        if c == 0x0D:
            text = line.decode("latin-1")
            line = b""
            out("\r\n")
            if state == "user":
                state = "password"
                out("PASSWORD: ")
            elif state == "password":
                state = "command"
                out("@ ")
            elif state == "command":
                if text == "EXIT":
                    return
                if text == "QUESTION":
                    # A question, then two console message blocks the way SINTRAN
                    # prints a server's reports (seen on ND5000.IMG, 2026-11-04).
                    out("Delete the previous version (Y/N): ")
                    out("\r\nERROR   * 1B:12B * 1988-12-16 09:34:15 * PROMAN.113127B\r\n"
                        "          SINTRAN III File System\r\n"
                        "          Not read, write and common access\r\n\r\n"
                        "INFO    * 1012B:0B * 1988-12-16 09:34:18 * MTSERV.30212B\r\n"
                        "          MTAD Server\r\n"
                        "          MTAD server initiated and started\r\n\r\n")
                    continue
                err("[DEBUG] scsi: TAPE1: CDB op=0x01 REWIND")
                out("OK %s\r\n@ " % text)
            continue
        line += b
        out(b.decode("latin-1"))


if __name__ == "__main__":
    main()
