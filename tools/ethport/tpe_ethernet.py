#!/usr/bin/env python3
"""Run the TPE PIOC-ETHER (ETHERNET-TWO) diagnostic against nd100x's Ethernet II card.

Usage (from anywhere):
  tpe_ethernet.py --bin build_eth/bin/nd100x --image images/Nd-210523I01-XX-01D.img \
                  --max-instr 3000000000 --log OUT.txt [--reply PROMPT_SUBSTRING=TEXT ...]

The floppy image is COPIED to a temporary directory first; the original is
never opened by the emulator (TPE programs write to their media).

The emulator is started with --pipe, --eth0=none and -n MAX_INSTR, so it ends
by itself after MAX_INSTR instructions - this script never kills it. Console
steps: CR to wake the monitor, then at each idle "TPE>" prompt the next of
the program name and "run"; after "run", each --reply whose PROMPT_SUBSTRING
appears at the end of the console output is sent once, in order.

Everything the console prints is written to --log, so the run can be read
afterwards; the script prints the lines of the test summary it finds.
"""
import argparse
import os
import pty
import select
import shutil
import sys
import tempfile
import time


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--bin", required=True)
    ap.add_argument("--image", required=True)
    ap.add_argument("--max-instr", type=int, required=True)
    ap.add_argument("--log", required=True)
    ap.add_argument("--program", default="PIOC-ETHER")
    ap.add_argument("--reply", action="append", default=[])
    ap.add_argument("--extra", action="append", default=[], help="extra nd100x argument")
    a = ap.parse_args()
    a.bin = os.path.abspath(a.bin)
    a.log = os.path.abspath(a.log)

    replies = []
    for r in a.reply:
        k, _, v = r.partition("=")
        replies.append((k, v))

    tmp = tempfile.mkdtemp(prefix="tpe_eth_")
    image = os.path.join(tmp, os.path.basename(a.image))
    shutil.copyfile(a.image, image)

    argv = [a.bin, "--boot=floppy", "--image=%s" % image, "--pipe", "--eth0=none",
            "-n", str(a.max_instr)] + a.extra
    pid, fd = pty.fork()
    if pid == 0:
        os.environ["TERM"] = "vt100"
        os.chdir(tmp)
        os.execv(a.bin, argv)
        os._exit(127)

    out = bytearray()
    start = time.time()
    steps = [a.program, "run"]
    si = 0
    ri = 0
    last_send = 0.0
    kicked = False
    run_sent = False
    while True:
        r, _, _ = select.select([fd], [], [], 0.3)
        if r:
            try:
                c = os.read(fd, 4096)
            except OSError:
                break
            if not c:
                break
            out += c
        now = time.time()
        text = out.decode("latin1", "replace")
        tail = text.rstrip()
        if not kicked and now - start > 3:
            os.write(fd, b"\r")
            kicked = True
            last_send = now
            continue
        if kicked and si < len(steps) and tail.endswith("TPE>") and now - last_send > 1.5:
            os.write(fd, steps[si].encode() + b"\r")
            run_sent = run_sent or steps[si] == "run"
            si += 1
            last_send = now
            continue
        if run_sent and ri < len(replies) and now - last_send > 1.0:
            key, val = replies[ri]
            if tail.endswith(key) or tail[-200:].rstrip().endswith(key):
                os.write(fd, val.encode() + b"\r")
                ri += 1
                last_send = now
                continue
    _, status = os.waitpid(pid, 0)
    with open(a.log, "w", encoding="latin1") as f:
        f.write(out.decode("latin1", "replace"))
    shutil.rmtree(tmp, ignore_errors=True)
    print("emulator exit status: %d, %.0f s, %d bytes of console in %s"
          % (os.waitstatus_to_exitcode(status), time.time() - start, len(out), a.log))
    for line in out.decode("latin1", "replace").splitlines():
        s = line.strip()
        if s[:3].strip().rstrip(".").isdigit() or "ERROR" in s or "End of" in s:
            print("  " + s)
    return 0


if __name__ == "__main__":
    sys.exit(main())
