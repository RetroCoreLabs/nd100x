/*
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

static uint8_t s_chain_vector;
static bool s_ieo_state;
static struct
{
    MFPRegister reg;
    uint8_t value;
} s_rw[64];
static int s_rw_count;
static bool s_rw_only_isrb;

static uint8_t chain_cb(void *ctx)
{
    (void)ctx;
    return s_chain_vector;
}

static void rec_ieo(void *ctx, bool state)
{
    (void)ctx;
    s_ieo_state = state;
}

static void rec_rw(void *ctx, MFPRegister reg, uint8_t value)
{
    (void)ctx;
    if (s_rw_only_isrb && (reg != MFP_REGISTER_ISRB))
    {
        return;
    }
    if (s_rw_count < 64)
    {
        s_rw[s_rw_count].reg = reg;
        s_rw[s_rw_count].value = value;
        s_rw_count++;
    }
}

/* Helper method to clock one bit into the receiver (/1 mode) */
static void ClockInBit(bool bit)
{
    mfp_set_serial_input(&s_mfp, bit);
    mfp_set_receiver_clock(&s_mfp, false);
    mfp_set_receiver_clock(&s_mfp, true);
}

/* Helper method to clock in multiple bits for /16 mode (16 clocks per bit) */
static void ClockInBit16(bool bit)
{
    mfp_set_serial_input(&s_mfp, bit);
    for (int i = 0; i < 16; i++)
    {
        mfp_set_receiver_clock(&s_mfp, false);
        mfp_set_receiver_clock(&s_mfp, true);
    }
}

/* RSR flag constants for test assertions (MC68901MFPTests.cs:1001-1006) */
#define RSR_BUFFER_FULL 0x80u
#define RSR_OVERRUN_ERROR 0x40u
#define RSR_PARITY_ERROR 0x20u
#define RSR_FRAME_ERROR 0x10u
#define RSR_BREAK 0x08u
#define RSR_RCV_ENABLE 0x01u

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
    s_rw_only_isrb = false;
    mfp_reset(&s_mfp);
}

/* C#: MC68901MFPTests.cs:77 */
ETH_TEST(MC68901MFPTests, Test_GPIO_DDR_InputMode)
{
    setup();

    // DDR = 0 means all pins are inputs
    mfp_write(&s_mfp, DDR, 0x00);

    // Set external input pins
    mfp_gpio_input(&s_mfp, 0, true);
    mfp_gpio_input(&s_mfp, 7, true);

    // Reading GPDR should return input values for DDR=0 pins
    uint8_t gpdr = mfp_read(&s_mfp, GPDR);
    CHECK_EQ(gpdr & 0x01, 0x01); /* GPIO 0 should read as high */
    CHECK_EQ(gpdr & 0x80, 0x80); /* GPIO 7 should read as high */

}

/* C#: MC68901MFPTests.cs:94 */
ETH_TEST(MC68901MFPTests, Test_GPIO_DDR_OutputMode)
{
    setup();

    // DDR = 0xFF means all pins are outputs
    mfp_write(&s_mfp, DDR, 0xFF);

    // Write output value
    mfp_write(&s_mfp, GPDR, 0xAA);

    // Reading GPDR should return written value for DDR=1 pins
    uint8_t gpdr = mfp_read(&s_mfp, GPDR);
    CHECK_EQ(gpdr, 0xAA); /* GPIO should return output value */

}

/* C#: MC68901MFPTests.cs:109 */
ETH_TEST(MC68901MFPTests, Test_GPIO_ReadInputPins)
{
    setup();

    // Mixed DDR: lower nibble = input, upper nibble = output
    mfp_write(&s_mfp, DDR, 0xF0);
    mfp_write(&s_mfp, GPDR, 0xA0);

    // Set input on lower nibble
    mfp_gpio_input(&s_mfp, 0, true);
    mfp_gpio_input(&s_mfp, 1, false);
    mfp_gpio_input(&s_mfp, 2, true);
    mfp_gpio_input(&s_mfp, 3, false);

    uint8_t gpdr = mfp_read(&s_mfp, GPDR);
    CHECK_EQ(gpdr & 0x0F, 0x05); /* Lower nibble should be input (0101) */
    CHECK_EQ(gpdr & 0xF0, 0xA0); /* Upper nibble should be output (1010) */

}

/* C#: MC68901MFPTests.cs:128 */
ETH_TEST(MC68901MFPTests, Test_GPIO_WriteOutputPins)
{
    setup();

    s_gpio_count = 0;

    // Set DDR for output
    mfp_write(&s_mfp, DDR, 0xFF);

    // Write to GPDR
    mfp_write(&s_mfp, GPDR, 0x55);

    CHECK((s_gpio_count) > (0)); /* OnGPIO should be called */
    CHECK_EQ(s_gpio[s_gpio_count - 1], 0x55); /* GPIO output should be 0x55 */

}

/* C#: MC68901MFPTests.cs:144 */
ETH_TEST(MC68901MFPTests, Test_GPIO_EdgeDetection_RisingEdge)
{
    setup();

    // Configure GPIO 0 for rising edge (AER bit 0 = 1)
    mfp_write(&s_mfp, AER, 0x01);
    mfp_write(&s_mfp, IERA, 0x00);
    mfp_write(&s_mfp, IERB, 0x01);
    mfp_write(&s_mfp, IMRB, 0x01);

    s_irq_count = 0;

    // Set GPIO 0 low first
    mfp_gpio_input(&s_mfp, 0, false);

    // Now trigger rising edge
    mfp_gpio_input(&s_mfp, 0, true);

    // Check interrupt was triggered
    CHECK((s_irq_count) > (0)); /* IRQ should be triggered on rising edge */
    CHECK(s_irq[s_irq_count - 1]); /* IRQ should be asserted */

}

/* C#: MC68901MFPTests.cs:167 */
ETH_TEST(MC68901MFPTests, Test_GPIO_EdgeDetection_FallingEdge)
{
    setup();

    // Configure GPIO 0 for falling edge (AER bit 0 = 0)
    mfp_write(&s_mfp, AER, 0x00);
    mfp_write(&s_mfp, IERA, 0x00);
    mfp_write(&s_mfp, IERB, 0x01);
    mfp_write(&s_mfp, IMRB, 0x01);

    // Set GPIO 0 high first
    mfp_gpio_input(&s_mfp, 0, true);

    s_irq_count = 0;

    // Now trigger falling edge
    mfp_gpio_input(&s_mfp, 0, false);

    // Check interrupt was triggered
    CHECK((s_irq_count) > (0)); /* IRQ should be triggered on falling edge */

}

/* C#: MC68901MFPTests.cs:189 */
ETH_TEST(MC68901MFPTests, Test_GPIO_InterruptGeneration)
{
    setup();

    // Enable GPIO 0 interrupt and unmask it
    mfp_write(&s_mfp, IERB, 0x01);
    mfp_write(&s_mfp, IMRB, 0x01);
    mfp_write(&s_mfp, AER, 0x01);

    s_irq_count = 0;
    mfp_gpio_input(&s_mfp, 0, false);
    mfp_gpio_input(&s_mfp, 0, true);

    CHECK((s_irq_count) > (0)); /* IRQ should be generated */

}

/* C#: MC68901MFPTests.cs:205 */
ETH_TEST(MC68901MFPTests, Test_GPIO_NoInterruptWhenDisabled)
{
    setup();

    // Disable GPIO 0 interrupt
    mfp_write(&s_mfp, IERB, 0x00);
    mfp_write(&s_mfp, AER, 0x01);

    s_irq_count = 0;
    mfp_gpio_input(&s_mfp, 0, false);
    mfp_gpio_input(&s_mfp, 0, true);

    // No IRQ state changes expected when interrupt is disabled
    CHECK_EQ(s_irq_count, 0); /* No IRQ when interrupt disabled */

}

/* C#: MC68901MFPTests.cs:225 */
ETH_TEST(MC68901MFPTests, Test_Interrupt_IER_EnablesInterrupt)
{
    setup();

    // Enable Timer D interrupt (bit 4 of IERB)
    mfp_write(&s_mfp, IERB, 0x10);

    uint8_t ierb = mfp_read(&s_mfp, IERB);
    CHECK_EQ(ierb & 0x10, 0x10); /* Timer D interrupt should be enabled */

}

/* C#: MC68901MFPTests.cs:236 */
ETH_TEST(MC68901MFPTests, Test_Interrupt_IER_DisablesClearsIPR)
{
    setup();

    // First enable and trigger an interrupt
    mfp_write(&s_mfp, IERB, 0x01);
    mfp_write(&s_mfp, IMRB, 0x01);
    mfp_write(&s_mfp, AER, 0x01);
    mfp_gpio_input(&s_mfp, 0, false);
    mfp_gpio_input(&s_mfp, 0, true);

    // Verify IPR is set
    uint8_t iprb = mfp_read(&s_mfp, IPRB);
    CHECK_EQ(iprb & 0x01, 0x01); /* IPR should be set after interrupt */

    // Now disable the interrupt
    mfp_write(&s_mfp, IERB, 0x00);

    // IPR should be cleared when IER is cleared
    iprb = mfp_read(&s_mfp, IPRB);
    CHECK_EQ(iprb & 0x01, 0x00); /* IPR should be cleared when IER is cleared */

}

/* C#: MC68901MFPTests.cs:259 */
ETH_TEST(MC68901MFPTests, Test_Interrupt_IPR_WriteClearsBits)
{
    setup();

    // Enable and trigger interrupt
    mfp_write(&s_mfp, IERB, 0x01);
    mfp_write(&s_mfp, IMRB, 0x01);
    mfp_write(&s_mfp, AER, 0x01);
    mfp_gpio_input(&s_mfp, 0, false);
    mfp_gpio_input(&s_mfp, 0, true);

    // Verify IPR is set
    uint8_t iprb = mfp_read(&s_mfp, IPRB);
    CHECK_EQ(iprb & 0x01, 0x01); /* IPR bit should be set */

    // Write 0 to clear the bit (writing ~0x01 = 0xFE)
    mfp_write(&s_mfp, IPRB, 0xFE);

    // Bit should be cleared
    iprb = mfp_read(&s_mfp, IPRB);
    CHECK_EQ(iprb & 0x01, 0x00); /* IPR bit should be cleared by writing 0 */

}

/* C#: MC68901MFPTests.cs:282 */
ETH_TEST(MC68901MFPTests, Test_Interrupt_IPR_WriteDoesNotSetBits)
{
    setup();

    // IPR starts cleared
    uint8_t iprb = mfp_read(&s_mfp, IPRB);
    CHECK_EQ(iprb, 0x00); /* IPR should start at 0 */

    // Try to set a bit by writing 1
    mfp_write(&s_mfp, IPRB, 0xFF);

    // Bits should NOT be set (IPR can only be cleared by writing 0)
    iprb = mfp_read(&s_mfp, IPRB);
    CHECK_EQ(iprb, 0x00); /* IPR bits cannot be set by writing 1 */

}

/* C#: MC68901MFPTests.cs:298 */
ETH_TEST(MC68901MFPTests, Test_Interrupt_ISR_WriteClearsBits)
{
    setup();

    // Enable software EOI mode
    mfp_write(&s_mfp, VR, 0x48);

    // Enable, unmask, and trigger interrupt
    mfp_write(&s_mfp, IERB, 0x01);
    mfp_write(&s_mfp, IMRB, 0x01);
    mfp_write(&s_mfp, AER, 0x01);
    mfp_gpio_input(&s_mfp, 0, false);
    mfp_gpio_input(&s_mfp, 0, true);

    // Acknowledge interrupt to set ISR
    mfp_handle_interrupt_acknowledge(&s_mfp);

    // Verify ISR is set
    uint8_t isrb = mfp_read(&s_mfp, ISRB);
    CHECK_EQ(isrb & 0x01, 0x01); /* ISR bit should be set after IACK */

    // Write 0 to clear the bit
    mfp_write(&s_mfp, ISRB, 0xFE);

    isrb = mfp_read(&s_mfp, ISRB);
    CHECK_EQ(isrb & 0x01, 0x00); /* ISR bit should be cleared by writing 0 */

}

/* C#: MC68901MFPTests.cs:326 */
ETH_TEST(MC68901MFPTests, Test_Interrupt_ISR_WriteDoesNotSetBits)
{
    setup();

    // ISR starts cleared
    uint8_t isrb = mfp_read(&s_mfp, ISRB);
    CHECK_EQ(isrb, 0x00); /* ISR should start at 0 */

    // Try to set a bit by writing 1
    mfp_write(&s_mfp, ISRB, 0xFF);

    // Bits should NOT be set
    isrb = mfp_read(&s_mfp, ISRB);
    CHECK_EQ(isrb, 0x00); /* ISR bits cannot be set by writing 1 */

}

/* C#: MC68901MFPTests.cs:342 */
ETH_TEST(MC68901MFPTests, Test_Interrupt_IMR_MasksIRQ)
{
    setup();

    // Enable interrupt but mask it
    mfp_write(&s_mfp, IERB, 0x01);
    mfp_write(&s_mfp, IMRB, 0x00);
    mfp_write(&s_mfp, AER, 0x01);

    s_irq_count = 0;
    mfp_gpio_input(&s_mfp, 0, false);
    mfp_gpio_input(&s_mfp, 0, true);

    // IPR should be set but IRQ should not be asserted
    uint8_t iprb = mfp_read(&s_mfp, IPRB);
    CHECK_EQ(iprb & 0x01, 0x01); /* IPR should still be set */

    // IRQ should show as deasserted (last state should be false or no change)
    if (s_irq_count > 0) {
    CHECK(!(s_irq[s_irq_count - 1])); /* IRQ should be deasserted when masked */
    }

}

/* C#: MC68901MFPTests.cs:366 */
ETH_TEST(MC68901MFPTests, Test_Interrupt_IMR_UnmasksIRQ)
{
    setup();

    // Enable and unmask interrupt
    mfp_write(&s_mfp, IERB, 0x01);
    mfp_write(&s_mfp, IMRB, 0x01);
    mfp_write(&s_mfp, AER, 0x01);

    s_irq_count = 0;
    mfp_gpio_input(&s_mfp, 0, false);
    mfp_gpio_input(&s_mfp, 0, true);

    CHECK((s_irq_count) > (0)); /* IRQ state should change */
    CHECK(s_irq[s_irq_count - 1]); /* IRQ should be asserted when unmasked */

}

/* C#: MC68901MFPTests.cs:383 */
ETH_TEST(MC68901MFPTests, Test_Interrupt_VectorGeneration_BaseVector)
{
    setup();

    // Set vector base to 0x40
    mfp_write(&s_mfp, VR, 0x48);

    // Enable and trigger GPIO 0 interrupt
    mfp_write(&s_mfp, IERB, 0x01);
    mfp_write(&s_mfp, IMRB, 0x01);
    mfp_write(&s_mfp, AER, 0x01);
    mfp_gpio_input(&s_mfp, 0, false);
    mfp_gpio_input(&s_mfp, 0, true);

    uint8_t vector = mfp_handle_interrupt_acknowledge(&s_mfp);

    // Vector should have base 0x40 in upper nibble
    CHECK_EQ(vector & 0xF0, 0x40); /* Vector base should be 0x40 */

}

/* C#: MC68901MFPTests.cs:403 */
ETH_TEST(MC68901MFPTests, Test_Interrupt_VectorGeneration_ChannelNumber)
{
    setup();

    // GPIO 0 is channel 0
    mfp_write(&s_mfp, VR, 0x40);
    mfp_write(&s_mfp, IERB, 0x01);
    mfp_write(&s_mfp, IMRB, 0x01);
    mfp_write(&s_mfp, AER, 0x01);
    mfp_gpio_input(&s_mfp, 0, false);
    mfp_gpio_input(&s_mfp, 0, true);

    uint8_t vector = mfp_handle_interrupt_acknowledge(&s_mfp);
    CHECK_EQ(vector & 0x0F, 0x00); /* GPIO 0 should be channel 0 */

}

/* C#: MC68901MFPTests.cs:419 */
ETH_TEST(MC68901MFPTests, Test_Interrupt_Priority_HigherChannelWins)
{
    setup();

    mfp_write(&s_mfp, VR, 0x40);

    // Enable GPIO 0 (channel 0) and GPIO 7 (channel 15)
    mfp_write(&s_mfp, IERA, 0x80);
    mfp_write(&s_mfp, IERB, 0x01);
    mfp_write(&s_mfp, IMRA, 0x80);
    mfp_write(&s_mfp, IMRB, 0x01);
    mfp_write(&s_mfp, AER, 0x81);

    // Trigger both interrupts
    mfp_gpio_input(&s_mfp, 0, false);
    mfp_gpio_input(&s_mfp, 7, false);
    mfp_gpio_input(&s_mfp, 0, true);
    mfp_gpio_input(&s_mfp, 7, true);

    // Higher channel (15) should be acknowledged first
    uint8_t vector = mfp_handle_interrupt_acknowledge(&s_mfp);
    CHECK_EQ(vector & 0x0F, 0x0F); /* GPIO 7 (channel 15) should have priority */

}

/* C#: MC68901MFPTests.cs:443 */
ETH_TEST(MC68901MFPTests, Test_Interrupt_SoftwareEOI_SetsISR)
{
    setup();

    // Enable software EOI mode (S-bit = 1)
    mfp_write(&s_mfp, VR, 0x48);

    mfp_write(&s_mfp, IERB, 0x01);
    mfp_write(&s_mfp, IMRB, 0x01);
    mfp_write(&s_mfp, AER, 0x01);
    mfp_gpio_input(&s_mfp, 0, false);
    mfp_gpio_input(&s_mfp, 0, true);

    // ISR should be set after IACK
    mfp_handle_interrupt_acknowledge(&s_mfp);

    uint8_t isrb = mfp_read(&s_mfp, ISRB);
    CHECK_EQ(isrb & 0x01, 0x01); /* ISR should be set in software EOI mode */

}

/* C#: MC68901MFPTests.cs:463 */
ETH_TEST(MC68901MFPTests, Test_Interrupt_AutoEOI_ClearsISR)
{
    setup();

    // Enable auto EOI mode (S-bit = 0)
    mfp_write(&s_mfp, VR, 0x40);

    // ISR should be cleared immediately
    uint8_t isra = mfp_read(&s_mfp, ISRA);
    uint8_t isrb = mfp_read(&s_mfp, ISRB);
    CHECK_EQ(isra, 0x00); /* ISR A should be 0 in auto EOI mode */
    CHECK_EQ(isrb, 0x00); /* ISR B should be 0 in auto EOI mode */

}

/* C#: MC68901MFPTests.cs:481 */
ETH_TEST(MC68901MFPTests, Test_Timer_Stopped_NoCount)
{
    setup();

    // Set timer A data register
    mfp_write(&s_mfp, TADR, 0x10);

    // Timer A stopped (TCR = 0)
    mfp_write(&s_mfp, TACR, 0x00);

    // TMC should equal TDR when stopped
    uint8_t tmc = mfp_read(&s_mfp, TADR);
    CHECK_EQ(tmc, 0x10); /* TMC should equal TDR when stopped */

}

/* C#: MC68901MFPTests.cs:496 */
ETH_TEST(MC68901MFPTests, Test_Timer_DelayMode_Enables)
{
    setup();

    mfp_write(&s_mfp, TADR, 0x10);

    // Set delay mode (prescaler /4)
    mfp_write(&s_mfp, TACR, 0x01);

    // Timer should be enabled - verified by checking it counts
    // This test verifies the fix for timer not enabling in delay mode
    uint8_t tmc1 = mfp_read(&s_mfp, TADR);
    CHECK_EQ(tmc1, 0x10); /* Initial TMC value */

}

/* C#: MC68901MFPTests.cs:511 */
ETH_TEST(MC68901MFPTests, Test_Timer_DelayMode_Countdown)
{
    setup();

    mfp_write(&s_mfp, TADR, 0x05);
    mfp_write(&s_mfp, TACR, 0x01);

    // Simulate some ticks - the exact behavior depends on implementation
    // For now just verify TMC is readable
    uint8_t tmc = mfp_read(&s_mfp, TADR);
    CHECK((tmc) <= (0x05)); /* TMC should count down or equal TDR */

}

/* C#: MC68901MFPTests.cs:524 */
ETH_TEST(MC68901MFPTests, Test_Timer_Reset_ClearsOutput_TimerA)
{
    setup();

    s_tout_count = 0;

    // Set reset bit for Timer A
    mfp_write(&s_mfp, TACR, 0x10);

    // Check output was cleared
    bool foundClearOutput = false;
    for (int i = 0; i < s_tout_count; i++) {
    if (s_tout[i].name == MFP_TIMER_NAME_A && s_tout[i].state == false) {
    foundClearOutput = true;
    break;
    }
    }
    CHECK(foundClearOutput); /* Timer A output should be cleared on reset */

}

/* C#: MC68901MFPTests.cs:546 */
ETH_TEST(MC68901MFPTests, Test_Timer_Reset_ClearsOutput_TimerB)
{
    setup();

    s_tout_count = 0;

    // Set reset bit for Timer B
    mfp_write(&s_mfp, TBCR, 0x10);

    bool foundClearOutput = false;
    for (int i = 0; i < s_tout_count; i++) {
    if (s_tout[i].name == MFP_TIMER_NAME_B && s_tout[i].state == false) {
    foundClearOutput = true;
    break;
    }
    }
    CHECK(foundClearOutput); /* Timer B output should be cleared on reset */

}

/* C#: MC68901MFPTests.cs:567 */
ETH_TEST(MC68901MFPTests, Test_Timer_Reset_IgnoredForTimerC)
{
    setup();

    s_tout_count = 0;

    // Try to set reset bit for Timer C (should be ignored - C only has 3-bit control)
    mfp_write(&s_mfp, TCDCR, 0x17);

    // Timer C should NOT get a reset output event
    bool foundCReset = false;
    for (int i = 0; i < s_tout_count; i++) {
    if (s_tout[i].name == MFP_TIMER_NAME_C) {
    foundCReset = true;
    }
    }
    // Timer C doesn't support reset, so no output event should occur for reset
    // (It may have output events for other reasons though)
    CHECK(true); /* C# Assert.Pass: Timer C reset bit is correctly ignored (3-bit control only) */

}

/* C#: MC68901MFPTests.cs:590 */
ETH_TEST(MC68901MFPTests, Test_Timer_Reset_IgnoredForTimerD)
{
    setup();

    s_tout_count = 0;

    // Try to set reset bit for Timer D (should be ignored - D only has 3-bit control)
    // D is in low nibble, so 0x10 would be invalid but let's try
    mfp_write(&s_mfp, TCDCR, 0x70);

    // Similar to Timer C, reset should be ignored for Timer D
    CHECK(true); /* C# Assert.Pass: Timer D reset bit is correctly ignored (3-bit control only) */

}

/* C#: MC68901MFPTests.cs:604 */
ETH_TEST(MC68901MFPTests, Test_Timer_TDR_LoadsTMC_WhenStopped)
{
    setup();

    // Stop timer
    mfp_write(&s_mfp, TACR, 0x00);

    // Write to TDR
    mfp_write(&s_mfp, TADR, 0x42);

    // TMC should be loaded immediately when stopped
    uint8_t tmc = mfp_read(&s_mfp, TADR);
    CHECK_EQ(tmc, 0x42); /* TMC should equal TDR when timer is stopped */

}

/* C#: MC68901MFPTests.cs:619 */
ETH_TEST(MC68901MFPTests, Test_Timer_TDR_NotLoadsTMC_WhenRunning)
{
    setup();

    // Set initial value and start timer
    mfp_write(&s_mfp, TADR, 0x20);
    mfp_write(&s_mfp, TACR, 0x01);

    // Write new value to TDR
    mfp_write(&s_mfp, TADR, 0xFF);

    // TMC should NOT immediately change (will only load on next timeout)
    // This is a bit tricky to test without ticking the timer
    // For now, verify TDR was written
    CHECK(true); /* C# Assert.Pass: TDR write while running - TMC loads on timeout */

}

/* C#: MC68901MFPTests.cs:640 */
ETH_TEST(MC68901MFPTests, Test_TCDCR_Read_TimerDLowNibble)
{
    setup();

    // Set Timer D to mode 3 (delay /16) via TCDCR low nibble
    mfp_write(&s_mfp, TCDCR, 0x03);

    uint8_t tcdcr = mfp_read(&s_mfp, TCDCR);
    CHECK_EQ(tcdcr & 0x07, 0x03); /* Timer D control should be in bits 0-2 */

}

/* C#: MC68901MFPTests.cs:651 */
ETH_TEST(MC68901MFPTests, Test_TCDCR_Read_TimerCHighNibble)
{
    setup();

    // Set Timer C to mode 5 (delay /64) via TCDCR high nibble
    mfp_write(&s_mfp, TCDCR, 0x50);

    uint8_t tcdcr = mfp_read(&s_mfp, TCDCR);
    CHECK_EQ((tcdcr >> 4) & 0x07, 0x05); /* Timer C control should be in bits 4-6 */

}

/* C#: MC68901MFPTests.cs:662 */
ETH_TEST(MC68901MFPTests, Test_TCDCR_Write_TimerDLowNibble)
{
    setup();

    // Write TCDCR with Timer D = 2, Timer C = 0
    mfp_write(&s_mfp, TCDCR, 0x02);

    // Verify by reading back
    uint8_t tcdcr = mfp_read(&s_mfp, TCDCR);
    CHECK_EQ(tcdcr & 0x07, 0x02); /* Timer D should be set to mode 2 */
    CHECK_EQ((tcdcr >> 4) & 0x07, 0x00); /* Timer C should be stopped */

}

/* C#: MC68901MFPTests.cs:675 */
ETH_TEST(MC68901MFPTests, Test_TCDCR_Write_TimerCHighNibble)
{
    setup();

    // Write TCDCR with Timer C = 7, Timer D = 0
    mfp_write(&s_mfp, TCDCR, 0x70);

    uint8_t tcdcr = mfp_read(&s_mfp, TCDCR);
    CHECK_EQ((tcdcr >> 4) & 0x07, 0x07); /* Timer C should be set to mode 7 */
    CHECK_EQ(tcdcr & 0x07, 0x00); /* Timer D should be stopped */

}

/* C#: MC68901MFPTests.cs:691 */
ETH_TEST(MC68901MFPTests, Test_Register_OddAddressOnly)
{
    setup();

    // Write to even address should be ignored
    mfp_write(&s_mfp, BASE_ADDR + 0x00, 0xFF);

    // Read from even address should return 0
    uint8_t value = mfp_read(&s_mfp, BASE_ADDR + 0x00);
    CHECK_EQ(value, 0x00); /* Even address read should return 0 */

}

/* C#: MC68901MFPTests.cs:703 */
ETH_TEST(MC68901MFPTests, Test_Register_RSR_ReadClearsOverrun)
{
    setup();

    // This test verifies the fix for RSR read side effect
    // Set overrun error manually (via internal access if possible)
    // For now, just verify the read doesn't crash and returns valid data
    uint8_t rsr = mfp_read(&s_mfp, RSR);
    CHECK((rsr) >= (0)); /* RSR should be readable */

}

/* C#: MC68901MFPTests.cs:714 */
ETH_TEST(MC68901MFPTests, Test_Register_TSR_ReadClearsUnderrun)
{
    setup();

    // Verify TSR is readable
    uint8_t tsr = mfp_read(&s_mfp, TSR);
    // Buffer empty bit should be set after reset
    CHECK_EQ(tsr & 0x80, 0x80); /* TSR buffer empty should be set after reset */

}

/* C#: MC68901MFPTests.cs:724 */
ETH_TEST(MC68901MFPTests, Test_Register_VR_StoresFullByte)
{
    setup();

    // Write value with lower bits set
    mfp_write(&s_mfp, VR, 0xFF);

    // VR stores full byte — bits 0-2 are unused but read back as written (per Hatari)
    uint8_t vr = mfp_read(&s_mfp, VR);
    CHECK_EQ(vr, 0xFF); /* VR should store full byte including bits 0-2 */

}

/* C#: MC68901MFPTests.cs:736 */
ETH_TEST(MC68901MFPTests, Test_Reset_ClearsAllRegisters)
{
    setup();

    // Set various registers
    mfp_write(&s_mfp, GPDR, 0xFF);
    mfp_write(&s_mfp, DDR, 0xFF);
    mfp_write(&s_mfp, AER, 0xFF);
    mfp_write(&s_mfp, VR, 0xF8);

    // Reset
    mfp_reset(&s_mfp);

    // Verify registers are cleared
    CHECK_EQ(mfp_read(&s_mfp, DDR), 0x00); /* DDR should be 0 after reset */
    CHECK_EQ(mfp_read(&s_mfp, AER), 0x00); /* AER should be 0 after reset */
    CHECK_EQ(mfp_read(&s_mfp, VR), 0x00); /* VR should be 0 after reset */

}

/* C#: MC68901MFPTests.cs:760 */
ETH_TEST(MC68901MFPTests, Test_Timer_DelayMode_InterruptGeneration)
{
    setup();

    // Enable Timer A interrupt
    mfp_write(&s_mfp, IERA, 0x20);
    mfp_write(&s_mfp, IMRA, 0x20);

    // Set timer for quick timeout
    mfp_write(&s_mfp, TADR, 0x02);
    mfp_write(&s_mfp, TACR, 0x01);

    // Note: actual interrupt generation depends on timer ticking
    // This test verifies the setup is correct
    uint8_t iera = mfp_read(&s_mfp, IERA);
    CHECK_EQ(iera & 0x20, 0x20); /* Timer A interrupt should be enabled */

}

/* C#: MC68901MFPTests.cs:778 */
ETH_TEST(MC68901MFPTests, Test_Timer_EventMode_CountsOnEdge)
{
    setup();

    // Set Timer A to event count mode
    mfp_write(&s_mfp, TADR, 0x05);
    mfp_write(&s_mfp, TACR, 0x08);

    // Configure for rising edge on TAI (GPIO 4)
    mfp_write(&s_mfp, AER, 0x10);

    // Trigger edges via TAI input
    mfp_timer_input_a(&s_mfp, false);
    mfp_timer_input_a(&s_mfp, true);

    // Timer should have counted
    // Exact behavior depends on implementation
    CHECK(true); /* C# Assert.Pass: Event count mode configured correctly */

}

/* C#: MC68901MFPTests.cs:802 */
ETH_TEST(MC68901MFPTests, Test_USART_TSR_BufferEmptyOnReset)
{
    setup();

    // After reset, transmit buffer should be empty
    uint8_t tsr = mfp_read(&s_mfp, TSR);
    CHECK_EQ(tsr & 0x80, 0x80); /* TSR_BUFFER_EMPTY should be set on reset */

}

/* C#: MC68901MFPTests.cs:811 */
ETH_TEST(MC68901MFPTests, Test_USART_UDR_Write_ClearsBufferEmpty)
{
    setup();

    // Initially buffer is empty
    uint8_t tsr1 = mfp_read(&s_mfp, TSR);
    CHECK_EQ(tsr1 & 0x80, 0x80); /* TSR_BUFFER_EMPTY should be set initially */

    // Write data to UDR
    mfp_write(&s_mfp, UDR, 0x55);

    // Buffer should no longer be empty
    uint8_t tsr2 = mfp_read(&s_mfp, TSR);
    CHECK_EQ(tsr2 & 0x80, 0x00); /* TSR_BUFFER_EMPTY should be cleared after UDR write */

}

/* C#: MC68901MFPTests.cs:827 */
ETH_TEST(MC68901MFPTests, Test_USART_RSR_ReceiverDisabledOnReset)
{
    setup();

    // After reset, receiver should be disabled
    uint8_t rsr = mfp_read(&s_mfp, RSR);
    CHECK_EQ(rsr & 0x01, 0x00); /* RSR_RCV_ENABLE should be clear on reset */

}

/* C#: MC68901MFPTests.cs:836 */
ETH_TEST(MC68901MFPTests, Test_USART_RSR_EnableReceiver)
{
    setup();

    // Enable receiver
    mfp_write(&s_mfp, RSR, 0x01);

    uint8_t rsr = mfp_read(&s_mfp, RSR);
    CHECK_EQ(rsr & 0x01, 0x01); /* RSR_RCV_ENABLE should be set */

}

/* C#: MC68901MFPTests.cs:847 */
ETH_TEST(MC68901MFPTests, Test_USART_RSR_DisableReceiverClearsFlags)
{
    setup();

    // Enable receiver first
    mfp_write(&s_mfp, RSR, 0x01);

    // Disable receiver - should clear all RSR flags
    mfp_write(&s_mfp, RSR, 0x00);

    uint8_t rsr = mfp_read(&s_mfp, RSR);
    CHECK_EQ(rsr, 0x00); /* Disabling receiver should clear RSR */

}

/* C#: MC68901MFPTests.cs:861 */
ETH_TEST(MC68901MFPTests, Test_USART_TSR_EnableTransmitter)
{
    setup();

    // Enable transmitter
    mfp_write(&s_mfp, TSR, 0x01);

    uint8_t tsr = mfp_read(&s_mfp, TSR);
    CHECK_EQ(tsr & 0x01, 0x01); /* TSR_XMIT_ENABLE should be set */

}

/* C#: MC68901MFPTests.cs:872 */
ETH_TEST(MC68901MFPTests, Test_USART_TSR_PreservesBufferEmptyOnWrite)
{
    setup();

    // Buffer empty is read-only, should be preserved when writing TSR
    uint8_t tsr1 = mfp_read(&s_mfp, TSR);
    CHECK_EQ(tsr1 & 0x80, 0x80); /* Buffer should be empty initially */

    // Write to TSR to enable transmitter (trying to clear buffer empty has no effect)
    mfp_write(&s_mfp, TSR, 0x01);

    uint8_t tsr2 = mfp_read(&s_mfp, TSR);
    CHECK_EQ(tsr2 & 0x80, 0x80); /* TSR_BUFFER_EMPTY should be preserved */
    CHECK_EQ(tsr2 & 0x01, 0x01); /* TSR_XMIT_ENABLE should be set */

}

/* C#: MC68901MFPTests.cs:888 */
ETH_TEST(MC68901MFPTests, Test_USART_UCR_WordLength)
{
    setup();

    // Set 7-bit word length
    mfp_write(&s_mfp, UCR, 0x20);

    uint8_t ucr = mfp_read(&s_mfp, UCR);
    CHECK_EQ(ucr & 0x60, 0x20); /* UCR word length should be 7 bits */

}

/* C#: MC68901MFPTests.cs:899 */
ETH_TEST(MC68901MFPTests, Test_USART_UCR_ParityEnable)
{
    setup();

    // Enable even parity
    mfp_write(&s_mfp, UCR, 0x06);

    uint8_t ucr = mfp_read(&s_mfp, UCR);
    CHECK_EQ(ucr & 0x04, 0x04); /* UCR parity should be enabled */
    CHECK_EQ(ucr & 0x02, 0x02); /* UCR parity should be even */

}

/* C#: MC68901MFPTests.cs:911 */
ETH_TEST(MC68901MFPTests, Test_USART_UCR_AsyncMode)
{
    setup();

    // Set async mode with 1 start, 1 stop bit
    mfp_write(&s_mfp, UCR, 0x08);

    uint8_t ucr = mfp_read(&s_mfp, UCR);
    CHECK_EQ(ucr & 0x18, 0x08); /* UCR should be async 1 start, 1 stop */

}

/* C#: MC68901MFPTests.cs:922 */
ETH_TEST(MC68901MFPTests, Test_USART_UCR_ClockDivide16)
{
    setup();

    // Set clock divide by 16
    mfp_write(&s_mfp, UCR, 0x80);

    uint8_t ucr = mfp_read(&s_mfp, UCR);
    CHECK_EQ(ucr & 0x80, 0x80); /* UCR clock divide should be /16 */

}

/* C#: MC68901MFPTests.cs:933 */
ETH_TEST(MC68901MFPTests, Test_USART_SCR_Write)
{
    setup();

    // Write sync character
    mfp_write(&s_mfp, SCR, 0x16);

    uint8_t scr = mfp_read(&s_mfp, SCR);
    CHECK_EQ(scr, 0x16); /* SCR should store sync character */

}

/* C#: MC68901MFPTests.cs:945 */
ETH_TEST(MC68901MFPTests, Test_USART_TX_InterruptEnable)
{
    setup();

    // Enable transmit buffer empty interrupt (bit 10 = IERB bit 2)
    mfp_write(&s_mfp, IERA, 0x04);
    mfp_write(&s_mfp, IMRA, 0x04);

    uint8_t iera = mfp_read(&s_mfp, IERA);
    uint8_t imra = mfp_read(&s_mfp, IMRA);
    CHECK_EQ(iera & 0x04, 0x04); /* TX interrupt should be enabled */
    CHECK_EQ(imra & 0x04, 0x04); /* TX interrupt should be unmasked */

}

/* C#: MC68901MFPTests.cs:960 */
ETH_TEST(MC68901MFPTests, Test_USART_RX_InterruptEnable)
{
    setup();

    // Enable receive buffer full interrupt (bit 12 = IERA bit 4)
    mfp_write(&s_mfp, IERA, 0x10);
    mfp_write(&s_mfp, IMRA, 0x10);

    uint8_t iera = mfp_read(&s_mfp, IERA);
    uint8_t imra = mfp_read(&s_mfp, IMRA);
    CHECK_EQ(iera & 0x10, 0x10); /* RX interrupt should be enabled */
    CHECK_EQ(imra & 0x10, 0x10); /* RX interrupt should be unmasked */

}

/* C#: MC68901MFPTests.cs:974 */
ETH_TEST(MC68901MFPTests, Test_USART_SerialOutput_IdleHigh)
{
    setup();

    // After reset, serial output should be high (mark/idle state)
    CHECK((s_so_count) > (0)); /* Should have serial output event from reset */
    CHECK(s_so[s_so_count - 1]); /* Serial output should be high (idle) after reset */

}

/* C#: MC68901MFPTests.cs:1010 */
ETH_TEST(MC68901MFPTests, Test_USART_RX_ReceiveCharacter)
{
    setup();

    // Configure USART: 8-bit, no parity, 1 stop, /1 clock mode
    // UCR: 0x08 = UCR_START_STOP_1_1 (bit 3), /1 clock (bit 7=0), 8-bit (bits 5-6=0)
    mfp_write(&s_mfp, UCR, 0x08);
    mfp_write(&s_mfp, RSR, RSR_RCV_ENABLE);

    // Start with line idle (high)
    mfp_set_serial_input(&s_mfp, true);
    ClockInBit(true);

    // Send character 0x55 (01010101): Start(0), D0-D7, Stop(1)
    ClockInBit(false);
    ClockInBit(true);
    ClockInBit(false);
    ClockInBit(true);
    ClockInBit(false);
    ClockInBit(true);
    ClockInBit(false);
    ClockInBit(true);
    ClockInBit(false);
    ClockInBit(true);

    // Check buffer full flag
    uint8_t rsr = mfp_read(&s_mfp, RSR);
    CHECK((rsr & RSR_BUFFER_FULL) != 0); /* RSR_BUFFER_FULL should be set */

    // Read received data
    uint8_t data = mfp_read(&s_mfp, UDR);
    CHECK_EQ(data, 0x55); /* Should receive 0x55 */

}

/* C#: MC68901MFPTests.cs:1044 */
ETH_TEST(MC68901MFPTests, Test_USART_FrameError_Detection)
{
    setup();

    // Configure USART: 8-bit, no parity, 1 stop, /1 clock mode
    mfp_write(&s_mfp, UCR, 0x08);
    mfp_write(&s_mfp, RSR, RSR_RCV_ENABLE);

    // Start with line idle (high)
    mfp_set_serial_input(&s_mfp, true);
    ClockInBit(true);

    // Send character with MISSING stop bit (frame error)
    ClockInBit(false);
    ClockInBit(true);
    ClockInBit(true);
    ClockInBit(true);
    ClockInBit(true);
    ClockInBit(true);
    ClockInBit(true);
    ClockInBit(true);
    ClockInBit(true);
    ClockInBit(false);

    // Check frame error flag
    uint8_t rsr = mfp_read(&s_mfp, RSR);
    CHECK((rsr & RSR_FRAME_ERROR) != 0); /* RSR_FRAME_ERROR should be set */

}

/* C#: MC68901MFPTests.cs:1073 */
ETH_TEST(MC68901MFPTests, Test_USART_ParityError_Detection)
{
    setup();

    // Configure USART: 8-bit, EVEN parity, 1 stop, /1 clock mode
    // UCR: 0x0E = UCR_START_STOP_1_1 (0x08) | UCR_PARITY_ENABLED (0x04) | UCR_PARITY_EVEN (0x02)
    mfp_write(&s_mfp, UCR, 0x0E);
    mfp_write(&s_mfp, RSR, RSR_RCV_ENABLE);

    // Start with line idle (high)
    mfp_set_serial_input(&s_mfp, true);
    ClockInBit(true);

    // Send 0x55 (4 ones) with WRONG parity (should be 0 for even, we send 1)
    ClockInBit(false);
    ClockInBit(true);
    ClockInBit(false);
    ClockInBit(true);
    ClockInBit(false);
    ClockInBit(true);
    ClockInBit(false);
    ClockInBit(true);
    ClockInBit(false);
    ClockInBit(true);
    ClockInBit(true);

    // Check parity error flag
    uint8_t rsr = mfp_read(&s_mfp, RSR);
    CHECK((rsr & RSR_PARITY_ERROR) != 0); /* RSR_PARITY_ERROR should be set */

}

/* C#: MC68901MFPTests.cs:1104 */
ETH_TEST(MC68901MFPTests, Test_USART_OverrunError_Detection)
{
    setup();

    // Configure USART: 8-bit, no parity, 1 stop, /1 clock mode
    mfp_write(&s_mfp, UCR, 0x08);
    mfp_write(&s_mfp, RSR, RSR_RCV_ENABLE);

    // Start with line idle (high)
    mfp_set_serial_input(&s_mfp, true);
    ClockInBit(true);

    // Send first character (0xAA)
    ClockInBit(false);
    ClockInBit(false);
    ClockInBit(true);
    ClockInBit(false);
    ClockInBit(true);
    ClockInBit(false);
    ClockInBit(true);
    ClockInBit(false);
    ClockInBit(true);
    ClockInBit(true);

    // Verify first char received (don't read UDR - leave buffer full)
    uint8_t rsr1 = mfp_read(&s_mfp, RSR);
    CHECK((rsr1 & RSR_BUFFER_FULL) != 0); /* First char should be in buffer */

    // Send second character WITHOUT reading first (causes overrun)
    ClockInBit(false);
    ClockInBit(true);
    ClockInBit(true);
    ClockInBit(true);
    ClockInBit(true);
    ClockInBit(true);
    ClockInBit(true);
    ClockInBit(true);
    ClockInBit(true);
    ClockInBit(true);

    // Overrun is stored in next_rsr, transferred when UDR is read
    // Read UDR to trigger the transfer of overrun error to rsr
    mfp_read(&s_mfp, UDR);

    // Check overrun error flag
    uint8_t rsr2 = mfp_read(&s_mfp, RSR);
    CHECK((rsr2 & RSR_OVERRUN_ERROR) != 0); /* RSR_OVERRUN_ERROR should be set */

}

/* C#: MC68901MFPTests.cs:1153 */
ETH_TEST(MC68901MFPTests, Test_USART_Break_Detection)
{
    setup();

    // Configure USART: 8-bit, no parity, 1 stop, /1 clock mode
    mfp_write(&s_mfp, UCR, 0x08);
    mfp_write(&s_mfp, RSR, RSR_RCV_ENABLE);

    // Start with line idle (high)
    mfp_set_serial_input(&s_mfp, true);
    ClockInBit(true);

    // Send break condition: all zeros including where stop bit should be
    ClockInBit(false);
    ClockInBit(false);
    ClockInBit(false);
    ClockInBit(false);
    ClockInBit(false);
    ClockInBit(false);
    ClockInBit(false);
    ClockInBit(false);
    ClockInBit(false);
    ClockInBit(false);

    // Check break flag
    uint8_t rsr = mfp_read(&s_mfp, RSR);
    CHECK((rsr & RSR_BREAK) != 0); /* RSR_BREAK should be set */

}

/* C#: MC68901MFPTests.cs:1182 */
ETH_TEST(MC68901MFPTests, Test_USART_RX_Interrupt_OnError)
{
    setup();

    // Enable RX error interrupt
    mfp_write(&s_mfp, IERA, 0x08);
    mfp_write(&s_mfp, IMRA, 0x08);
    s_irq_count = 0;

    // Configure USART: 8-bit, no parity, 1 stop, /1 clock mode
    mfp_write(&s_mfp, UCR, 0x08);
    mfp_write(&s_mfp, RSR, RSR_RCV_ENABLE);

    // Start with line idle (high)
    mfp_set_serial_input(&s_mfp, true);
    ClockInBit(true);

    // Send character with frame error
    ClockInBit(false);
    ClockInBit(true);
    ClockInBit(true);
    ClockInBit(true);
    ClockInBit(true);
    ClockInBit(true);
    ClockInBit(true);
    ClockInBit(true);
    ClockInBit(true);
    ClockInBit(false);

    // Check that IRQ was asserted
    bool hasIrq = false;
    for (int i = 0; i < s_irq_count; i++) {
    if (s_irq[i]) hasIrq = true;
    }
    CHECK(hasIrq); /* IRQ should be asserted on RX error */

}

/* C#: MC68901MFPTests.cs:1224 */
ETH_TEST(MC68901MFPTests, Test_Timer_Prescaler_Div4)
{
    setup();

    // Set Timer A to delay mode /4
    mfp_write(&s_mfp, TADR, 0x10);
    mfp_write(&s_mfp, TACR, 0x01);

    // Timer should need 4 clocks per count
    uint8_t tmc1 = mfp_read(&s_mfp, TADR);

    // Clock 3 times - should not count yet
    mfp_clock(&s_mfp);
    mfp_clock(&s_mfp);
    mfp_clock(&s_mfp);
    uint8_t tmc2 = mfp_read(&s_mfp, TADR);
    CHECK_EQ(tmc2, tmc1); /* Timer should not count after 3 clocks with /4 prescaler */

    // 4th clock - should count
    mfp_clock(&s_mfp);
    uint8_t tmc3 = mfp_read(&s_mfp, TADR);
    CHECK_EQ(tmc3, (uint8_t)(tmc1 - 1)); /* Timer should count after 4 clocks */

}

/* C#: MC68901MFPTests.cs:1248 */
ETH_TEST(MC68901MFPTests, Test_Timer_TDR_ZeroValue)
{
    setup();

    // Set TDR to 0
    mfp_write(&s_mfp, TADR, 0x00);
    mfp_write(&s_mfp, TACR, 0x00);

    uint8_t tmc = mfp_read(&s_mfp, TADR);
    CHECK_EQ(tmc, 0x00); /* Timer should accept TDR value of 0 */

}

/* C#: MC68901MFPTests.cs:1260 */
ETH_TEST(MC68901MFPTests, Test_Timer_TDR_OneValue)
{
    setup();

    // Set TDR to 1 - should trigger on first count
    mfp_write(&s_mfp, IERA, 0x20);
    mfp_write(&s_mfp, IMRA, 0x20);
    mfp_write(&s_mfp, TADR, 0x01);
    mfp_write(&s_mfp, TACR, 0x01);

    // Clock 4 times for one count
    mfp_clock(&s_mfp);
    mfp_clock(&s_mfp);
    mfp_clock(&s_mfp);
    mfp_clock(&s_mfp);

    // Timer should have triggered and reloaded
    uint8_t tmc = mfp_read(&s_mfp, TADR);
    CHECK_EQ(tmc, 0x01); /* Timer should reload from TDR after expiration */

}

/* C#: MC68901MFPTests.cs:1281 */
ETH_TEST(MC68901MFPTests, Test_Timer_PulseMode_DisabledInitially)
{
    setup();

    // Set Timer A to pulse width mode
    mfp_write(&s_mfp, TADR, 0x10);
    mfp_write(&s_mfp, TACR, 0x09);

    // Clock several times - timer should not count (waiting for input)
    mfp_clock(&s_mfp);
    mfp_clock(&s_mfp);
    mfp_clock(&s_mfp);
    mfp_clock(&s_mfp);

    uint8_t tmc = mfp_read(&s_mfp, TADR);
    CHECK_EQ(tmc, 0x10); /* Timer should not count in pulse mode until input active */

}

/* C#: MC68901MFPTests.cs:1299 */
ETH_TEST(MC68901MFPTests, Test_Timer_OutputToggle)
{
    setup();

    // Enable Timer A with short count to verify output toggles
    mfp_write(&s_mfp, TADR, 0x01);
    mfp_write(&s_mfp, TACR, 0x01);

    s_tout_count = 0;

    // Clock 4 times for one count (should toggle output)
    mfp_clock(&s_mfp);
    mfp_clock(&s_mfp);
    mfp_clock(&s_mfp);
    mfp_clock(&s_mfp);

    CHECK((s_tout_count) > (0)); /* Timer should produce output event */

}

/* C#: MC68901MFPTests.cs:1322 */
ETH_TEST(MC68901MFPTests, Test_DaisyChain_IEI_DefaultTrue)
{
    setup();

    // IEI should default to true (device can interrupt)
    CHECK(s_mfp.iei); /* IEI should default to true */

}

/* C#: MC68901MFPTests.cs:1330 */
ETH_TEST(MC68901MFPTests, Test_DaisyChain_IEI_DisablesInterrupts)
{
    setup();

    // When IEI is false, IRQ should not be asserted
    mfp_write(&s_mfp, IERA, 0x20);
    mfp_write(&s_mfp, IMRA, 0x20);
    s_irq_count = 0;

    // Disable IEI
    mfp_set_iei(&s_mfp, false);

    // Trigger Timer A interrupt
    mfp_write(&s_mfp, TADR, 0x01);
    mfp_write(&s_mfp, TACR, 0x01);
    for (int i = 0; i < 4; i++) mfp_clock(&s_mfp);

    // IRQ should NOT be asserted because IEI is false
    bool hasActiveIrq = false;
    for (int i = 0; i < s_irq_count; i++) {
    if (s_irq[i]) hasActiveIrq = true;
    }
    CHECK(!(hasActiveIrq)); /* IRQ should not be asserted when IEI is false */

}

/* C#: MC68901MFPTests.cs:1356 */
ETH_TEST(MC68901MFPTests, Test_DaisyChain_GetVector_CallsChainWhenNoInterrupt)
{
    uint8_t chain_vector = 0x42u;
    uint8_t vector;
    setup();
    s_chain_vector = chain_vector;
    s_mfp.ev.daisy_chain_callback = chain_cb;
    vector = mfp_get_vector(&s_mfp);
    CHECK_EQ(vector, chain_vector);
}

/* C#: MC68901MFPTests.cs:1369 */
ETH_TEST(MC68901MFPTests, Test_DaisyChain_GetVector_ReturnsSpuriousWhenNoCallback)
{
    uint8_t vector;
    setup();
    s_mfp.ev.daisy_chain_callback = NULL;
    vector = mfp_get_vector(&s_mfp);
    CHECK_EQ(vector, 0x18);
}

/* C#: MC68901MFPTests.cs:1381 */
ETH_TEST(MC68901MFPTests, Test_DaisyChain_GetVector_IgnoresChainWhenLocalInterrupt)
{
    uint8_t chain_vector = 0x42u;
    uint8_t vector;
    setup();
    s_chain_vector = chain_vector;
    s_mfp.ev.daisy_chain_callback = chain_cb;
    mfp_write(&s_mfp, VR, 0x40);
    mfp_write(&s_mfp, IERA, 0x20);
    mfp_write(&s_mfp, IMRA, 0x20);
    mfp_write(&s_mfp, TADR, 0x01);
    mfp_write(&s_mfp, TACR, 0x01);
    for (int i = 0; i < 4; i++)
    {
        mfp_clock(&s_mfp);
    }
    vector = mfp_get_vector(&s_mfp);
    CHECK_EQ(vector, 0x4D);
}

/* C#: MC68901MFPTests.cs:1403 */
ETH_TEST(MC68901MFPTests, Test_DaisyChain_IEI_False_CallsChainDirectly)
{
    uint8_t chain_vector = 0x42u;
    uint8_t vector;
    setup();
    s_chain_vector = chain_vector;
    s_mfp.ev.daisy_chain_callback = chain_cb;
    mfp_write(&s_mfp, IERA, 0x20);
    mfp_write(&s_mfp, IMRA, 0x20);
    mfp_write(&s_mfp, TADR, 0x01);
    mfp_write(&s_mfp, TACR, 0x01);
    for (int i = 0; i < 4; i++)
    {
        mfp_clock(&s_mfp);
    }
    mfp_set_iei(&s_mfp, false);
    vector = mfp_get_vector(&s_mfp);
    CHECK_EQ(vector, chain_vector);
}

/* C#: MC68901MFPTests.cs:1426 */
ETH_TEST(MC68901MFPTests, Test_DaisyChain_IEO_ActiveWhenNoISR)
{
    setup();
    s_ieo_state = false;
    s_mfp.ev.on_ieo = rec_ieo;
    mfp_write(&s_mfp, ISRA, 0xFF);
    CHECK(s_ieo_state);
}

/* C#: MC68901MFPTests.cs:1441 */
ETH_TEST(MC68901MFPTests, Test_DaisyChain_IEO_InactiveWhenISRSet)
{
    setup();
    s_ieo_state = true;
    s_mfp.ev.on_ieo = rec_ieo;
    mfp_write(&s_mfp, VR, 0x48);
    mfp_write(&s_mfp, IERA, 0x20);
    mfp_write(&s_mfp, IMRA, 0x20);
    mfp_write(&s_mfp, TADR, 0x01);
    mfp_write(&s_mfp, TACR, 0x01);
    for (int i = 0; i < 4; i++)
    {
        mfp_clock(&s_mfp);
    }
    (void)mfp_get_vector(&s_mfp);
    CHECK(!(s_ieo_state));
}

/* C#: MC68901MFPTests.cs:1463 */
ETH_TEST(MC68901MFPTests, Test_DaisyChain_IEO_InactiveWhenIEIFalse)
{
    setup();
    s_ieo_state = true;
    s_mfp.ev.on_ieo = rec_ieo;
    mfp_set_iei(&s_mfp, false);
    mfp_write(&s_mfp, ISRA, 0xFF);
    CHECK(!(s_ieo_state));
}

/* C#: MC68901MFPTests.cs:1486 */
ETH_TEST(MC68901MFPTests, Test_OnRegisterWrite_Fires_For_All_Writes)
{
    setup();
    s_rw_count = 0;
    s_mfp.ev.on_register_write = rec_rw;
    mfp_write(&s_mfp, GPDR, 0x42);
    mfp_write(&s_mfp, VR, 0x40);
    mfp_write(&s_mfp, IERA, 0xFF);
    mfp_write(&s_mfp, ISRB, 0x58);
    CHECK_EQ(s_rw_count, 4);
    CHECK_EQ(s_rw[0].reg, MFP_REGISTER_GPDR);
    CHECK_EQ(s_rw[0].value, 0x42);
    CHECK_EQ(s_rw[1].reg, MFP_REGISTER_VR);
    CHECK_EQ(s_rw[1].value, 0x40);
    CHECK_EQ(s_rw[2].reg, MFP_REGISTER_IERA);
    CHECK_EQ(s_rw[2].value, 0xFF);
    CHECK_EQ(s_rw[3].reg, MFP_REGISTER_ISRB);
    CHECK_EQ(s_rw[3].value, 0x58);
}

/* C#: MC68901MFPTests.cs:1513 */
ETH_TEST(MC68901MFPTests, Test_EthernetII_Register_Mapping)
{
    MC68901MFP eth_mfp;
    MfpEvents ev;
    setup();
    memset(&ev, 0, sizeof ev);
    ev.on_register_write = rec_rw;
    mfp_create(&eth_mfp, 0xEF00C0u, 0x40u, &ev);
    s_rw_count = 0;
    mfp_write(&eth_mfp, 0xEF00C1u, 0x01);
    mfp_write(&eth_mfp, 0xEF00C7u, 0x02);
    mfp_write(&eth_mfp, 0xEF00D1u, 0x03);
    mfp_write(&eth_mfp, 0xEF00D7u, 0x04);
    CHECK_EQ(s_rw_count, 4);
    CHECK_EQ(s_rw[0].reg, MFP_REGISTER_GPDR); /* 0xEF00C1 should map to GPDR */
    CHECK_EQ(s_rw[1].reg, MFP_REGISTER_IERA); /* 0xEF00C7 should map to IERA */
    CHECK_EQ(s_rw[2].reg, MFP_REGISTER_ISRB); /* 0xEF00D1 should map to ISRB */
    CHECK_EQ(s_rw[3].reg, MFP_REGISTER_VR);   /* 0xEF00D7 should map to VR */
}

/* C#: MC68901MFPTests.cs:1540 */
ETH_TEST(MC68901MFPTests, Test_EthernetII_GPIP_Base_Mapping)
{
    MC68901MFP eth_mfp;
    MfpEvents ev;
    setup();
    memset(&ev, 0, sizeof ev);
    ev.on_register_write = rec_rw;
    mfp_create(&eth_mfp, 0xEF00C0u, 0x40u, &ev);
    s_rw_count = 0;
    mfp_write(&eth_mfp, 0xEF0080u + 0x40u, 0xAA);
    CHECK_EQ(s_rw[0].reg, MFP_REGISTER_GPDR);
    mfp_write(&eth_mfp, 0xEF0090u + 0x40u, 0x58);
    CHECK_EQ(s_rw[1].reg, MFP_REGISTER_ISRB);
    mfp_write(&eth_mfp, 0xEF009Cu + 0x40u, 0x11);
    CHECK_EQ(s_rw[2].reg, MFP_REGISTER_TCDCR);
}

/* C#: MC68901MFPTests.cs:1567 */
ETH_TEST(MC68901MFPTests, Test_GPIO_Edge_Detection_Generates_Interrupt)
{
    setup();

    s_irq_count = 0;

    // Configure: all pins as inputs, rising edge detection
    mfp_write(&s_mfp, AER, 0xFF);
    mfp_write(&s_mfp, DDR, 0x00);
    mfp_write(&s_mfp, IERA, 0xFF);
    mfp_write(&s_mfp, IERB, 0xFF);
    mfp_write(&s_mfp, IMRA, 0xFF);
    mfp_write(&s_mfp, IMRB, 0xFF);

    // Toggle GPIO pin 5 LOW → HIGH (rising edge)
    mfp_gpio_input(&s_mfp, 5, false);
    s_irq_count = 0;

    mfp_gpio_input(&s_mfp, 5, true);

    // Check that IRQ was asserted
    bool irqFired = false;
    for (int i = 0; i < s_irq_count; i++) {
    if (s_irq[i]) irqFired = true;
    }
    CHECK(irqFired); /* GPIO pin 5 rising edge should trigger IRQ */

}

/* C#: MC68901MFPTests.cs:1599 */
ETH_TEST(MC68901MFPTests, Test_TriggerSoftwareInterrupt_Sets_Pending)
{
    setup();

    s_irq_count = 0;

    // TriggerSoftwareInterrupt should force-enable and trigger
    mfp_trigger_software_interrupt(&s_mfp, 0, 0x40);

    // Check IPR bit 0 (GPIP0) is set
    uint8_t iprb = mfp_read(&s_mfp, IPRB);
    CHECK((iprb & 0x01) != (0)); /* IPRB bit 0 should be set after TriggerSoftwareInterrupt(0) */

    // Check IRQ was asserted
    bool irqFired = false;
    for (int i = 0; i < s_irq_count; i++) {
    if (s_irq[i]) irqFired = true;
    }
    CHECK(irqFired); /* TriggerSoftwareInterrupt should fire IRQ */

}

/* C#: MC68901MFPTests.cs:1625 */
ETH_TEST(MC68901MFPTests, Test_VR_Vector_Calculation)
{
    setup();

    // Set VR base to 0x40 (as firmware does)
    mfp_write(&s_mfp, VR, 0x48);

    uint8_t vr = mfp_read(&s_mfp, VR);
    CHECK_EQ(vr & 0xF0, 0x40); /* VR upper nibble should be 0x40 */
    CHECK_EQ(vr & 0x08, 0x08); /* VR S-bit should be set */

}

/* C#: MC68901MFPTests.cs:1642 */
ETH_TEST(MC68901MFPTests, Test_IER_IMR_Gating)
{
    setup();

    s_irq_count = 0;

    // Configure GPIO 0 for rising edge detection
    mfp_write(&s_mfp, AER, 0xFF);
    mfp_write(&s_mfp, DDR, 0x00);

    // Enable all interrupts but mask them
    mfp_write(&s_mfp, IERA, 0xFF);
    mfp_write(&s_mfp, IERB, 0xFF);
    mfp_write(&s_mfp, IMRA, 0x00);
    mfp_write(&s_mfp, IMRB, 0x00);

    // Trigger GPIO 0 interrupt (rising edge: LOW → HIGH)
    mfp_gpio_input(&s_mfp, 0, false);
    mfp_gpio_input(&s_mfp, 0, true);

    // IPR should be set but IRQ should NOT assert (masked)
    uint8_t iprb = mfp_read(&s_mfp, IPRB);
    CHECK((iprb & 0x01) != (0)); /* IPRB bit 0 should be pending */

    bool irqFired = false;
    for (int i = 0; i < s_irq_count; i++) {
    if (s_irq[i]) irqFired = true;
    }
    CHECK(!(irqFired)); /* IRQ should NOT fire when interrupt is masked */

    // Now unmask it
    s_irq_count = 0;
    mfp_write(&s_mfp, IMRB, 0x01);

    irqFired = false;
    for (int i = 0; i < s_irq_count; i++) {
    if (s_irq[i]) irqFired = true;
    }
    CHECK(irqFired); /* IRQ should fire after unmasking pending interrupt */

}

/* C#: MC68901MFPTests.cs:1690 */
ETH_TEST(MC68901MFPTests, Test_ISRB_Write_Values_Are_Received_By_Callback)
{
    setup();
    s_rw_count = 0;
    s_rw_only_isrb = true;
    s_mfp.ev.on_register_write = rec_rw;
    mfp_write(&s_mfp, ISRB, 0x00);
    mfp_write(&s_mfp, ISRB, 0xA0);
    mfp_write(&s_mfp, ISRB, 0xA8);
    mfp_write(&s_mfp, ISRB, 0xAC);
    mfp_write(&s_mfp, ISRB, 0xA4);
    CHECK_EQ(s_rw_count, 5);
    CHECK_EQ(s_rw[0].value, 0x00);
    CHECK_EQ(s_rw[1].value, 0xA0);
    CHECK_EQ(s_rw[2].value, 0xA8);
    CHECK_EQ(s_rw[3].value, 0xAC);
    CHECK_EQ(s_rw[4].value, 0xA4);
}

/* C#: MC68901MFPTests.cs:1720 */
ETH_TEST(MC68901MFPTests, Test_ISRB_Software_Interrupt_Pattern)
{
    setup();
    s_rw_count = 0;
    s_rw_only_isrb = true;
    s_mfp.ev.on_register_write = rec_rw;
    for (unsigned v = 0x58u; v <= 0x5Fu; v++)
    {
        mfp_write(&s_mfp, ISRB, (uint8_t)v);
    }
    CHECK_EQ(s_rw_count, 8);
    for (int i = 0; i < 8; i++)
    {
        CHECK_EQ(s_rw[i].value, 0x58 + i);
    }
}

/* C#: MC68901MFPTests.cs:1748 */
ETH_TEST(MC68901MFPTests, Test_Even_Odd_Address_Same_Register)
{
    MC68901MFP eth_mfp;
    uint8_t ddr;
    setup();
    mfp_create(&eth_mfp, 0xEF00C0u, 0x40u, NULL);
    mfp_write(&eth_mfp, 0xEF00C4u, 0xAA);
    ddr = mfp_read(&eth_mfp, 0xEF00C5u);
    CHECK_EQ(ddr, 0xAA);
    mfp_write(&eth_mfp, 0xEF00C5u, 0x55);
    ddr = mfp_read(&eth_mfp, 0xEF00C4u);
    CHECK_EQ(ddr, 0x55);
}

/* C#: MC68901MFPTests.cs:1770 */
ETH_TEST(MC68901MFPTests, Test_IEI_Disabled_Blocks_All_Interrupts)
{
    setup();

    mfp_set_iei(&s_mfp, false);

    mfp_write(&s_mfp, IERA, 0xFF);
    mfp_write(&s_mfp, IERB, 0xFF);
    mfp_write(&s_mfp, IMRA, 0xFF);
    mfp_write(&s_mfp, IMRB, 0xFF);

    s_irq_count = 0;

    // Trigger interrupt
    mfp_gpio_input(&s_mfp, 0, false);
    mfp_gpio_input(&s_mfp, 0, true);

    bool irqFired = false;
    for (int i = 0; i < s_irq_count; i++) {
    if (s_irq[i]) irqFired = true;
    }
    CHECK(!(irqFired)); /* IRQ should not fire when IEI is disabled */

}

/* C#: MC68901MFPTests.cs:1804 */
ETH_TEST(MC68901MFPTests, Test_StandardVectorMapping_GPIO6_UsesVRFormula)
{
    setup();

    s_mfp.use_system_vector_mapping = false;

    // Set VR = 0x48 (base 0x40, S-bit set for software EOI)
    mfp_write(&s_mfp, VR, 0x48);

    // Enable and unmask GPIO 6 interrupt (channel 14 = IR_GPIP_6)
    mfp_write(&s_mfp, IERA, 0x40);
    mfp_write(&s_mfp, IMRA, 0x40);

    // Configure AER for rising edge on GPIO 6
    mfp_write(&s_mfp, AER, 0x40);

    // Trigger GPIO 6: LOW → HIGH (rising edge)
    mfp_gpio_input(&s_mfp, 6, false);
    mfp_gpio_input(&s_mfp, 6, true);

    // Get vector - should be standard formula: (0x40 | 14) = 0x4E = 78
    uint8_t vector = mfp_get_interrupt_vector(&s_mfp);
    CHECK_EQ(vector, 0x4E); /* Standard mapping: GPIO 6 (channel 14) with VR=0x48 should produce vector 0x4E (78) */

}

/* C#: MC68901MFPTests.cs:1834 */
ETH_TEST(MC68901MFPTests, Test_SystemVectorMapping_GPIO6_ReturnsVector116)
{
    setup();

    s_mfp.use_system_vector_mapping = true;

    // Set VR = 0x48 (same as firmware would configure)
    mfp_write(&s_mfp, VR, 0x48);

    // Enable and unmask GPIO 6 interrupt
    mfp_write(&s_mfp, IERA, 0x40);
    mfp_write(&s_mfp, IMRA, 0x40);

    // Configure AER for rising edge on GPIO 6
    mfp_write(&s_mfp, AER, 0x40);

    // Trigger GPIO 6: LOW → HIGH
    mfp_gpio_input(&s_mfp, 6, false);
    mfp_gpio_input(&s_mfp, 6, true);

    uint8_t vector = mfp_get_interrupt_vector(&s_mfp);
    CHECK_EQ(vector, 116); /* System mapping: GPIO 6 should produce vector 116 (ND-100 interrupt) */

}

/* C#: MC68901MFPTests.cs:1863 */
ETH_TEST(MC68901MFPTests, Test_SystemVectorMapping_GPIO7_ReturnsVector117)
{
    setup();

    s_mfp.use_system_vector_mapping = true;

    mfp_write(&s_mfp, VR, 0x48);
    mfp_write(&s_mfp, IERA, 0x80);
    mfp_write(&s_mfp, IMRA, 0x80);
    mfp_write(&s_mfp, AER, 0x80);

    mfp_gpio_input(&s_mfp, 7, false);
    mfp_gpio_input(&s_mfp, 7, true);

    uint8_t vector = mfp_get_interrupt_vector(&s_mfp);
    CHECK_EQ(vector, 117); /* System mapping: GPIO 7 should produce vector 117 (write violation) */

}

/* C#: MC68901MFPTests.cs:1886 */
ETH_TEST(MC68901MFPTests, Test_SystemVectorMapping_GPIO5_ReturnsVector107)
{
    setup();

    s_mfp.use_system_vector_mapping = true;

    mfp_write(&s_mfp, VR, 0x48);
    mfp_write(&s_mfp, IERA, 0x00);
    mfp_write(&s_mfp, IERB, 0x80);
    mfp_write(&s_mfp, IMRA, 0x00);
    mfp_write(&s_mfp, IMRB, 0x80);
    mfp_write(&s_mfp, AER, 0x20);

    mfp_gpio_input(&s_mfp, 5, false);
    mfp_gpio_input(&s_mfp, 5, true);

    uint8_t vector = mfp_get_interrupt_vector(&s_mfp);
    CHECK_EQ(vector, 107); /* System mapping: GPIO 5 should produce vector 107 (LANCE memory error) */

}

/* C#: MC68901MFPTests.cs:1912 */
ETH_TEST(MC68901MFPTests, Test_SystemVectorMapping_TimerC_UsesStandardFormula)
{
    setup();

    s_mfp.use_system_vector_mapping = true;

    mfp_write(&s_mfp, VR, 0x48);

    // Enable Timer C interrupt (channel 5 = bit 5 of IERB)
    mfp_write(&s_mfp, IERB, 0x20);
    mfp_write(&s_mfp, IMRB, 0x20);

    // Trigger Timer C interrupt directly
    mfp_trigger_timer_c_interrupt(&s_mfp);

    uint8_t vector = mfp_get_interrupt_vector(&s_mfp);
    // Timer C = channel 5, standard formula: (0x40 | 5) = 0x45 = 69
    CHECK_EQ(vector, 0x45); /* Timer C should use standard formula, not system mapping */

}

/* C#: MC68901MFPTests.cs:1936 */
ETH_TEST(MC68901MFPTests, Test_SystemVectorMapping_RxBufferFull_ReturnsVector114)
{
    setup();

    s_mfp.use_system_vector_mapping = true;

    mfp_write(&s_mfp, VR, 0x48);

    // Enable RX buffer full interrupt (channel 12 = bit 4 of IERA)
    mfp_write(&s_mfp, IERA, 0x10);
    mfp_write(&s_mfp, IMRA, 0x10);

    // Trigger USART receive interrupt
    mfp_trigger_usart_receive_interrupt(&s_mfp);

    uint8_t vector = mfp_get_interrupt_vector(&s_mfp);
    CHECK_EQ(vector, 114); /* System mapping: RX buffer full should produce vector 114 */

}

/* C#: MC68901MFPTests.cs:1959 */
ETH_TEST(MC68901MFPTests, Test_SystemVectorMapping_TxBufferEmpty_ReturnsVector112)
{
    setup();

    s_mfp.use_system_vector_mapping = true;

    mfp_write(&s_mfp, VR, 0x48);

    // Enable TX buffer empty interrupt (channel 10 = bit 2 of IERA)
    mfp_write(&s_mfp, IERA, 0x04);
    mfp_write(&s_mfp, IMRA, 0x04);

    mfp_trigger_usart_transmit_interrupt(&s_mfp);

    uint8_t vector = mfp_get_interrupt_vector(&s_mfp);
    CHECK_EQ(vector, 112); /* System mapping: TX buffer empty should produce vector 112 */

}

/* C#: MC68901MFPTests.cs:1981 */
ETH_TEST(MC68901MFPTests, Test_SystemVectorMapping_TxError_ReturnsVector111)
{
    setup();

    s_mfp.use_system_vector_mapping = true;

    mfp_write(&s_mfp, VR, 0x48);

    // Enable TX error interrupt (channel 9 = bit 1 of IERA)
    mfp_write(&s_mfp, IERA, 0x02);
    mfp_write(&s_mfp, IMRA, 0x02);

    mfp_trigger_usart_transmit_error(&s_mfp);

    uint8_t vector = mfp_get_interrupt_vector(&s_mfp);
    CHECK_EQ(vector, 111); /* System mapping: TX error should produce vector 111 */

}

/* C#: MC68901MFPTests.cs:2003 */
ETH_TEST(MC68901MFPTests, Test_SystemVectorMapping_RxError_ReturnsVector113)
{
    setup();

    s_mfp.use_system_vector_mapping = true;

    mfp_write(&s_mfp, VR, 0x48);

    // Enable RX error interrupt (channel 11 = bit 3 of IERA)
    mfp_write(&s_mfp, IERA, 0x08);
    mfp_write(&s_mfp, IMRA, 0x08);

    mfp_trigger_usart_receive_error(&s_mfp);

    uint8_t vector = mfp_get_interrupt_vector(&s_mfp);
    CHECK_EQ(vector, 113); /* System mapping: RX error should produce vector 113 */

}

/* C#: MC68901MFPTests.cs:2026 */
ETH_TEST(MC68901MFPTests, Test_SystemVectorMapping_UnmappedChannel_FallsBackToStandard)
{
    setup();

    s_mfp.use_system_vector_mapping = true;

    // Set VR = 0x48 (base 0x40)
    mfp_write(&s_mfp, VR, 0x48);

    // Enable Timer A interrupt (channel 13 = bit 5 of IERA)
    // Timer A is NOT in the system mapping table → should use standard formula
    mfp_write(&s_mfp, IERA, 0x20);
    mfp_write(&s_mfp, IMRA, 0x20);

    // Trigger Timer A
    mfp_trigger_software_interrupt(&s_mfp, 13, 0x40);

    uint8_t vector = mfp_get_interrupt_vector(&s_mfp);
    // Standard formula: (0x40 | 13) = 0x4D = 77
    CHECK_EQ(vector, 0x4D); /* Unmapped Timer A should fall back to standard formula: (0x40 | 13) = 0x4D */

}

/* C#: MC68901MFPTests.cs:2052 */
ETH_TEST(MC68901MFPTests, Test_UseSystemVectorMapping_DefaultIsFalse)
{
    MC68901MFP fresh_mfp;
    setup();
    mfp_create(&fresh_mfp, 0x1000u, 0x30u, NULL);
    CHECK(!(fresh_mfp.use_system_vector_mapping));
}

/* C#: MC68901MFPTests.cs:2065 */
ETH_TEST(MC68901MFPTests, Test_SystemVectorMapping_Priority_GPIO7_BeforeGPIO6)
{
    setup();

    s_mfp.use_system_vector_mapping = true;

    mfp_write(&s_mfp, VR, 0x48);
    mfp_write(&s_mfp, IERA, 0xC0);
    mfp_write(&s_mfp, IMRA, 0xC0);
    mfp_write(&s_mfp, AER, 0xC0);

    // Trigger both GPIO 6 and 7
    mfp_gpio_input(&s_mfp, 6, false);
    mfp_gpio_input(&s_mfp, 7, false);
    mfp_gpio_input(&s_mfp, 6, true);
    mfp_gpio_input(&s_mfp, 7, true);

    // First vector should be GPIO 7 (higher priority) = 117
    uint8_t vector1 = mfp_get_interrupt_vector(&s_mfp);
    CHECK_EQ(vector1, 117); /* GPIO 7 (vector 117) should be serviced first (higher priority) */

    // Clear GPIO 7 ISR (software EOI) so GPIO 6 is unblocked
    mfp_write(&s_mfp, ISRA, 0x00);

    // Second vector should be GPIO 6 = 116
    uint8_t vector2 = mfp_get_interrupt_vector(&s_mfp);
    CHECK_EQ(vector2, 116); /* GPIO 6 (vector 116) should be serviced second after clearing ISR */

}

/* C#: MC68901MFPTests.cs:2104 */
ETH_TEST(MC68901MFPTests, Test_Timer_CounterNeverReachesZero)
{
    setup();

    // TDR=3, delay /4. Counter should go: 3 → 2 → 1 → (reload) → 3
    mfp_write(&s_mfp, TADR, 0x03);
    mfp_write(&s_mfp, TACR, 0x01);

    // After 4 clocks: tmc should go from 3 to 2
    for (int i = 0; i < 4; i++) mfp_clock(&s_mfp);
    uint8_t tmc1 = mfp_read(&s_mfp, TADR);
    CHECK_EQ(tmc1, 0x02); /* After 1 count: tmc should be 2 */

    // After 4 more clocks: tmc should go from 2 to 1
    for (int i = 0; i < 4; i++) mfp_clock(&s_mfp);
    uint8_t tmc2 = mfp_read(&s_mfp, TADR);
    CHECK_EQ(tmc2, 0x01); /* After 2 counts: tmc should be 1 */

    // After 4 more clocks: tmc==1 triggers reload, tmc should be back to 3
    // It must NOT pass through 0.
    for (int i = 0; i < 4; i++) mfp_clock(&s_mfp);
    uint8_t tmc3 = mfp_read(&s_mfp, TADR);
    CHECK_EQ(tmc3, 0x03); /* After 3 counts: tmc should reload to TDR (3), never reaching 0 */

}

/* C#: MC68901MFPTests.cs:2133 */
ETH_TEST(MC68901MFPTests, Test_Timer_InterruptFiresAtOne)
{
    setup();

    mfp_write(&s_mfp, IERA, 0x20);
    mfp_write(&s_mfp, IMRA, 0x20);
    mfp_write(&s_mfp, VR, 0x40);

    mfp_write(&s_mfp, TADR, 0x02);
    mfp_write(&s_mfp, TACR, 0x01);

    s_irq_count = 0;

    // First count: 2 → 1 (no interrupt yet, tmc is now 1)
    for (int i = 0; i < 4; i++) mfp_clock(&s_mfp);
    uint8_t tmc = mfp_read(&s_mfp, TADR);
    CHECK_EQ(tmc, 0x01); /* After 1 count: tmc should be 1 */

    // Second count: tmc==1 triggers reload+interrupt
    s_irq_count = 0;
    for (int i = 0; i < 4; i++) mfp_clock(&s_mfp);

    CHECK((s_irq_count) > (0)); /* IRQ should fire when tmc was 1 */
    uint8_t ipra = mfp_read(&s_mfp, IPRA);
    // IPR bit 5 should be set (Timer A)
    CHECK_EQ(ipra & 0x20, 0x20); /* Timer A interrupt should be pending */

}

/* C#: MC68901MFPTests.cs:2165 */
ETH_TEST(MC68901MFPTests, Test_Timer_TDR1_FiresEveryCount)
{
    setup();

    mfp_write(&s_mfp, IERA, 0x20);
    mfp_write(&s_mfp, IMRA, 0x20);
    mfp_write(&s_mfp, VR, 0x40);

    mfp_write(&s_mfp, TADR, 0x01);
    mfp_write(&s_mfp, TACR, 0x01);

    s_tout_count = 0;

    // 4 clocks = 1 count; with TDR=1 each count triggers reload
    for (int i = 0; i < 4; i++) mfp_clock(&s_mfp);

    bool foundTimerA = false;
    for (int i = 0; i < s_tout_count; i++) {
    if (s_tout[i].name == MFP_TIMER_NAME_A) {
    foundTimerA = true;
    break;
    }
    }
    CHECK(foundTimerA); /* TDR=1: timer should fire on every count */

}

/* C#: MC68901MFPTests.cs:2198 */
ETH_TEST(MC68901MFPTests, Test_Timer_TDR0_Means256Counts)
{
    setup();

    mfp_write(&s_mfp, TADR, 0x00);
    mfp_write(&s_mfp, TACR, 0x01);

    // TMC starts at 0 (loaded from TDR). First count decrements: 0 wraps to 255.
    for (int i = 0; i < 4; i++) mfp_clock(&s_mfp);
    uint8_t tmc = mfp_read(&s_mfp, TADR);
    CHECK_EQ(tmc, 0xFF); /* TDR=0: first decrement should wrap to 255 (256-count mode) */

}

/* C#: MC68901MFPTests.cs:2221 */
ETH_TEST_FLAGS(MC68901MFPTests, Test_Timer_TDR_NotReloadedInEventCountMode, ETH_TEST_EXPECT_FAIL) /* cs-fails-in-retrocore */
{
    setup();

    // Set up Timer B in event count mode with initial data
    mfp_write(&s_mfp, TBDR, 0x05);
    mfp_write(&s_mfp, TBCR, 0x08);

    // Count down a couple edges
    mfp_write(&s_mfp, AER, 0x00);
    mfp_timer_input_b(&s_mfp, true);
    mfp_timer_input_b(&s_mfp, false);
    mfp_timer_input_b(&s_mfp, true);
    mfp_timer_input_b(&s_mfp, false);

    uint8_t tmcBefore = mfp_read(&s_mfp, TBDR);
    CHECK_EQ(tmcBefore, 0x03); /* TMC should be 3 after 2 edge counts from 5 */

    // Now write new TDR value while in event count mode — TMC must NOT reload
    mfp_write(&s_mfp, TBDR, 0x0A);

    uint8_t tmcAfter = mfp_read(&s_mfp, TBDR);
    CHECK_EQ(tmcAfter, 0x03); /* TMC must NOT reload when TDR written in event count mode */

}

/* C#: MC68901MFPTests.cs:2250 */
ETH_TEST(MC68901MFPTests, Test_Timer_TDR_ReloadsWhenStopped)
{
    setup();

    mfp_write(&s_mfp, TBCR, 0x00);
    mfp_write(&s_mfp, TBDR, 0x42);

    uint8_t tmc = mfp_read(&s_mfp, TBDR);
    CHECK_EQ(tmc, 0x42); /* TMC should reload from TDR when timer is stopped */

}

/* C#: MC68901MFPTests.cs:2265 */
ETH_TEST(MC68901MFPTests, Test_Timer_TDR_NotReloadedInDelayMode)
{
    setup();

    mfp_write(&s_mfp, TBDR, 0x20);
    mfp_write(&s_mfp, TBCR, 0x01);

    // Write new TDR — should NOT reload TMC
    mfp_write(&s_mfp, TBDR, 0xFF);

    uint8_t tmc = mfp_read(&s_mfp, TBDR);
    CHECK_EQ(tmc, 0x20); /* TMC must NOT reload when TDR written while timer is running */

}

/* C#: MC68901MFPTests.cs:2288 */
ETH_TEST(MC68901MFPTests, Test_VR_StoresFullByte)
{
    setup();

    mfp_write(&s_mfp, VR, 0x4B);
    uint8_t vr = mfp_read(&s_mfp, VR);
    CHECK_EQ(vr, 0x4B); /* VR should store full byte including bits 0-2 */

}

/* C#: MC68901MFPTests.cs:2301 */
ETH_TEST(MC68901MFPTests, Test_VR_LowBitsReadBack)
{
    setup();

    mfp_write(&s_mfp, VR, 0x47);
    uint8_t vr = mfp_read(&s_mfp, VR);
    CHECK_EQ(vr & 0x07, 0x07); /* VR bits 0-2 should read back as written */

}

/* C#: MC68901MFPTests.cs:2316 */
ETH_TEST(MC68901MFPTests, Test_VR_ISROnlyClearedOnTransition)
{
    setup();

    // Start in auto EOI mode
    mfp_write(&s_mfp, VR, 0x40);

    // Set up an interrupt and get it in-service via software EOI mode
    mfp_write(&s_mfp, VR, 0x48);
    mfp_write(&s_mfp, IERA, 0x20);
    mfp_write(&s_mfp, IMRA, 0x20);

    // Trigger Timer A interrupt (bit 5 of A)
    mfp_write(&s_mfp, TADR, 0x01);
    mfp_write(&s_mfp, TACR, 0x01);
    for (int i = 0; i < 4; i++) mfp_clock(&s_mfp);

    // IACK to set ISR
    mfp_handle_interrupt_acknowledge(&s_mfp);
    uint8_t isra = mfp_read(&s_mfp, ISRA);
    CHECK_EQ(isra & 0x20, 0x20); /* ISR should be set after IACK in software EOI mode */

    // Writing VR with same software EOI value should NOT clear ISR
    mfp_write(&s_mfp, VR, 0x48);
    isra = mfp_read(&s_mfp, ISRA);
    CHECK_EQ(isra & 0x20, 0x20); /* ISR should NOT be cleared when VR rewritten with same S-bit */

    // Now switch to auto EOI (bit 3: 1→0) — THIS should clear ISR
    mfp_write(&s_mfp, VR, 0x40);
    isra = mfp_read(&s_mfp, ISRA);
    CHECK_EQ(isra, 0x00); /* ISR should be cleared when switching from software to auto EOI */

}

/* C#: MC68901MFPTests.cs:2354 */
ETH_TEST(MC68901MFPTests, Test_VR_AutoToAutoDoesNotClearISR)
{
    setup();

    // Start in auto EOI mode
    mfp_write(&s_mfp, VR, 0x40);

    // Re-write VR with auto mode again (no transition)
    mfp_write(&s_mfp, VR, 0x40);

    // Should not have fired ISR clear (nothing to clear anyway, but verifies no crash)
    uint8_t isra = mfp_read(&s_mfp, ISRA);
    CHECK_EQ(isra, 0x00); /* ISR should remain 0 when rewriting auto EOI */

}

/* C#: MC68901MFPTests.cs:2378 */
ETH_TEST(MC68901MFPTests, Test_IMR_WriteDoesNotClearISR)
{
    setup();

    // Software EOI mode
    mfp_write(&s_mfp, VR, 0x48);

    // Enable and unmask Timer A
    mfp_write(&s_mfp, IERA, 0x20);
    mfp_write(&s_mfp, IMRA, 0x20);

    // Trigger Timer A interrupt
    mfp_write(&s_mfp, TADR, 0x01);
    mfp_write(&s_mfp, TACR, 0x01);
    for (int i = 0; i < 4; i++) mfp_clock(&s_mfp);

    // IACK to set ISR bit
    mfp_handle_interrupt_acknowledge(&s_mfp);
    uint8_t isra = mfp_read(&s_mfp, ISRA);
    CHECK_EQ(isra & 0x20, 0x20); /* ISR bit should be set after IACK */

    // Now write IMR to mask Timer A — ISR must NOT be cleared
    mfp_write(&s_mfp, IMRA, 0x00);
    isra = mfp_read(&s_mfp, ISRA);
    CHECK_EQ(isra & 0x20, 0x20); /* IMR write must NOT clear ISR bits */

}

/* C#: MC68901MFPTests.cs:2409 */
ETH_TEST(MC68901MFPTests, Test_IMRB_WriteDoesNotClearISRB)
{
    setup();

    mfp_write(&s_mfp, VR, 0x48);

    // Enable GPIO 0 interrupt (bit 0 of B)
    mfp_write(&s_mfp, IERB, 0x01);
    mfp_write(&s_mfp, IMRB, 0x01);
    mfp_write(&s_mfp, AER, 0x01);

    // Trigger GPIO 0 interrupt
    mfp_gpio_input(&s_mfp, 0, false);
    mfp_gpio_input(&s_mfp, 0, true);

    // IACK
    mfp_handle_interrupt_acknowledge(&s_mfp);
    uint8_t isrb = mfp_read(&s_mfp, ISRB);
    CHECK_EQ(isrb & 0x01, 0x01); /* ISRB bit 0 should be set after IACK */

    // Write IMRB — ISR must NOT be cleared
    mfp_write(&s_mfp, IMRB, 0x00);
    isrb = mfp_read(&s_mfp, ISRB);
    CHECK_EQ(isrb & 0x01, 0x01); /* IMRB write must NOT clear ISRB bits */

}

/* C#: MC68901MFPTests.cs:2445 */
ETH_TEST(MC68901MFPTests, Test_ISR_PriorityBlocking_LowerPriorityBlocked)
{
    setup();

    mfp_write(&s_mfp, VR, 0x48);

    // Enable Timer A (ch 13, bit 5 of A) and GPIO 0 (ch 0, bit 0 of B)
    mfp_write(&s_mfp, IERA, 0x20);
    mfp_write(&s_mfp, IERB, 0x01);
    mfp_write(&s_mfp, IMRA, 0x20);
    mfp_write(&s_mfp, IMRB, 0x01);

    // Trigger Timer A interrupt and IACK to put it in service
    mfp_write(&s_mfp, TADR, 0x01);
    mfp_write(&s_mfp, TACR, 0x01);
    for (int i = 0; i < 4; i++) mfp_clock(&s_mfp);

    mfp_handle_interrupt_acknowledge(&s_mfp);
    uint8_t isra = mfp_read(&s_mfp, ISRA);
    CHECK_EQ(isra & 0x20, 0x20); /* Timer A should be in-service */

    // Now trigger GPIO 0 (lower priority) — IRQ should NOT assert
    s_irq_count = 0;
    mfp_write(&s_mfp, AER, 0x01);
    mfp_gpio_input(&s_mfp, 0, false);
    mfp_gpio_input(&s_mfp, 0, true);

    // GPIO 0 should be pending
    uint8_t iprb = mfp_read(&s_mfp, IPRB);
    CHECK_EQ(iprb & 0x01, 0x01); /* GPIO 0 should be pending */

    // But IRQ should not assert because Timer A (higher priority) is in-service
    bool anyIrqAsserted = false;
    for (int i = 0; i < s_irq_count; i++) {
    if (s_irq[i]) anyIrqAsserted = true;
    }
    CHECK(!(anyIrqAsserted)); /* Lower-priority IRQ must NOT assert while higher-priority is in-service */

}

/* C#: MC68901MFPTests.cs:2490 */
ETH_TEST(MC68901MFPTests, Test_ISR_PriorityBlocking_HigherPriorityNotBlocked)
{
    setup();

    mfp_write(&s_mfp, VR, 0x48);

    // Enable GPIO 0 (ch 0, low priority) and Timer A (ch 13, high priority)
    mfp_write(&s_mfp, IERB, 0x01);
    mfp_write(&s_mfp, IMRB, 0x01);
    mfp_write(&s_mfp, IERA, 0x20);
    mfp_write(&s_mfp, IMRA, 0x20);
    mfp_write(&s_mfp, AER, 0x01);

    // Trigger GPIO 0 and IACK to put it in service (low priority in-service)
    mfp_gpio_input(&s_mfp, 0, false);
    mfp_gpio_input(&s_mfp, 0, true);
    mfp_handle_interrupt_acknowledge(&s_mfp);

    uint8_t isrb = mfp_read(&s_mfp, ISRB);
    CHECK_EQ(isrb & 0x01, 0x01); /* GPIO 0 should be in-service */

    // Now trigger Timer A (higher priority) — IRQ SHOULD assert
    s_irq_count = 0;
    mfp_write(&s_mfp, TADR, 0x01);
    mfp_write(&s_mfp, TACR, 0x01);
    for (int i = 0; i < 4; i++) mfp_clock(&s_mfp);

    bool irqAsserted = false;
    for (int i = 0; i < s_irq_count; i++) {
    if (s_irq[i]) irqAsserted = true;
    }
    CHECK(irqAsserted); /* Higher-priority interrupt should assert even with lower-priority in-service */

}

/* C#: MC68901MFPTests.cs:2530 */
ETH_TEST(MC68901MFPTests, Test_GetVector_ISRPriorityBlocking)
{
    setup();

    mfp_write(&s_mfp, VR, 0x48);

    // Enable Timer A (ch 13) and GPIO 0 (ch 0)
    mfp_write(&s_mfp, IERA, 0x20);
    mfp_write(&s_mfp, IERB, 0x01);
    mfp_write(&s_mfp, IMRA, 0x20);
    mfp_write(&s_mfp, IMRB, 0x01);
    mfp_write(&s_mfp, AER, 0x01);

    // Trigger Timer A and IACK — puts Timer A in-service
    mfp_write(&s_mfp, TADR, 0x01);
    mfp_write(&s_mfp, TACR, 0x01);
    for (int i = 0; i < 4; i++) mfp_clock(&s_mfp);
    uint8_t vec1 = mfp_handle_interrupt_acknowledge(&s_mfp);
    CHECK_EQ(vec1 & 0x0F, 13); /* First vector should be Timer A (ch 13) */

    // Now trigger GPIO 0 (lower priority)
    mfp_gpio_input(&s_mfp, 0, false);
    mfp_gpio_input(&s_mfp, 0, true);

    // IACK should NOT service GPIO 0 — blocked by Timer A ISR
    uint8_t vec2 = mfp_handle_interrupt_acknowledge(&s_mfp);

    // With ISR blocking, get_vector should not service any channel
    // It returns spurious vector (0x18) or daisy chain vector
    CHECK((vec2 & 0x0F) != (0)); /* get_vector should not service GPIO 0 while Timer A is in-service */

}

/* C#: MC68901MFPTests.cs:2566 */
ETH_TEST(MC68901MFPTests, Test_ISR_ClearUnblocksLowerPriority)
{
    setup();

    mfp_write(&s_mfp, VR, 0x48);

    // Enable Timer A (ch 13) and GPIO 0 (ch 0)
    mfp_write(&s_mfp, IERA, 0x20);
    mfp_write(&s_mfp, IERB, 0x01);
    mfp_write(&s_mfp, IMRA, 0x20);
    mfp_write(&s_mfp, IMRB, 0x01);
    mfp_write(&s_mfp, AER, 0x01);

    // Trigger Timer A, IACK to put in service
    mfp_write(&s_mfp, TADR, 0x01);
    mfp_write(&s_mfp, TACR, 0x01);
    for (int i = 0; i < 4; i++) mfp_clock(&s_mfp);
    mfp_handle_interrupt_acknowledge(&s_mfp);

    // Trigger GPIO 0 (blocked while Timer A in-service)
    mfp_gpio_input(&s_mfp, 0, false);
    mfp_gpio_input(&s_mfp, 0, true);

    // Clear Timer A ISR (software EOI)
    mfp_write(&s_mfp, ISRA, 0x00);

    // Now GPIO 0 should be serviceable
    s_irq_count = 0;
    // Force re-check by reading state
    uint8_t iprb = mfp_read(&s_mfp, IPRB);
    CHECK_EQ(iprb & 0x01, 0x01); /* GPIO 0 should still be pending */

    // IACK should now service GPIO 0
    uint8_t vec = mfp_handle_interrupt_acknowledge(&s_mfp);
    CHECK_EQ(vec & 0x0F, 0); /* After clearing ISR, GPIO 0 (ch 0) should be serviceable */

}

/* C#: MC68901MFPTests.cs:2608 */
ETH_TEST(MC68901MFPTests, Test_AutoEOI_NoPriorityBlocking)
{
    setup();

    mfp_write(&s_mfp, VR, 0x40);

    // Enable Timer A (ch 13) and GPIO 0 (ch 0)
    mfp_write(&s_mfp, IERA, 0x20);
    mfp_write(&s_mfp, IERB, 0x01);
    mfp_write(&s_mfp, IMRA, 0x20);
    mfp_write(&s_mfp, IMRB, 0x01);
    mfp_write(&s_mfp, AER, 0x01);

    // Trigger both
    mfp_write(&s_mfp, TADR, 0x01);
    mfp_write(&s_mfp, TACR, 0x01);
    for (int i = 0; i < 4; i++) mfp_clock(&s_mfp);
    mfp_gpio_input(&s_mfp, 0, false);
    mfp_gpio_input(&s_mfp, 0, true);

    // First IACK: Timer A (higher priority)
    uint8_t vec1 = mfp_handle_interrupt_acknowledge(&s_mfp);
    CHECK_EQ(vec1 & 0x0F, 13); /* Timer A should be serviced first */

    // Second IACK: GPIO 0 — should work because auto EOI doesn't set ISR
    uint8_t vec2 = mfp_handle_interrupt_acknowledge(&s_mfp);
    CHECK_EQ(vec2 & 0x0F, 0); /* GPIO 0 should be serviced in auto EOI (no ISR blocking) */

}

/* C#: MC68901MFPTests.cs:2647 */
ETH_TEST(MC68901MFPTests, Test_TimerB_EventCount_TOSBootPattern)
{
    setup();

    // TOS boot pattern: write TBDR=1, enable event count, wait for interrupt
    mfp_write(&s_mfp, IERA, 0x01);
    mfp_write(&s_mfp, IMRA, 0x01);
    mfp_write(&s_mfp, VR, 0x40);

    mfp_write(&s_mfp, TBDR, 0x01);
    mfp_write(&s_mfp, TBCR, 0x08);

    // Configure AER for falling edge on TBI
    mfp_write(&s_mfp, AER, 0x00);

    s_irq_count = 0;

    // Trigger one edge — should count from 1 to reload (tmc==1 → reload+interrupt)
    mfp_timer_input_b(&s_mfp, true);
    mfp_timer_input_b(&s_mfp, false);

    // IRQ should have fired
    bool irqFired = false;
    for (int i = 0; i < s_irq_count; i++) {
    if (s_irq[i]) irqFired = true;
    }
    CHECK(irqFired); /* Timer B event count with TDR=1 should fire interrupt on first edge */

}

/* C#: MC68901MFPTests.cs:2681 */
ETH_TEST(MC68901MFPTests, Test_TimerB_EventCount_Countdown)
{
    setup();

    mfp_write(&s_mfp, TBDR, 0x03);
    mfp_write(&s_mfp, TBCR, 0x08);
    mfp_write(&s_mfp, AER, 0x00);

    // Edge 1: 3 → 2
    mfp_timer_input_b(&s_mfp, true);
    mfp_timer_input_b(&s_mfp, false);
    uint8_t tmc1 = mfp_read(&s_mfp, TBDR);
    CHECK_EQ(tmc1, 0x02); /* After 1 edge: tmc should be 2 */

    // Edge 2: 2 → 1
    mfp_timer_input_b(&s_mfp, true);
    mfp_timer_input_b(&s_mfp, false);
    uint8_t tmc2 = mfp_read(&s_mfp, TBDR);
    CHECK_EQ(tmc2, 0x01); /* After 2 edges: tmc should be 1 */

    // Edge 3: tmc==1 → reload to 3 + interrupt
    mfp_timer_input_b(&s_mfp, true);
    mfp_timer_input_b(&s_mfp, false);
    uint8_t tmc3 = mfp_read(&s_mfp, TBDR);
    CHECK_EQ(tmc3, 0x03); /* After 3 edges: tmc should reload to TDR (3) */

}


/* ---- Port of RetroCore Emulated.Tests/MC68k/TestMFP_Timers.cs ------------- */

static MC68901MFP s_tmfp;
static uint8_t s_tmfp_last_gpio;

static void tmfp_on_gpio(void *ctx, uint8_t data)
{
    (void)ctx;
    s_tmfp_last_gpio = data;
}

/* [SetUp] Setup(): MC68901MFP(0xEF00C0, 0x3F); the OnIRQ / OnTimerOutput
 * handlers there only print, so they are not attached here. */
static void tmfp_setup(void)
{
    MfpEvents ev;

    memset(&ev, 0, sizeof ev);
    ev.on_gpio = tmfp_on_gpio;
    mfp_create(&s_tmfp, 0xEF00C0u, 0x3Fu, &ev);
    s_tmfp_last_gpio = 0u;
}

/* C#: TestMFP_Timers.cs:52 */
ETH_TEST(TestMFP_Timers, GPIO_test_write)
{
    const uint32_t start_address = 0xEF00C0u;
    uint8_t expected = 0xA5u;
    uint8_t was;

    tmfp_setup();
    mfp_write(&s_tmfp, start_address + 0x05u, 0xFF);
    mfp_write(&s_tmfp, start_address + 0x01u, expected);
    was = mfp_read(&s_tmfp, start_address + 0x01u);
    CHECK_EQ(expected, was);
    CHECK_EQ(expected, s_tmfp_last_gpio);
    CHECK(true); /* C# Assert.Pass() */
}
