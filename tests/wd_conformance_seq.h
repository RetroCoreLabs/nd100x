/**************************************************************************
** WINCHESTER CONFORMANCE SEQUENCE - shared between two implementations   **
**                                                                        **
** A scripted IOX sequence plus the trace it is expected to produce. Two   **
** independent Winchester cores walk this same table and print the same    **
** canonical trace; the traces are then diffed. That is what makes         **
** "ported" mean BIT-IDENTICAL rather than "looks similar":                **
**                                                                        **
**   nd100x  (synchronous model) tests/wd_conformance_seq.h                **
**   Pico    (async phase machine) test/wd_conformance_seq.h               **
**                                                                        **
** THESE TWO FILES MUST BE BYTE-IDENTICAL. If you change one, copy it to   **
** the other. A step-count or content drift shows up immediately as a      **
** diff in the traces, which is the point.                                 **
**                                                                        **
** Only observable REGISTER behaviour is scripted here - the register      **
** load/read protocol, the status word, and IDENT. Transferred DATA is     **
** deliberately not compared: the two harnesses have different fake media,  **
** and the data path is covered by each side's own testbench.              **
**                                                                        **
** Ronny Hansen                                                           **
***************************************************************************/

#ifndef WD_CONFORMANCE_SEQ_H
#define WD_CONFORMANCE_SEQ_H

/* Step opcodes. The driver on each side implements these five against its
 * own core, then prints one trace line per step. */
typedef enum
{
    WDS_RESET = 0, /* master clear                                        */
    WDS_WRITE,     /* IOX write:  arg = register offset, val = value      */
    WDS_READ,      /* IOX read:   arg = register offset; prints the value */
    WDS_IDENT,     /* IDENT:      arg = level;           prints the code  */
    WDS_INTBITS,   /* print the pending interrupt bit for level 11        */
    WDS_SETTLE     /* run the core to completion (a no-op when the model
                     * is synchronous); prints nothing                     */
} wds_op;

typedef struct
{
    wds_op op;
    unsigned arg; /* register offset, or IDENT level */
    unsigned val; /* value for WDS_WRITE             */
    const char *what;
} wds_step;

/* Register offsets. */
// clang-format off
#define WDS_R_READ_MA     0
#define WDS_R_LOAD_MA     1
#define WDS_R_READ_SECT   2
#define WDS_R_LOAD_BLOCK  3
#define WDS_R_STATUS      4
#define WDS_R_CONTROL     5
#define WDS_R_READ_BLOCK  6
#define WDS_R_LOAD_WC     7
// clang-format on

/* Control-word bits. */
// clang-format off
#define WDS_CW_INT_EN   0001
#define WDS_CW_ERR_INT  0002
#define WDS_CW_ACTIVE   0004
#define WDS_CW_TEST     0010
#define WDS_CW_DEVCLR   0020
// clang-format on

// clang-format off
static const wds_step WD_CONFORMANCE_SEQ[] = {

    /* ---- 1. power-on state ------------------------------------------- */
    { WDS_RESET,   0, 0, "master clear" },
    { WDS_READ,    WDS_R_STATUS, 0, "status after master clear" },
    { WDS_INTBITS, 0, 0, "level 11 quiet after master clear" },

    /* ---- 2. memory address: write HI-then-LO, read LO-then-HI --------- */
    { WDS_WRITE, WDS_R_LOAD_MA, 0x0012, "MA write 1 (HI byte)" },
    { WDS_WRITE, WDS_R_LOAD_MA, 0x3456, "MA write 2 (LO 16)" },
    { WDS_READ,  WDS_R_READ_MA, 0,      "MA read 1 (must be the LO 16)" },
    { WDS_READ,  WDS_R_READ_MA, 0,      "MA read 2 (must be the HI 8)" },

    /* A third read wraps back to the LO word - the flip-flop toggles. */
    { WDS_READ,  WDS_R_READ_MA, 0,      "MA read 3 (flip-flop wrapped)" },

    /* ---- 3. a status read resets the write flip-flop ------------------ */
    { WDS_WRITE, WDS_R_LOAD_MA, 0x0077, "MA write left mid-sequence" },
    { WDS_READ,  WDS_R_STATUS,  0,      "status read (resets the FFs)" },
    { WDS_WRITE, WDS_R_LOAD_MA, 0x0011, "MA write 1 again (HI byte)" },
    { WDS_WRITE, WDS_R_LOAD_MA, 0x2222, "MA write 2 again (LO 16)" },
    { WDS_READ,  WDS_R_READ_MA, 0,      "MA read back LO" },
    { WDS_READ,  WDS_R_READ_MA, 0,      "MA read back HI" },

    /* ---- 4. word count is a SINGLE access ----------------------------- *
     * The property that identifies this card. On a two-access card the
     * second write would land in a HI byte and the trace would diverge. */
    { WDS_WRITE, WDS_R_LOAD_WC, 02000, "word count 002000 in ONE write" },
    { WDS_WRITE, WDS_R_LOAD_WC, 00777, "word count REPLACED, not HI-latched" },

    /* ---- 5. block address split: cylinder b15-5, sector b4-0 ---------- */
    { WDS_WRITE, WDS_R_LOAD_BLOCK, (17u << 5) | 6u, "block address cyl 17 sect 6" },
    { WDS_READ,  WDS_R_READ_BLOCK, 0, "block address reads back" },
    { WDS_WRITE, WDS_R_LOAD_BLOCK, 0xFFFF, "block address all ones" },
    { WDS_READ,  WDS_R_READ_BLOCK, 0, "block address all ones reads back" },

    /* ---- 6. control word: unit, head, test mode ----------------------- */
    { WDS_WRITE, WDS_R_CONTROL, 0, "control: unit 0, no flags" },
    { WDS_READ,  WDS_R_STATUS,  0, "status after an idle control word" },
    { WDS_WRITE, WDS_R_CONTROL, WDS_CW_TEST, "control: test mode" },
    { WDS_READ,  WDS_R_STATUS,  0, "status in test mode" },
    { WDS_WRITE, WDS_R_CONTROL, 0x0200, "control: unit 1 (bit 9)" },
    { WDS_READ,  WDS_R_STATUS,  0, "status with unit 1 selected" },

    /* ---- 7. device clear zeroes the transfer registers ---------------- */
    { WDS_WRITE, WDS_R_LOAD_MA,    0x0044, "MA HI before device clear" },
    { WDS_WRITE, WDS_R_LOAD_MA,    0x5555, "MA LO before device clear" },
    { WDS_WRITE, WDS_R_LOAD_WC,    01234,  "word count before device clear" },
    { WDS_WRITE, WDS_R_LOAD_BLOCK, 0x0123, "block address before device clear" },
    { WDS_WRITE, WDS_R_CONTROL,    WDS_CW_DEVCLR, "device clear" },
    { WDS_READ,  WDS_R_READ_MA,    0, "MA LO after device clear (must be 0)" },
    { WDS_READ,  WDS_R_READ_MA,    0, "MA HI after device clear (must be 0)" },
    { WDS_READ,  WDS_R_READ_BLOCK, 0, "block address after device clear" },
    { WDS_READ,  WDS_R_STATUS,     0, "status after device clear" },

    /* ---- 8. the interrupt / IDENT probe (sec 4.1) --------------------- *
     * This is the TPE CONFIGURATION sequence. It is in the shared file
     * because it is exactly the behaviour that differed once already. */
    { WDS_RESET,   0, 0, "master clear before the probe" },
    { WDS_INTBITS, 0, 0, "quiet before arming" },
    { WDS_WRITE,   WDS_R_CONTROL, WDS_CW_INT_EN, "arm the interrupt on an idle card" },
    { WDS_READ,    WDS_R_STATUS,  0, "status with the interrupt armed" },
    { WDS_INTBITS, 0, 0, "BINT11 asserted on an idle armed card" },
    { WDS_IDENT,   11, 0, "IDENT PL11 (must answer 1)" },
    { WDS_INTBITS, 0, 0, "IDENT cleared the line" },
    { WDS_IDENT,   11, 0, "second IDENT with nothing pending (must be 0)" },

    /* Wrong-level IDENT is never answered and never clears the line. */
    { WDS_WRITE,   WDS_R_CONTROL, WDS_CW_INT_EN, "re-arm" },
    { WDS_IDENT,   10, 0, "IDENT PL10 (must be 0)" },
    { WDS_IDENT,   13, 0, "IDENT PL13 (must be 0)" },
    { WDS_INTBITS, 0, 0, "level 11 still pending after a wrong-level IDENT" },
    { WDS_IDENT,   11, 0, "IDENT PL11 finally answers" },

    /* Device clear AND interrupt enable in ONE control word. */
    { WDS_WRITE,   WDS_R_CONTROL, WDS_CW_DEVCLR | WDS_CW_INT_EN,
      "device clear + interrupt enable together" },
    { WDS_INTBITS, 0, 0, "the combined word still interrupts" },
    { WDS_WRITE,   WDS_R_CONTROL, 0, "drop the enable" },
    { WDS_INTBITS, 0, 0, "dropping the enable drops BINT11" },

    /* ---- 9. a real M0 read: activation, then the end state ------------ */
    { WDS_RESET, 0, 0, "master clear before the transfer" },
    { WDS_WRITE, WDS_R_LOAD_MA,    0x0000, "MA HI = 0" },
    { WDS_WRITE, WDS_R_LOAD_MA,    0x0100, "MA LO = 0400 octal" },
    { WDS_WRITE, WDS_R_LOAD_BLOCK, 0x0000, "block address: cylinder 0 sector 0" },
    { WDS_WRITE, WDS_R_LOAD_WC,    512,    "word count = one sector" },
    { WDS_WRITE, WDS_R_CONTROL,    WDS_CW_INT_EN | WDS_CW_ACTIVE, "GO: M0 read" },
    { WDS_SETTLE, 0, 0, "run to completion" },
    { WDS_READ,  WDS_R_STATUS,  0, "status after the transfer" },
    { WDS_INTBITS, 0, 0, "completion raised the interrupt" },
    { WDS_IDENT, 11, 0, "IDENT after the transfer" },
    { WDS_READ,  WDS_R_READ_MA, 0, "MA LO advanced by the words moved" },
    { WDS_READ,  WDS_R_READ_MA, 0, "MA HI after the transfer" },

    /* ---- 10. activation resets the write flip-flop -------------------- */
    { WDS_WRITE, WDS_R_LOAD_MA, 0x0099, "MA write left mid-sequence" },
    { WDS_WRITE, WDS_R_LOAD_WC, 0,      "zero-length transfer" },
    { WDS_WRITE, WDS_R_CONTROL, WDS_CW_ACTIVE, "GO with the FF mid-sequence" },
    { WDS_SETTLE, 0, 0, "run to completion" },
    { WDS_WRITE, WDS_R_LOAD_MA, 0x0066, "MA write 1 (must be taken as HI)" },
    { WDS_WRITE, WDS_R_LOAD_MA, 0x7777, "MA write 2 (LO)" },
    { WDS_READ,  WDS_R_READ_MA, 0, "MA LO confirms the FF was reset" },
    { WDS_READ,  WDS_R_READ_MA, 0, "MA HI confirms the FF was reset" },

    /* ---- 11. M6 is never activated ------------------------------------ */
    { WDS_RESET, 0, 0, "master clear" },
    { WDS_WRITE, WDS_R_CONTROL, (6u << 11) | WDS_CW_ACTIVE, "M6 with the activate bit" },
    { WDS_READ,  WDS_R_STATUS, 0, "status: M6 must not have gone active" },

    /* ---- 12. IOX +2 is "Not used" (sec 3.1 table), so it reads 0 ------ *
     * Both implementations modelled a readable sector counter here until
     * this cross-check sent us back to the manual's own register table. */
    { WDS_READ, WDS_R_READ_SECT, 0, "+2 is Not used - must read 0" },

    /* ---- 13. write-only registers read 0 ------------------------------ */
    { WDS_READ, WDS_R_LOAD_MA,    0, "+1 is write-only" },
    { WDS_READ, WDS_R_LOAD_BLOCK, 0, "+3 is write-only" },
    { WDS_READ, WDS_R_CONTROL,    0, "+5 is write-only" },
    { WDS_READ, WDS_R_LOAD_WC,    0, "+7 is write-only" }
};
// clang-format on

#define WD_CONFORMANCE_SEQ_LEN (sizeof(WD_CONFORMANCE_SEQ) / sizeof(WD_CONFORMANCE_SEQ[0]))

#endif /* WD_CONFORMANCE_SEQ_H */
