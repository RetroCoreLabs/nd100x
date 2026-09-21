#!/usr/bin/env python3
"""rule31_names.py - propose the rule 3.1 names for non-static functions.

    tools/house/rule31_names.py [--out FILE]

House rule 3.1: a non-static function is <module>_<verb> in lower snake_case.
The module prefix per file was settled in docs/house-audit/prefixes.tsv.

This writes a proposal to docs/house-audit/rule31_names.tsv - one row per
function, with the file, the old name and the proposed new name - so the whole
list can be read before anything is renamed. It changes no source.

Exempt (profile A.5): Dbg_*, Nd500_*, Init, Boot, SendKeyToTerminal, because
JavaScript calls those by name.
"""

import argparse
import os
import re
import subprocess
import sys

REPO = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
PREFIXES = os.path.join(REPO, "docs", "house-audit", "prefixes.tsv")
OUT = os.path.join(REPO, "docs", "house-audit", "rule31_names.tsv")

# Same splitter as rename_naming.py: an all-capitals run keeps its trailing
# digits (COM5025, IRQ12, B1), a capitalised word does not (From16 -> from_16).
WORD = re.compile(r"[A-Z]+(?![a-z])[0-9]*|[A-Z][a-z]+|[a-z]+[0-9]*|[0-9]+")

EXEMPT_PREFIX = ("Dbg_", "Nd500_")
EXEMPT_EXACT = {"Init", "Boot", "SendKeyToTerminal", "main"}
# The opcode_ functions were named by Ronny (21-SEP-2026) as
# opcode_<mnemonic>_<what it does>; that decision stands over the file prefix.
EXEMPT_PREFIX_MORE = ("opcode_",)

# Where the module prefix abbreviates a word the name already contains, the
# result reads twice. Only the genuinely redundant ones are listed: in
# mms_read_virtual_memory the "memory" belongs to "virtual memory" and must
# stay, so a blanket rule would make those names worse.
OVERRIDE = {
    "bkpt_breakpoint_manager_init": "bkpt_manager_init",
    "bkpt_breakpoint_manager_cleanup": "bkpt_manager_cleanup",
    "bkpt_breakpoint_manager_step_one": "bkpt_manager_step_one",
    "bkpt_breakpoint_manager_add": "bkpt_manager_add",
    "bkpt_breakpoint_manager_remove": "bkpt_manager_remove",
    "bkpt_breakpoint_manager_clear": "bkpt_manager_clear",
    "bkpt_breakpoint_manager_clear_type": "bkpt_manager_clear_type",
    "bkpt_breakpoint_manager_get_last_hit": "bkpt_manager_get_last_hit",
    "bkpt_check_for_breakpoint": "bkpt_check_hit",
    "kbd_keyboard_set_pipe_mode": "kbd_set_pipe_mode",
    "scsi_disk_set_disk_type": "scsi_disk_set_type",
    "smd_disk_set_disk_type": "smd_disk_set_type",
    "wd_disk_set_disk_type": "wd_disk_set_type",
    "dma_params_get_parameter_control_register": "dma_params_get_control_register",
    "dma_params_set_parameter_control_register": "dma_params_set_control_register",
}


def snake(name):
    words = []
    for part in name.split("_"):
        words += WORD.findall(part)
    return "_".join(w.lower() for w in words)


def js_exports():
    """Names JavaScript calls, from the WASM EXPORTED_FUNCTIONS list.

    House rule 3.1 exempts these (profile A.5). Hard-coding a handful was not
    enough: renaming IO_Tick broke the WASM link with "symbol exported via
    --export not found". The build file is the authoritative list, so it is
    read rather than guessed.
    """
    names = set()
    cml = os.path.join(REPO, "src", "frontend", "nd100wasm", "CMakeLists.txt")
    try:
        with open(cml, encoding="utf-8", errors="replace") as handle:
            text = handle.read()
    except OSError:
        return names
    m = re.search(r"EXPORTED_FUNCTIONS=\[(.*?)\]", text, re.S)
    if m:
        for name in re.findall(r"'_(\w+)'", m.group(1)):
            names.add(name)
    return names


def external_api():
    """Function names declared by vendored headers under external/.

    Some of these have WASM-only stub definitions in our own sources, so a
    definition scan finds them and proposes a rename. In a native build the
    real function comes from the vendored library under its original name,
    and renaming the call breaks the link with "undefined reference to
    debugger_dap_server_create". They are not ours to rename.
    """
    names = set()
    root = os.path.join(REPO, "external")
    # Statement-based, not line-based: a declaration often wraps, and
    # dap_server_send_process_event was missed because its argument list
    # continues on the next line.
    decl = re.compile(r"(?:^|[;{}])\s*(?:extern\s+)?[A-Za-z_][\w \*]*?\b(\w+)\s*\([^;{}]*\)\s*$")
    for base, _, files in os.walk(root):
        for f in files:
            if not f.endswith((".h", ".hpp")):
                continue
            try:
                with open(os.path.join(base, f), encoding="utf-8",
                          errors="replace") as handle:
                    text = handle.read()
            except OSError:
                continue
            text = re.sub(r"/\*.*?\*/", " ", text, flags=re.S)
            text = re.sub(r"//[^\n]*", " ", text)
            text = re.sub(r"^\s*#[^\n]*", " ", text, flags=re.M)
            for stmt in text.split(";"):
                stmt = " ".join(stmt.split())
                m = decl.search(stmt)
                if m:
                    names.add(m.group(1))
    return names


def prefixes():
    """file -> module prefix, from the agreed table."""
    out = {}
    with open(PREFIXES, encoding="utf-8") as handle:
        for line in handle:
            if line.startswith("#") or not line.strip():
                continue
            parts = line.rstrip("\n").split("\t")
            if len(parts) >= 3:
                out[parts[0]] = parts[2]
    return out


def non_static_functions(path):
    """(line, name) for every non-static function defined in this file."""
    found = []
    with open(os.path.join(REPO, path), encoding="utf-8", errors="replace") as handle:
        lines = handle.readlines()
    for no, line in enumerate(lines, 1):
        if line.startswith(("static", " ", "\t", "#", "/", "*", "}")):
            continue
        m = re.match(r"^[A-Za-z_][\w \*]*?\b(\w+)\s*\([^;]*$", line)
        if not m:
            continue
        # a definition has its body opening on this line or the next
        tail = "".join(lines[no - 1:no + 2])
        if "{" not in tail:
            continue
        found.append((no, m.group(1)))
    return found


def proposed(prefix, old):
    """The rule 3.1 name: <prefix>_<verb>, without doubling the prefix.

    Most of these functions already follow a Type_Verb convention, and that
    Type says the same thing the new module prefix says. Keeping both gives
    com5025_reg_com5025_registers_get_transmitter_character_len. The verb is
    everything after the first underscore, so the head is dropped.
    """
    p = prefix.lower()
    # Only a Type_ head, which starts with a capital. A lowercase head is
    # the verb itself - stripping it turned set_cpu_stop_reason and
    # get_cpu_stop_reason into the same name.
    head = re.match(r"^([A-Z][A-Za-z0-9]*)_(.+)$", old)
    if head:
        old = head.group(2)
    words = snake(old).split("_")
    # A multi-word prefix (floppy_dma) repeated inside the name:
    # CreateFloppyDMADevice -> floppy_dma_create_device, not
    # floppy_dma_create_floppy_dma_device.
    pw = p.split("_")
    if len(pw) > 1:
        for i in range(len(words) - len(pw) + 1):
            if words[i:i + len(pw)] == pw:
                words = words[:i] + words[i + len(pw):]
                break
    # Drop the module word where it already appears in the name, so
    # set_cpu_run_mode becomes cpu_set_run_mode rather than
    # cpu_set_cpu_run_mode, and cleanup_cpu becomes cpu_cleanup.
    if words.count(p) and len(words) > 1:
        words = [w for w in words if w != p] or [p]
    body = "_".join(words)
    if body == p:
        return p
    if body.startswith(p + "_"):
        return body
    return f"{p}_{body}"


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--out", default=OUT)
    args = ap.parse_args()

    table = prefixes()
    external = external_api() | js_exports()
    rows, skipped, seen = [], [], set()
    for path, prefix in sorted(table.items()):
        if not os.path.exists(os.path.join(REPO, path)):
            skipped.append((path, "file not found"))
            continue
        for no, old in non_static_functions(path):
            if (old.startswith(EXEMPT_PREFIX) or old in EXEMPT_EXACT
                    or old.startswith(EXEMPT_PREFIX_MORE)):
                continue
            if old in external:
                continue                 # vendored API, see external_api()
            if (path, old) in seen:
                continue                 # a multi-line signature matches twice
            seen.add((path, old))
            new = proposed(prefix, old)
            new = OVERRIDE.get(new, new)
            if new == old:
                continue
            rows.append((path, str(no), prefix, old, new))

    with open(args.out, "w", encoding="utf-8") as handle:
        handle.write("# Rule 3.1 proposal. Read before applying; nothing is renamed by\n"
                     "# this script. Prefixes come from docs/house-audit/prefixes.tsv.\n"
                     "file\tline\tprefix\told\tnew\n")
        for row in rows:
            handle.write("\t".join(row) + "\n")
    print(f"{len(rows)} proposed rename(s) -> {os.path.relpath(args.out, REPO)}")
    for path, why in skipped:
        print(f"  skipped {path}: {why}")
    dupes = {}
    for _, _, _, _, new in rows:
        dupes[new] = dupes.get(new, 0) + 1
    clash = {k: v for k, v in dupes.items() if v > 1}
    if clash:
        print(f"  NAME CLASHES ({len(clash)}): " + ", ".join(sorted(clash)[:10]))
    return 0


if __name__ == "__main__":
    sys.exit(main())
