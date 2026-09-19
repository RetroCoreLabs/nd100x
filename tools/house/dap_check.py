#!/usr/bin/env python3
"""dap_check.py - gate G14: start nd100x with the DAP server, attach, pause,
and require a 'stopped' event.

    tools/house/dap_check.py --binary BIN --image SMD0.IMG --tmp DIR [--port 6661]

The image is copied to DIR first; the repo image is never opened. The
emulator is started by this script and is stopped by it (disconnect with
terminateDebuggee), with a timeout as the backstop. Never uses port 4711.
"""

import argparse
import json
import os
import shutil
import socket
import subprocess
import sys
import time


def send(sock, seq, command, args=None):
    msg = {"seq": seq, "type": "request", "command": command}
    if args is not None:
        msg["arguments"] = args
    body = json.dumps(msg).encode()
    sock.sendall(b"Content-Length: %d\r\n\r\n" % len(body) + body)


def read_messages(sock, buf):
    out = []
    while True:
        head_end = buf.find(b"\r\n\r\n")
        if head_end < 0:
            return out, buf
        length = 0
        for line in buf[:head_end].split(b"\r\n"):
            if line.lower().startswith(b"content-length:"):
                length = int(line.split(b":")[1])
        if len(buf) < head_end + 4 + length:
            return out, buf
        out.append(json.loads(buf[head_end + 4:head_end + 4 + length]))
        buf = buf[head_end + 4 + length:]


def wait_for(sock, pred, timeout, buf):
    end = time.time() + timeout
    seen = []
    while time.time() < end:
        sock.settimeout(max(0.1, end - time.time()))
        try:
            data = sock.recv(65536)
        except socket.timeout:
            break
        if not data:
            break
        buf += data
        msgs, buf = read_messages(sock, buf)
        for m in msgs:
            seen.append(m)
            if pred(m):
                return m, buf, seen
    return None, buf, seen


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--binary", required=True)
    ap.add_argument("--image", required=True)
    ap.add_argument("--tmp", required=True)
    ap.add_argument("--port", type=int, default=6661)
    a = ap.parse_args()
    if a.port == 4711:
        sys.exit("dap_check: port 4711 is not allowed")

    img = os.path.join(a.tmp, "dap.img")
    shutil.copyfile(a.image, img)
    with socket.socket() as probe:
        if probe.connect_ex(("127.0.0.1", a.port)) == 0:
            sys.exit(f"dap_check: port {a.port} is in use - pick another with --port")

    proc = subprocess.Popen(["timeout", "120", a.binary, "--boot=smd", "--smd0=" + img,
                             "--debugger", "--port=%d" % a.port, "--pipe"],
                            stdin=subprocess.PIPE, stdout=subprocess.DEVNULL,
                            stderr=subprocess.DEVNULL)
    ok = False
    try:
        sock = None
        for _ in range(100):
            try:
                sock = socket.create_connection(("127.0.0.1", a.port), timeout=1)
                break
            except OSError:
                time.sleep(0.1)
        if sock is None:
            print("dap_check: could not connect")
            return 1
        buf = b""
        seq = 1
        send(sock, seq, "initialize", {"adapterID": "nd100x", "clientID": "gate"})
        r, buf, _ = wait_for(sock, lambda m: m.get("command") == "initialize", 10, buf)
        if not r or not r.get("success"):
            print("dap_check: initialize failed:", r)
            return 1
        seq += 1
        send(sock, seq, "attach", {"pid": proc.pid})
        r, buf, _ = wait_for(sock, lambda m: m.get("command") == "attach", 10, buf)
        if not r or not r.get("success"):
            print("dap_check: attach failed:", r)
            return 1
        seq += 1
        send(sock, seq, "configurationDone", {})
        _, buf, _ = wait_for(sock, lambda m: m.get("command") == "configurationDone", 5, buf)
        # nd100x flow is connect -> attach -> continue (memory note
        # dap-continue-attached-gate); pause only works on a running target.
        seq += 1
        send(sock, seq, "continue", {"threadId": 1})
        _, buf, _ = wait_for(sock, lambda m: m.get("command") == "continue", 5, buf)
        time.sleep(2)
        seq += 1
        send(sock, seq, "pause", {"threadId": 1})
        r, buf, seen = wait_for(sock, lambda m: m.get("type") == "event" and m.get("event") == "stopped",
                                15, buf)
        if not r:
            print("dap_check: no stopped event after pause; messages:",
                  [m.get("command") or m.get("event") for m in seen])
            return 1
        print("dap_check: attach + pause -> stopped (reason %s)" % r.get("body", {}).get("reason"))
        ok = True
        seq += 1
        send(sock, seq, "disconnect", {"terminateDebuggee": True})
        wait_for(sock, lambda m: m.get("command") == "disconnect", 5, buf)
        sock.close()
    finally:
        try:
            proc.wait(timeout=10)
        except subprocess.TimeoutExpired:
            # The emulator did not stop on disconnect. It runs under
            # 'timeout 120', which ends it; report instead of killing.
            print("dap_check: emulator still running after disconnect (timeout 120 will end it)")
            ok = False
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
