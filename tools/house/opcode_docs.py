#!/usr/bin/env python3
"""opcode_docs.py - Doxygen blocks for the opcode functions of src/cpu.

    tools/house/opcode_docs.py [--apply]

Without --apply nothing is written; the blocks are printed for review.

Every fact comes from a file in this repo, never from the author of this
script:

  - docs/cpu_documentation.md - the instruction reference. Supplies the
    description, octal opcode, instruction mask, category, privilege level,
    format and the operand table with bit ranges.
  - src/cpu/cpu_instr.c - the comments already there. They carry "Affected:"
    lines (which registers the instruction changes) that the reference does
    NOT have, so those are carried over rather than lost.
  - docs/house-audit/opcode_names.tsv - maps each function to its mnemonic.

An instruction with no entry in the reference keeps a short block naming only
what is known. Nothing is invented: opcode_halt and opcode_chreent_pages have
no reference entry, and opcode_shift_group dispatches a family rather than
one instruction.

The reference spells "Condition" as "Condtion" in 14 entries; that typo is
corrected on the way in rather than copied into the source.
"""

import argparse
import os
import re
import sys
import textwrap

REPO = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
SRC = os.path.join(REPO, "src", "cpu", "cpu_instr.c")
DOC = os.path.join(REPO, "docs", "cpu_documentation.md")
NAMES = os.path.join(REPO, "docs", "house-audit", "opcode_names.tsv")

WIDTH = 74

# The reference is written with arrows, subscripts and emoji. C comments in
# this project are ASCII only - the ND toolchain predates unicode - so they
# are translated rather than copied through.
NON_ASCII = {
    "\u27f5": "<-", "\u2190": "<-", "\u27f6": "->", "\u2192": "->",
    "\u0394": "delta", "\u2082": "2", "\u2088": "8", "\u2081": "1",
    "\u2080": "0", "\u00d7": "x", "\u2013": "-", "\u2014": "-",
    "\u2018": "'", "\u2019": "'", "\u201c": '"', "\u201d": '"',
}


def to_ascii(text):
    """Replace what the reference writes in unicode, drop anything left."""
    for bad, good in NON_ASCII.items():
        text = text.replace(bad, good)
    return "".join(c if ord(c) < 128 else " " for c in text)


def reference():
    """Every instruction entry of docs/cpu_documentation.md, by mnemonic."""
    with open(DOC, encoding="utf-8") as handle:
        doc = handle.read()
    out = {}
    for match in re.finditer(r"^### ([A-Z0-9_]+)\s*$", doc, re.M):
        name = match.group(1)
        start = match.end()
        nxt = re.search(r"^### ", doc[start:], re.M)
        body = doc[start:start + (nxt.start() if nxt else 6000)]

        def field(label):
            found = re.search(r"\|\s*" + label + r"\s*\|\s*`?([^|`]+)`?", body)
            return found.group(1).strip() if found else ""

        described = re.search(r"#### 📝 Description\s*\n+(.*?)\n#### ", body, re.S)
        text = described.group(1).strip() if described else ""
        text = text.replace("Condtion:", "Condition:")
        text = to_ascii(text)
        operands = [
            (n, bits, to_ascii(d).strip()) for n, bits, d in
            re.findall(r"\| `(\w+)` \| [^|]+ \| ([\d\-]+) \| ([^|]+)\|", body)
        ]
        out[name] = {
            "desc": re.sub(r"\n{2,}", "\n", text),
            "opcode": field("Opcode"), "mask": field("Mask"),
            "category": field("Category"), "privilege": field("Privilege"),
            "format": field("Format"), "operands": operands,
        }
    return out


def affected_lines(source):
    """The "Affected:" note already written above each opcode function.

    The reference does not say which registers an instruction changes, but
    the comments in cpu_instr.c do for 29 of them. Keyed by function name.
    """
    out = {}
    lines = source.splitlines()
    for index, line in enumerate(lines):
        found = re.match(r"^(?:static )?void (opcode_\w+)\(uint16_t", line)
        if not found:
            continue
        for back in range(index - 1, max(-1, index - 30), -1):
            text = lines[back]
            if re.match(r"^\s*(?:static )?\w.*\)\s*$", text) and "opcode_" in text:
                break
            got = re.search(r"Affected:\s*(.+?)\s*$", text)
            if got:
                out[found.group(1)] = got.group(1).strip()
                break
    return out


def build_block(mnemonic, info, affected):
    """The Doxygen block for one instruction."""
    out = ["/**"]
    if info and info["desc"]:
        lines = info["desc"].splitlines()
        head = lines[0].rstrip(".")
        out.append(f" * @brief {mnemonic} - {head}.")
        rest = " ".join(lines[1:]).strip()
        if rest:
            out.append(" *")
            out += [f" * {l}" for l in textwrap.wrap(rest, WIDTH)]
    else:
        out.append(f" * @brief {mnemonic} - see the notes below.")

    if info and (info["opcode"] or info["mask"]):
        out += [" *", " * @par Instruction"]
        if info["opcode"]:
            out.append(f" * Opcode {info['opcode']} octal"
                       + (f", mask {info['mask']}." if info["mask"] else "."))
        if info["category"]:
            out.append(f" * Category: {info['category']}."
                       + (f" Privilege: {info['privilege']}."
                          if info["privilege"] else ""))
        if info["format"]:
            out.append(f" * Format: {info['format']}")

    if affected:
        out += [" *", f" * @par Registers affected", f" * {affected}"]

    if info and info["operands"]:
        out += [" *", " * @par Instruction word"]
        for name, bits, text in info["operands"]:
            wrapped = textwrap.wrap(f"bits {bits}  {name} - {text}", WIDTH - 4)
            out.append(f" *   {wrapped[0]}")
            out += [f" *       {w}" for w in wrapped[1:]]

    out += [" *", " * @param operand The full instruction word as fetched."]
    if info:
        out.append(f" * @see docs/cpu_documentation.md section {mnemonic}")
    out.append(" */")
    return "\n".join(out)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--apply", action="store_true")
    args = ap.parse_args()

    ref = reference()
    with open(SRC, encoding="utf-8") as handle:
        source = handle.read()
    affected = affected_lines(source)

    mnemonic_of = {}
    with open(NAMES, encoding="utf-8") as handle:
        for line in handle:
            if line.startswith("#") or not line.strip():
                continue
            old, new = line.rstrip("\n").split("\t")
            mnemonic_of[new] = old.replace("ndfunc_", "").upper()
    # The two kept-but-not-dispatched implementations (see cpu_instr.c).
    mnemonic_of["opcode_movb_move_byte_buggy"] = "MOVB"
    mnemonic_of["opcode_movbf_move_bytes_forward_buggy"] = "MOVBF"
    mnemonic_of["opcode_movb_move_byte"] = "MOVB"
    mnemonic_of["opcode_movbf_move_bytes_forward"] = "MOVBF"

    lines = source.splitlines()
    edits = []
    for index, line in enumerate(lines):
        found = re.match(r"^(?:static )?void (opcode_\w+)\(uint16_t", line)
        if not found:
            continue
        fn = found.group(1)
        mnemonic = mnemonic_of.get(fn)
        if not mnemonic:
            continue
        # The comment block directly above, if any.
        start = index
        probe = index - 1
        while probe >= 0 and lines[probe].strip().startswith(("*", "/*", "///")):
            start = probe
            probe -= 1
        if probe >= 0 and lines[probe].strip().endswith("*/"):
            while probe >= 0 and not lines[probe].strip().startswith("/*"):
                probe -= 1
            start = max(probe, 0)
        block = build_block(mnemonic, ref.get(mnemonic), affected.get(fn))
        edits.append((start, index, block, fn))

    if not args.apply:
        for _, _, block, fn in edits[:3]:
            print(block)
            print(f"<function {fn}>\n")
        print(f"{len(edits)} block(s) would be written. Nothing changed.")
        return 0

    for start, end, block, _ in sorted(edits, reverse=True):
        lines[start:end] = block.splitlines()
    with open(SRC, "w", encoding="utf-8") as handle:
        handle.write("\n".join(lines) + "\n")
    print(f"wrote {len(edits)} Doxygen block(s) to "
          f"{os.path.relpath(SRC, REPO)}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
