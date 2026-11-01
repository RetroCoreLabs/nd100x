/*
 * nd100x - ND-100 emulator
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (c) 2026 Ronny Hansen
 *
 * scsi_tape.h - SCSI streamer target: Tandberg TDC 3600 series
 *
 * The drive ND sold as its SCSI streamer. SCSI-TV (the ND SCSI test program)
 * accepts "TANDBERG" / " TDC 3600" as the streamer, and its device test 3 note
 * says the extended self test "will perform no operation" on that drive.
 *
 * Everything here is from the Tandberg Data "TDC 3620/3640/3660 Streaming
 * Tape Cartridge Drive Reference Manual", chapter 9 (software interface) and
 * appendices A and B. Section numbers in the comments are from that manual;
 * page numbers are the manual's own (9-NN).
 *
 * ASSUMPTION, not verified: ND's "125 MB streamer" is the TDC 3640 (125 MB on
 * a 600 ft cartridge, QIC-120 only). INQUIRY returns " TDC 3600" for every
 * member of the series, so the answer does not tell them apart.
 *
 * The structure follows MAME's nscsi_tape_device (src/devices/bus/nscsi/
 * tape.cpp) and RetroCore's SCSITape.cs, but the behaviour is the TDC's, not
 * MAME's generic SCSI-2 drive: fixed 512-byte blocks, the TDC's own sense
 * block (25 bytes, with the error code and the extended error code), its
 * vendor mode parameters, Request Block Address / Seek Block, the append rule.
 *
 * The tape itself is a SIMH .tap image (tape_image.h), reached through the
 * controller's block callbacks.
 */

#ifndef SCSI_TAPE_H
#define SCSI_TAPE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "scsi_device.h"
#include "tape_image.h"

/* Command opcodes of the TDC 3600 (section 9.2, command summary). */
// clang-format off
#define TDC_TEST_UNIT_READY      0x00
#define TDC_REWIND               0x01
#define TDC_REQUEST_BLOCK_ADDR   0x02
#define TDC_REQUEST_SENSE        0x03
#define TDC_READ_BLOCK_LIMITS    0x05
#define TDC_READ                 0x08
#define TDC_WRITE                0x0A
#define TDC_SEEK_BLOCK           0x0C
#define TDC_WRITE_FILEMARK       0x10
#define TDC_SPACE                0x11
#define TDC_INQUIRY              0x12
#define TDC_VERIFY               0x13
#define TDC_RECOVER_BUFFERED     0x14
#define TDC_MODE_SELECT          0x15
#define TDC_RESERVE_UNIT         0x16
#define TDC_RELEASE_UNIT         0x17
#define TDC_COPY                 0x18
#define TDC_ERASE                0x19
#define TDC_MODE_SENSE           0x1A
#define TDC_LOAD_UNLOAD          0x1B
#define TDC_SEND_DIAGNOSTIC      0x1D
#define TDC_PREVENT_ALLOW        0x1E
/* Section 9.3.10 and 9.3.23: on this drive READ DATA BUFFER is 3Bh and WRITE
 * DATA BUFFER is 3Ch - the other way round from SCSI-2 (where WRITE BUFFER is
 * 3Bh). Both the headings and the CDB bit patterns in the manual say so. */
#define TDC_READ_DATA_BUFFER     0x3B
#define TDC_WRITE_DATA_BUFFER    0x3C
// clang-format on

/* The only block length the drive has (section 9.3.9: "Since the block length
 * is fixed to 512 bytes for QIC-11, QIC-24, QIC-120 and QIC-150 ..."). */
#define TDC_BLOCK_BYTES 512

/* INQUIRY data: five-byte header plus 46 (section 9.3.3). */
#define TDC_INQUIRY_BYTES 51

/* Extended sense block, bytes 0 to 24: additional sense length 17 in the
 * normal (non-Copy) case (section 9.3.14.2). */
#define TDC_SENSE_BYTES    25
#define TDC_SENSE_ADD_LEN  17
/* Standard (normal) sense block (section 9.3.14.1). */
#define TDC_NORMAL_SENSE_BYTES 4

/* Mode Select / Mode Sense parameter list: four-byte header, one 8-byte block
 * descriptor, 12 vendor bytes, "in total 24 bytes" (section 9.3.5). */
#define TDC_MODE_HEADER_BYTES 4
#define TDC_MODE_DESC_BYTES   8
#define TDC_MODE_VENDOR_BYTES 12
#define TDC_MODE_LIST_BYTES   24

/* The data buffer used by Read/Write Data Buffer. Section 9.3.10: the
 * available length "may be up to 65.531 bytes (64 Kbytes minus a 4-byte
 * header), or the Drive's maximum buffer size, whichever is less". Section
 * 9.3.5 gives 16 and 64 KB buffer versions, and a default buffer size of 120
 * blocks "for a Drive with 64 KByte buffer". ASSUMPTION: the ND drive has the
 * 64 KB buffer, so the available length is 65531. */
#define TDC_DATA_BUFFER_BYTES     65536
#define TDC_DATA_BUFFER_AVAILABLE 65531
#define TDC_DATA_BUFFER_HEADER    4

/* "Pseudo Early Warning" (section 9.3.22, Write): the drive stops a write and
 * reports end of medium when it writes past it, and "between Pseudo Early
 * Warning and the physical End Of Tape, the Drive will normally be able to
 * write 200 KBytes of data". The image has no tracks or feet, so PEW is put
 * 200 KB before the end of the image. */
#define TDC_PEW_BYTES (200u * 1024u)

/* What the drive is doing, for the block and filemark counters of the sense
 * block: they are "cleared when a transition is made from one mode (Read,
 * Write or Position) to Read or Write mode" (section 9.3.14.2). */
// clang-format off
typedef enum {
    TDC_MODE_NONE = 0,
    TDC_MODE_READ,
    TDC_MODE_WRITE,
    TDC_MODE_POSITION
} TDCOperationMode;
// clang-format on

// clang-format off
typedef struct {
    SCSITarget target;      /* must be first - SCSITarget.impl points back here */

    /* The owning ND SCSI controller Device, used only for its block callbacks. */
    struct Device *owner;
    int unit;               /* SCSI id, also the unit for the callbacks */

    TapeImage tape;
    bool present;           /* an image (a cartridge) is mounted */
    bool loaded;            /* not unloaded by Load/Unload */

    /* Unit Attention (section 8.6.2): after power-on, a bus reset or a new
     * cartridge, the next command (other than INQUIRY) gets CHECK CONDITION /
     * UNIT ATTENTION and is not performed. */
    bool unitAttention;

    /* Sense data for the last command (section 9.3.14.2 layout). */
    uint8_t sense[TDC_SENSE_BYTES];

    /* Counters reported in the sense block. */
    TDCOperationMode opMode;
    uint32_t blockCounter;      /* 24 bits in the sense block */
    uint16_t filemarkCounter;

    bool lastWasWrite;      /* a Write / Write File Mark with no rewind since */
    bool writeError;        /* a write failed; the next Write is refused */
    bool pewReported;       /* the Pseudo Early Warning was reported */

    /* Mode parameters, current and saved (section 9.3.5). SMP saves them;
     * a reset loads the saved ones. Kept in memory only. */
    uint8_t density;
    bool    bufferedMode;
    uint8_t vendor[TDC_MODE_VENDOR_BYTES];
    uint8_t savedDensity;
    bool    savedBufferedMode;
    uint8_t savedVendor[TDC_MODE_VENDOR_BYTES];

    /* The block being moved by a Read, Write or Verify, and the transfer. */
    uint8_t  block[TDC_BLOCK_BYTES];
    uint32_t xferBlocks;    /* blocks the CDB asked for */
    uint32_t xferDone;      /* blocks moved so far */
    bool     verifyMismatch; /* Verify with byte compare: a byte differed */

    /* Mode Select parameter list, filled by the DATA OUT phase. */
    uint8_t modeSelect[TDC_MODE_LIST_BYTES];
    int     modeSelectLength;
    bool    modeSelectSave;

    /* Command reply staged for a DATA IN phase (INQUIRY, sense, mode data,
     * block limits, block address). */
    uint8_t reply[TDC_SENSE_BYTES + TDC_INQUIRY_BYTES];

    uint8_t dataBuffer[TDC_DATA_BUFFER_BYTES];
} SCSITapeDevice;
// clang-format on

/**
 * @brief Attach a TDC 3600 streamer to the bus at scsi_id, its tape the image
 *        mounted on owner's unit.
 * @param tape Device state to initialize; zeroed and filled in.
 * @param bus SCSI bus to attach the target to.
 * @param scsi_id SCSI id the target answers to.
 * @param owner Device whose block callbacks reach the tape image.
 * @param unit Unit number on owner.
 */
void scsi_tape_init(SCSITapeDevice *tape, SCSIBus *bus, uint8_t scsi_id, struct Device *owner,
                    int unit);

/**
 * @brief Write any cached part of the tape image back to the image.
 * @param tape Device state.
 */
void scsi_tape_flush(SCSITapeDevice *tape);

#endif // SCSI_TAPE_H
