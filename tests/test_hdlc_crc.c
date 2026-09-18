/*
 * Unit tests for HDLC FCS (CRC-16-CCITT reflected).
 */

#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>

#include "../src/devices/hdlc/hdlcFrame.h"
#include "../src/devices/hdlc/hdlc_crc.h"
#include "test_suites.h"

static int failures = 0;

#define ASSERT_EQ(a, b, msg) do { \
    if ((a) != (b)) { \
        printf("  FAIL: %s: expected 0x%04X, got 0x%04X (line %d)\n", msg, (unsigned)(b), (unsigned)(a), __LINE__); \
        failures++; \
    } \
} while(0)

#define ASSERT(cond, msg) do { \
    if (!(cond)) { \
        printf("  FAIL: %s (line %d)\n", msg, __LINE__); \
        failures++; \
    } \
} while(0)

static void test_fcs_good_residue(void)
{
    printf("  test_fcs_good_residue...");
    // Standard HDLC FCS check: CRC over data+FCS should give 0xF0B8
    uint8_t data[] = {0x01, 0x02, 0x03};
    uint16_t fcs = 0xFFFF;
    for (int i = 0; i < 3; i++) {
        fcs = HDLC_CRC_CalcCCITT(fcs, data[i]);
    }
    uint16_t complement = fcs ^ 0xFFFF;

    // Now feed data + FCS bytes back through CRC
    uint16_t check = 0xFFFF;
    for (int i = 0; i < 3; i++) {
        check = HDLC_CRC_CalcCCITT(check, data[i]);
    }
    check = HDLC_CRC_CalcCCITT(check, complement & 0xFF);
    check = HDLC_CRC_CalcCCITT(check, (complement >> 8) & 0xFF);

    ASSERT_EQ(check, 0xF0B8, "FCS residue is 0xF0B8");
    printf(" ok\n");
}

static void test_fcs_deterministic(void)
{
    printf("  test_fcs_deterministic...");
    uint8_t data[] = {0xAA, 0xBB, 0xCC};
    uint16_t crc1 = HDLCFrame_CalculateCRC(data, 3);
    uint16_t crc2 = HDLCFrame_CalculateCRC(data, 3);
    ASSERT_EQ(crc1, crc2, "CRC deterministic");
    printf(" ok\n");
}

static void test_fcs_incremental_matches_batch(void)
{
    printf("  test_fcs_incremental_matches_batch...");
    uint8_t data[] = {0x01, 0x02, 0x03, 0x04};
    uint16_t batch = HDLCFrame_CalculateCRC(data, 4);

    uint16_t incr = 0xFFFF;
    for (int i = 0; i < 4; i++) {
        incr = HDLCFrame_UpdateCRC(incr, data[i]);
    }
    ASSERT_EQ(incr, batch, "incremental matches batch");
    printf(" ok\n");
}

static void test_fcs_empty(void)
{
    printf("  test_fcs_empty...");
    uint16_t crc = HDLCFrame_CalculateCRC(NULL, 0);
    ASSERT_EQ(crc, 0xFFFF, "empty data stays init");
    printf(" ok\n");
}

static void test_real_frame_crc(void)
{
    printf("  test_real_frame_crc...");
    // Real HDLC frame from wire: 7E 01 73 00 67 60 98 7E
    // Destuffed content: 01 73 00 67 60 98
    // Feeding all 6 bytes through CRC should give 0xF0B8
    uint8_t content[] = {0x01, 0x73, 0x00, 0x67, 0x60, 0x98};
    uint16_t crc = 0xFFFF;
    for (int i = 0; i < 6; i++) {
        crc = HDLC_CRC_CalcCCITT(crc, content[i]);
    }
    ASSERT_EQ(crc, 0xF0B8, "real frame CRC residue");
    printf(" ok\n");
}

static void test_real_frame_via_process_byte(void)
{
    printf("  test_real_frame_via_process_byte...");
    // Feed the user's exact frame through HDLCFrame_AddByte
    uint8_t wire[] = {0x7E, 0x01, 0x73, 0x00, 0x67, 0x60, 0x98, 0x7E};
    HDLCFrame frame;
    HDLCFrame_Init(&frame);

    bool complete = false;
    for (int i = 0; i < 8; i++) {
        complete = HDLCFrame_AddByte(&frame, wire[i]);
        if (complete) break;
    }

    ASSERT(complete, "frame complete");
    ASSERT(HDLCFrame_IsCRCValid(&frame), "CRC valid");
    ASSERT_EQ(HDLCFrame_GetFrameLength(&frame), 6, "6 bytes (data+FCS)");

    // Data portion (excluding 2-byte FCS): 01 73 00 67
    const uint8_t *data = HDLCFrame_GetFrameData(&frame);
    ASSERT_EQ(data[0], 0x01, "addr byte");
    ASSERT_EQ(data[1], 0x73, "control byte");
    ASSERT_EQ(data[2], 0x00, "data[0]");
    ASSERT_EQ(data[3], 0x67, "data[1]");
    printf(" ok\n");
}

int run_hdlc_crc_tests(void)
{
    failures = 0;

    test_fcs_good_residue();
    test_fcs_deterministic();
    test_fcs_incremental_matches_batch();
    test_fcs_empty();
    test_real_frame_crc();
    test_real_frame_via_process_byte();

    if (failures == 0) {
        printf("  All hdlc_crc tests passed.\n");
    }
    return failures;
}
