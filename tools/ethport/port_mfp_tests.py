#!/usr/bin/env python3
"""Mechanical first pass: RetroCore MC68901MFPTests.cs -> tests/ethernet/test_eth_mfp.c.

Translates only fixed, listed forms (calls on _mfp, Assert.That with the
NUnit constraints used in the file, C# integer types, enum members, the four
recorder lists). Anything else is copied as a line starting with
"PORT-TODO:" so the C file does not compile until a human has ported it.
The output is reviewed test by test against the C# (plan rule R5); this
script is not trusted on its own.

Usage: port_mfp_tests.py <path to MC68901MFPTests.cs> > tests/ethernet/test_eth_mfp.c
"""
import re
import sys

src = open(sys.argv[1], encoding="utf-8-sig").read().replace("\r\n", "\n")

# ---- split into test methods -------------------------------------------------
methods = []
for m in re.finditer(r"((?:[ \t]*\[[^\]]*\][ \t]*\n)+)[ \t]*public void (\w+)\(\)\s*\n[ \t]*\{", src):
    attrs, name = m.group(1), m.group(2)
    if not re.search(r"\[(Test|TestCase)\b", attrs):
        continue
    i = m.end() - 1
    depth = 0
    for j in range(i, len(src)):
        if src[j] == "{":
            depth += 1
        elif src[j] == "}":
            depth -= 1
            if depth == 0:
                break
    methods.append((name, attrs, src[i + 1:j], src.count("\n", 0, m.start(2)) + 1))

CALLS = [
    (r"_mfp\.Write\(", "mfp_write(&s_mfp, "),
    (r"_mfp\.Read\(", "mfp_read(&s_mfp, "),
    (r"_mfp\.Clock\(null\)", "mfp_clock(&s_mfp)"),
    (r"_mfp\.gpio_input\(", "mfp_gpio_input(&s_mfp, "),
    (r"_mfp\.HandleInterruptAcknowledge\(\)", "mfp_handle_interrupt_acknowledge(&s_mfp)"),
    (r"_mfp\.GetInterruptVector\(\)", "mfp_get_interrupt_vector(&s_mfp)"),
    (r"_mfp\.get_vector\(\)", "mfp_get_vector(&s_mfp)"),
    (r"_mfp\.TimerInputA\(", "mfp_timer_input_a(&s_mfp, "),
    (r"_mfp\.TimerInputB\(", "mfp_timer_input_b(&s_mfp, "),
    (r"_mfp\.SetSerialInput\(", "mfp_set_serial_input(&s_mfp, "),
    (r"_mfp\.SetReceiverClock\(", "mfp_set_receiver_clock(&s_mfp, "),
    (r"_mfp\.SetTransmitterClock\(", "mfp_set_transmitter_clock(&s_mfp, "),
    (r"_mfp\.TriggerSoftwareInterrupt\(", "mfp_trigger_software_interrupt(&s_mfp, "),
    (r"_mfp\.TriggerTimerCInterrupt\(\)", "mfp_trigger_timer_c_interrupt(&s_mfp)"),
    (r"_mfp\.TriggerUSARTReceiveInterrupt\(\)", "mfp_trigger_usart_receive_interrupt(&s_mfp)"),
    (r"_mfp\.TriggerUSARTReceiveError\(\)", "mfp_trigger_usart_receive_error(&s_mfp)"),
    (r"_mfp\.TriggerUSARTTransmitInterrupt\(\)", "mfp_trigger_usart_transmit_interrupt(&s_mfp)"),
    (r"_mfp\.TriggerUSARTTransmitError\(\)", "mfp_trigger_usart_transmit_error(&s_mfp)"),
    (r"_mfp\.Reset\(\)", "mfp_reset(&s_mfp)"),
    (r"_mfp\.UseSystemVectorMapping\b", "s_mfp.use_system_vector_mapping"),
    (r"_irqStates\.Count", "s_irq_count"),
    (r"_irqStates\.Clear\(\)", "s_irq_count = 0"),
    (r"_irqStates\[", "s_irq["),
    (r"_timerOutputs\.Count", "s_tout_count"),
    (r"_timerOutputs\.Clear\(\)", "s_tout_count = 0"),
    (r"_timerOutputs\[([^\]]+)\]\.name", r"s_tout[\1].name"),
    (r"_timerOutputs\[([^\]]+)\]\.state", r"s_tout[\1].state"),
    (r"_serialOutputs\.Count", "s_so_count"),
    (r"_serialOutputs\.Clear\(\)", "s_so_count = 0"),
    (r"_serialOutputs\[", "s_so["),
    (r"_gpioOutputs\.Count", "s_gpio_count"),
    (r"_gpioOutputs\.Clear\(\)", "s_gpio_count = 0"),
    (r"_gpioOutputs\[", "s_gpio["),
    (r"MfpTimerName\.([ABCD])\b", r"MFP_TIMER_NAME_\1"),
    (r"MFPRegister\.(\w+)", r"MFP_REGISTER_\1"),
    (r"\(byte\)", "(uint8_t)"),
    (r"\(ushort\)", "(uint16_t)"),
    (r"\bbyte\s+(\w+)\s*=", r"uint8_t \1 ="),
    (r"\bushort\s+(\w+)\s*=", r"uint16_t \1 ="),
    (r"\buint\s+(\w+)\s*=", r"uint32_t \1 ="),
]


def gpio_props(s):
    s = re.sub(r"_mfp\.GPIO_(\d)\s*=\s*([^;]+);", r"mfp_gpio_input(&s_mfp, \1, \2);", s)
    s = re.sub(r"_mfp\.GPIO_(\d)\b", r"mfp_get_gpio_pin_status(&s_mfp, \1)", s)
    s = re.sub(r"_mfp\.IEI\s*=\s*([^;]+);", r"mfp_set_iei(&s_mfp, \1);", s)
    return s


ASSERT = re.compile(r"^Assert\.That\((.*)\);$", re.S)


def split_args(s):
    out, depth, cur, instr = [], 0, "", False
    for ch in s:
        if ch == '"':
            instr = not instr
        if not instr:
            if ch in "([{":
                depth += 1
            elif ch in ")]}":
                depth -= 1
            elif ch == "," and depth == 0:
                out.append(cur.strip())
                cur = ""
                continue
        cur += ch
    out.append(cur.strip())
    return out


def assert_c(stmt):
    m = ASSERT.match(stmt)
    if not m:
        return None
    args = split_args(m.group(1))
    if len(args) == 1:
        return "CHECK(%s);" % args[0]
    a, c = args[0], args[1]
    msg = (" /* " + args[2].strip('"').replace("*/", "* /") + " */") if len(args) > 2 else ""
    for pat, fmt in [
        (r"^Is\.EqualTo\((.*)\)$", "CHECK_EQ({a}, {b});"),
        (r"^Is\.Not\.EqualTo\((.*)\)$", "CHECK(({a}) != ({b}));"),
        (r"^Is\.GreaterThan\((.*)\)$", "CHECK(({a}) > ({b}));"),
        (r"^Is\.LessThan\((.*)\)$", "CHECK(({a}) < ({b}));"),
        (r"^Is\.GreaterThanOrEqualTo\((.*)\)$", "CHECK(({a}) >= ({b}));"),
        (r"^Is\.LessThanOrEqualTo\((.*)\)$", "CHECK(({a}) <= ({b}));"),
        (r"^Is\.True$", "CHECK({a});"),
        (r"^Is\.False$", "CHECK(!({a}));"),
        (r"^Is\.Zero$", "CHECK_EQ({a}, 0);"),
    ]:
        mm = re.match(pat, c)
        if mm:
            b = mm.group(1) if mm.groups() else ""
            return fmt.format(a=a, b=b) + msg
    return None


def strip_trailing_comment(line):
    """Drop a // comment that follows code (not inside a string)."""
    if line.startswith("//"):
        return line
    instr = False
    for i in range(len(line) - 1):
        if line[i] == '"' and (i == 0 or line[i - 1] != "\\"):
            instr = not instr
        if not instr and line[i] == "/" and line[i + 1] == "/":
            return line[:i].rstrip()
    return line


def translate_body(body):
    out = []
    # join statements: C# statements end with ';' or are block lines
    lines = body.split("\n")
    buf = ""
    for raw in lines:
        line = strip_trailing_comment(raw.strip())
        if not line:
            if not buf:
                out.append("")
            continue
        if line.startswith("//"):
            if not buf:
                out.append("    " + line)
            continue
        buf = (buf + " " + line) if buf else line
        if buf.endswith(";") or buf.endswith("{") or buf.endswith("}") or buf.startswith("#"):
            out.append(buf)
            buf = ""
    if buf:
        out.append(buf)
    res = []
    for stmt in out:
        if stmt == "" or stmt.startswith("    //"):
            res.append(stmt)
            continue
        s = gpio_props(stmt)
        for pat, rep in CALLS:
            s = re.sub(pat, rep, s)
        s = re.sub(r"\btrue\b", "true", s)
        if s.startswith("Assert.Pass("):
            res.append("    CHECK(true); /* C# Assert.Pass: %s */" % s[len("Assert.Pass("):-2].strip('"'))
            continue
        a = assert_c(s)
        if a is not None:
            if re.search(r"(?<![\w])_mfp\.|=>|\$\"", a):
                res.append("PORT-TODO: " + a)
            else:
                res.append("    " + a)
            continue
        if re.search(r"(?<![\w])_mfp\.|Assert\.|=>|\bvar\b|new |List<|\.Add\(|foreach|\bstring\b|\$\"", s):
            res.append("PORT-TODO: " + s)
            continue
        res.append("    " + s)
    return "\n".join(res)


print('''/*
 * test_eth_mfp.c - Port of RetroCore MC68901MFPTests.cs (Emulated.Tests.Chips).
 *
 * nd100x - ND100 Virtual Machine
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (c) 2026 Ronny Hansen
 *
 * One ETH_TEST per C# [Test], same name, same asserted values. First pass
 * made by tools/ethport/port_mfp_tests.py, then reviewed and finished by hand
 * against the C# (RetroCore commit 935163f). The C# [SetUp] is setup() here,
 * called first in every test.
 */

#include "eth_test.h"
#include "eth_mfp.h"

#include <string.h>

#define BASE_ADDR 0xFF8800u
#define GPDR (BASE_ADDR + 0x01u)
#define AER (BASE_ADDR + 0x03u)
#define DDR (BASE_ADDR + 0x05u)
#define IERA (BASE_ADDR + 0x07u)
#define IERB (BASE_ADDR + 0x09u)
#define IPRA (BASE_ADDR + 0x0Bu)
#define IPRB (BASE_ADDR + 0x0Du)
#define ISRA (BASE_ADDR + 0x0Fu)
#define ISRB (BASE_ADDR + 0x11u)
#define IMRA (BASE_ADDR + 0x13u)
#define IMRB (BASE_ADDR + 0x15u)
#define VR (BASE_ADDR + 0x17u)
#define TACR (BASE_ADDR + 0x19u)
#define TBCR (BASE_ADDR + 0x1Bu)
#define TCDCR (BASE_ADDR + 0x1Du)
#define TADR (BASE_ADDR + 0x1Fu)
#define TBDR (BASE_ADDR + 0x21u)
#define TCDR (BASE_ADDR + 0x23u)
#define TDDR (BASE_ADDR + 0x25u)
#define SCR (BASE_ADDR + 0x27u)
#define UCR (BASE_ADDR + 0x29u)
#define RSR (BASE_ADDR + 0x2Bu)
#define TSR (BASE_ADDR + 0x2Du)
#define UDR (BASE_ADDR + 0x2Fu)

#define REC_MAX 4096

static MC68901MFP s_mfp;
static bool s_irq[REC_MAX];
static int s_irq_count;
static struct
{
    MfpTimerName name;
    bool state;
} s_tout[REC_MAX];
static int s_tout_count;
static bool s_so[REC_MAX];
static int s_so_count;
static uint8_t s_gpio[REC_MAX];
static int s_gpio_count;

static void rec_irq(void *ctx, bool state)
{
    (void)ctx;
    if (s_irq_count < REC_MAX)
    {
        s_irq[s_irq_count++] = state;
    }
}

static void rec_tout(void *ctx, MfpTimerName name, bool state)
{
    (void)ctx;
    if (s_tout_count < REC_MAX)
    {
        s_tout[s_tout_count].name = name;
        s_tout[s_tout_count].state = state;
        s_tout_count++;
    }
}

static void rec_so(void *ctx, bool state)
{
    (void)ctx;
    if (s_so_count < REC_MAX)
    {
        s_so[s_so_count++] = state;
    }
}

static void rec_gpio(void *ctx, uint8_t data)
{
    (void)ctx;
    if (s_gpio_count < REC_MAX)
    {
        s_gpio[s_gpio_count++] = data;
    }
}

/* [SetUp] Setup() */
static void setup(void)
{
    MfpEvents ev;

    memset(&ev, 0, sizeof ev);
    ev.on_irq = rec_irq;
    ev.on_timer_output = rec_tout;
    ev.on_serial_output = rec_so;
    ev.on_gpio = rec_gpio;
    mfp_create(&s_mfp, BASE_ADDR, 0x30u, &ev);
    s_irq_count = 0;
    s_tout_count = 0;
    s_so_count = 0;
    s_gpio_count = 0;
    mfp_reset(&s_mfp);
}
''')

for name, attrs, body, line in methods:
    flags = []
    marks = []
    if re.search(r"\[Ignore", attrs):
        flags.append("ETH_TEST_IGNORE")
        marks.append("cs-ignore: see C#")
    if re.search(r"\[Explicit", attrs):
        flags.append("ETH_TEST_EXPLICIT")
        marks.append("cs-explicit")
    head = ("ETH_TEST_FLAGS(MC68901MFPTests, %s, %s)" % (name, " | ".join(flags))) if flags \
        else "ETH_TEST(MC68901MFPTests, %s)" % name
    mark = (" /* %s */" % "; ".join(marks)) if marks else ""
    print("/* C#: MC68901MFPTests.cs:%d */" % line)
    print(head + mark)
    print("{")
    print("    setup();")
    print(translate_body(body))
    print("}")
    print()
sys.stderr.write("tests: %d\n" % len(methods))
