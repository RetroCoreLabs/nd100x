#!/usr/bin/env python3
"""Create or refresh docs/ethernet-port/TRACE-MATRIX.csv from the inventory.

Every inventory row gets a matrix row. New rows are PLANNED, with the C file
and C symbol given by the naming rule below, so no row depends on someone
remembering it. Rows already in the matrix with any other status, or with a
hand-edited PLANNED target, are kept unchanged. Rows whose id disappeared
from the inventory are dropped and listed.

Naming rule (plan Phase 1):
  C# type          -> C file (FILE_MAP) and a function prefix
  method/ctor/...  -> <prefix>_<snake_case(name)>
  field/property   -> struct member <snake_case(name)>
  const/enumvalue  -> <PREFIX>_<UPPER_SNAKE(name)>
  enum/class/...   -> type <PascalCase> (C# name kept)
  case/switcharm   -> the C function that ports the containing method
"""
import csv
import os
import re
import sys

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
INV = os.path.join(ROOT, "docs", "ethernet-port", "cs-inventory.csv")
MAT = os.path.join(ROOT, "docs", "ethernet-port", "TRACE-MATRIX.csv")
FIELDS = ["id", "status", "c_file", "c_symbol", "c_line", "reason", "approved"]

E = "src/devices/ethernet/"
# outermost C# type -> (C file, prefix)
TYPE_MAP = {
    "NDBusEthernetII": (E + "device_ethernet.c", "eth"),
    "InstrumentedRAM": (E + "eth_memory.c", "ethram"),
    "NDEthernetMemory": (E + "eth_memory.c", "ethmem"),
    "EthMailboxTracer": (E + "eth_memory.c", "ethmbox"),
    "ETH_IOMem": (E + "eth_iospace.c", "ethio"),
    "EthDecode": (E + "eth_decode.c", "ethdec"),
    "Am7990Lance": (E + "eth_lance.c", "lance"),
    "MC68901MFP": (E + "eth_mfp.c", "mfp"),
    "MfpTimer": (E + "eth_mfp_timer.c", "mfptimer"),
    "Usart": (E + "eth_mfp_usart.c", "usart"),
    "Registers": (E + "eth_mfp_regs.c", "mfpregs"),
    "IpChecksumRepair": (E + "net/eth_ipcsum.c", "ipcsum"),
    "EthernetBackendFactory": (E + "net/eth_backend_factory.c", "ethnet_factory"),
    "NullEthernetBackend": (E + "net/eth_net_null.c", "ethnet_null"),
    "UdpEthernetBackend": (E + "net/eth_net_udp.c", "ethnet_udp"),
    "TcpEthernetBackend": (E + "net/eth_net_tcp.c", "ethnet_tcp"),
    "TcpEthernetRelay": (E + "net/eth_net_tcp_relay.c", "ethnet_relay"),
    "PcapEthernetBackend": (E + "net/eth_net_pcap.c", "ethnet_pcap"),
    "InProcessEthernetBridge": (E + "net/eth_net_inproc.c", "ethnet_inproc"),
    "InProcessEthernetWire": (E + "net/eth_net_inproc.c", "ethnet_wire"),
    "IEthernetBackend": (E + "net/eth_backend.h", "ethnet"),
}
# C# file -> default (C file, prefix) for types not in TYPE_MAP (enums etc.)
FILE_MAP = {
    "Emulated.HW/ND/CPU/NDBUS/NDBusEthernetII.cs": (E + "device_ethernet.h", "eth"),
    "Emulated.HW/ND/CPU/NDBUS/NDBusEthernetIIDecode.cs": (E + "eth_decode.h", "ethdec"),
    "Emulated.HW/AMD/LANCE/Am7990/Am2990Lance.cs": (E + "eth_lance.h", "lance"),
    "Emulated.HW/AMD/LANCE/Am7990/Enums.cs": (E + "eth_lance.h", "lance"),
    "Emulated.HW/Motorola/MFP/MC68901/HelperEnum.cs": (E + "eth_mfp.h", "mfp"),
    "Emulated.HW/Motorola/MFP/MC68901/MC68901MFP.cs": (E + "eth_mfp.h", "mfp"),
    "Emulated.HW/Motorola/MFP/MC68901/MfpTimer.cs": (E + "eth_mfp_timer.h", "mfptimer"),
    "Emulated.HW/Motorola/MFP/MC68901/Registers.cs": (E + "eth_mfp_regs.h", "mfpregs"),
    "Emulated.HW/Motorola/MFP/MC68901/Usart.cs": (E + "eth_mfp_usart.h", "usart"),
}


def snake(name):
    s = re.sub(r"[^0-9A-Za-z]+", "_", name)
    s = re.sub(r"([a-z0-9])([A-Z])", r"\1_\2", s)
    s = re.sub(r"([A-Z]+)([A-Z][a-z])", r"\1_\2", s)
    return s.strip("_").lower() or "x"


def target(row):
    container = [c for c in row["container"].split(".") if c] if row["container"] else []
    types = [c for c in container if not c.endswith("()")]
    kind = row["kind"]
    if kind in ("class", "struct", "interface", "record", "enum", "delegate"):
        outer = (types[0] if types else row["name"])
    else:
        outer = types[0] if types else None
    inner = types[-1] if types else None
    # nested types map on their own name when listed (EthMailboxTracer, InstrumentedRAM)
    key = inner if inner in TYPE_MAP else outer
    if kind in ("class", "struct", "interface") and row["name"] in TYPE_MAP:
        key = row["name"]
    if key in TYPE_MAP:
        cfile, prefix = TYPE_MAP[key]
    else:
        cfile, prefix = FILE_MAP.get(row["file"], (E + "UNMAPPED", "x"))
    name = row["name"]
    if kind in ("method", "ctor", "dtor", "localfunc", "operator", "indexer"):
        sym = "%s_%s" % (prefix, snake(name if kind != "ctor" else "create"))
    elif kind in ("field", "property", "event"):
        sym = snake(name)
    elif kind in ("const", "localconst", "enumvalue"):
        owner = snake(inner) if (kind == "enumvalue" and inner) else prefix
        sym = ("%s_%s" % (owner, snake(name))).upper()
    elif kind in ("case", "switcharm"):
        meth = [c for c in container if c.endswith("()")]
        sym = "%s_%s" % (prefix, snake(meth[-1][:-2])) if meth else prefix
    else:
        sym = name
    return cfile, sym


def main():
    inv = list(csv.DictReader(open(INV, encoding="utf-8")))
    old = {}
    if os.path.exists(MAT):
        for r in csv.DictReader(open(MAT, encoding="ascii")):
            old[r["id"]] = r
    rows, added = [], 0
    for r in inv:
        if r["id"] in old:
            rows.append(old.pop(r["id"]))
            continue
        cfile, sym = target(r)
        rows.append({"id": r["id"], "status": "PLANNED", "c_file": cfile, "c_symbol": sym,
                     "c_line": "", "reason": "", "approved": ""})
        added += 1
    for gone in old:
        print("dropped (no longer in inventory):", gone)
    with open(MAT, "w", newline="", encoding="ascii") as f:
        w = csv.DictWriter(f, fieldnames=FIELDS, lineterminator="\n")
        w.writeheader()
        w.writerows(rows)
    unmapped = [r for r in rows if "UNMAPPED" in r["c_file"]]
    print("matrix: %d rows, %d new PLANNED, %d dropped, %d unmapped"
          % (len(rows), added, len(old), len(unmapped)))
    for r in unmapped[:20]:
        print("  unmapped:", r["id"])
    return 1 if unmapped else 0


if __name__ == "__main__":
    sys.exit(main())
