/*
 * eth_mfp.h - Motorola MC68901 MFP, ported from RetroCore.
 *
 * nd100x - ND100 Virtual Machine
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (c) 2026 Ronny Hansen
 *
 * Port of RetroCore Emulated.HW/Motorola/MFP/MC68901 (commit 935163f):
 * HelperEnum.cs, MfpTimer.cs, Registers.cs, Usart.cs, MC68901MFP.cs. The C#
 * behaviour is copied as it is, including parts that look wrong (see the
 * phase log for the list); C# events are callback pointers here.
 *
 * Reference manual named by the C#: MC68901UM (NXP).
 */

#ifndef ETH_MFP_H
#define ETH_MFP_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* ---- HelperEnum.cs -------------------------------------------------------- */

/** MFPRegister: register index = (address - base) >> 1 (map2register). */
typedef enum
{
    MFP_REGISTER_GPDR = 0, /* cs: none - C# enum, implicit values from 0 */
    MFP_REGISTER_AER,
    MFP_REGISTER_DDR,
    MFP_REGISTER_IERA,
    MFP_REGISTER_IERB,
    MFP_REGISTER_IPRA,
    MFP_REGISTER_IPRB,
    MFP_REGISTER_ISRA,
    MFP_REGISTER_ISRB,
    MFP_REGISTER_IMRA,
    MFP_REGISTER_IMRB,
    MFP_REGISTER_VR,
    MFP_REGISTER_TACR,
    MFP_REGISTER_TBCR,
    MFP_REGISTER_TCDCR,
    MFP_REGISTER_TADR,
    MFP_REGISTER_TBDR,
    MFP_REGISTER_TCDR,
    MFP_REGISTER_TDDR,
    MFP_REGISTER_SCR,
    MFP_REGISTER_UCR,
    MFP_REGISTER_RSR,
    MFP_REGISTER_TSR,
    MFP_REGISTER_UDR
} MFPRegister;

/** MfpTimerName */
typedef enum
{
    MFP_TIMER_NAME_A = 0, /* cs: Emulated.HW/Motorola/MFP/MC68901/HelperEnum.cs:116 */
    MFP_TIMER_NAME_B,
    MFP_TIMER_NAME_C,
    MFP_TIMER_NAME_D
} MfpTimerName;

/** InterruptVectorNumber: one bit per MFP interrupt channel (channel = bit). */
#define INTERRUPT_VECTOR_NUMBER_IR_GPIP_0 0x0001u /* cs: Emulated.HW/Motorola/MFP/MC68901/HelperEnum.cs:126 */
#define INTERRUPT_VECTOR_NUMBER_IR_GPIP_1 0x0002u /* cs: Emulated.HW/Motorola/MFP/MC68901/HelperEnum.cs:128 */
#define INTERRUPT_VECTOR_NUMBER_IR_GPIP_2 0x0004u /* cs: Emulated.HW/Motorola/MFP/MC68901/HelperEnum.cs:130 */
#define INTERRUPT_VECTOR_NUMBER_IR_GPIP_3 0x0008u /* cs: Emulated.HW/Motorola/MFP/MC68901/HelperEnum.cs:132 */
#define INTERRUPT_VECTOR_NUMBER_IR_TIMER_D 0x0010u /* cs: Emulated.HW/Motorola/MFP/MC68901/HelperEnum.cs:134 */
#define INTERRUPT_VECTOR_NUMBER_IR_TIMER_C 0x0020u /* cs: Emulated.HW/Motorola/MFP/MC68901/HelperEnum.cs:136 */
#define INTERRUPT_VECTOR_NUMBER_IR_GPIP_4 0x0040u /* cs: Emulated.HW/Motorola/MFP/MC68901/HelperEnum.cs:138 */
#define INTERRUPT_VECTOR_NUMBER_IR_GPIP_5 0x0080u /* cs: Emulated.HW/Motorola/MFP/MC68901/HelperEnum.cs:140 */
#define INTERRUPT_VECTOR_NUMBER_IR_TIMER_B 0x0100u /* cs: Emulated.HW/Motorola/MFP/MC68901/HelperEnum.cs:142 */
#define INTERRUPT_VECTOR_NUMBER_IR_XMIT_ERROR 0x0200u /* cs: Emulated.HW/Motorola/MFP/MC68901/HelperEnum.cs:144 */
#define INTERRUPT_VECTOR_NUMBER_IR_XMIT_BUFFER_EMPTY 0x0400u /* cs: Emulated.HW/Motorola/MFP/MC68901/HelperEnum.cs:146 */
#define INTERRUPT_VECTOR_NUMBER_IR_RCV_ERROR 0x0800u /* cs: Emulated.HW/Motorola/MFP/MC68901/HelperEnum.cs:148 */
#define INTERRUPT_VECTOR_NUMBER_IR_RCV_BUFFER_FULL 0x1000u /* cs: Emulated.HW/Motorola/MFP/MC68901/HelperEnum.cs:150 */
#define INTERRUPT_VECTOR_NUMBER_IR_TIMER_A 0x2000u /* cs: Emulated.HW/Motorola/MFP/MC68901/HelperEnum.cs:152 */
#define INTERRUPT_VECTOR_NUMBER_IR_GPIP_6 0x4000u /* cs: Emulated.HW/Motorola/MFP/MC68901/HelperEnum.cs:154 */
#define INTERRUPT_VECTOR_NUMBER_IR_GPIP_7 0x8000u /* cs: Emulated.HW/Motorola/MFP/MC68901/HelperEnum.cs:156 */

/** GPIP pin numbers */
typedef enum
{
    GPIP_GPIP_0 = 0, /* cs: Emulated.HW/Motorola/MFP/MC68901/HelperEnum.cs:160 */
    GPIP_GPIP_1,
    GPIP_GPIP_2,
    GPIP_GPIP_3,
    GPIP_GPIP_4,
    GPIP_GPIP_5,
    GPIP_GPIP_6,
    GPIP_GPIP_7
} GPIP;

/** TCR_STATE: timer control register values */
#define TCR_STATE_TCR_TIMER_STOPPED 0x00u /* cs: Emulated.HW/Motorola/MFP/MC68901/HelperEnum.cs:174 */
#define TCR_STATE_TCR_TIMER_DELAY_4 0x01u /* cs: Emulated.HW/Motorola/MFP/MC68901/HelperEnum.cs:175 */
#define TCR_STATE_TCR_TIMER_DELAY_10 0x02u /* cs: Emulated.HW/Motorola/MFP/MC68901/HelperEnum.cs:176 */
#define TCR_STATE_TCR_TIMER_DELAY_16 0x03u /* cs: Emulated.HW/Motorola/MFP/MC68901/HelperEnum.cs:177 */
#define TCR_STATE_TCR_TIMER_DELAY_50 0x04u /* cs: Emulated.HW/Motorola/MFP/MC68901/HelperEnum.cs:178 */
#define TCR_STATE_TCR_TIMER_DELAY_64 0x05u /* cs: Emulated.HW/Motorola/MFP/MC68901/HelperEnum.cs:179 */
#define TCR_STATE_TCR_TIMER_DELAY_100 0x06u /* cs: Emulated.HW/Motorola/MFP/MC68901/HelperEnum.cs:180 */
#define TCR_STATE_TCR_TIMER_DELAY_200 0x07u /* cs: Emulated.HW/Motorola/MFP/MC68901/HelperEnum.cs:181 */
#define TCR_STATE_TCR_TIMER_EVENT 0x08u /* cs: Emulated.HW/Motorola/MFP/MC68901/HelperEnum.cs:182 */
#define TCR_STATE_TCR_TIMER_PULSE_4 0x09u /* cs: Emulated.HW/Motorola/MFP/MC68901/HelperEnum.cs:183 */
#define TCR_STATE_TCR_TIMER_PULSE_10 0x0Au /* cs: Emulated.HW/Motorola/MFP/MC68901/HelperEnum.cs:184 */
#define TCR_STATE_TCR_TIMER_PULSE_16 0x0Bu /* cs: Emulated.HW/Motorola/MFP/MC68901/HelperEnum.cs:185 */
#define TCR_STATE_TCR_TIMER_PULSE_50 0x0Cu /* cs: Emulated.HW/Motorola/MFP/MC68901/HelperEnum.cs:186 */
#define TCR_STATE_TCR_TIMER_PULSE_64 0x0Du /* cs: Emulated.HW/Motorola/MFP/MC68901/HelperEnum.cs:187 */
#define TCR_STATE_TCR_TIMER_PULSE_100 0x0Eu /* cs: Emulated.HW/Motorola/MFP/MC68901/HelperEnum.cs:188 */
#define TCR_STATE_TCR_TIMER_PULSE_200 0x0Fu /* cs: Emulated.HW/Motorola/MFP/MC68901/HelperEnum.cs:189 */
#define TCR_STATE_TCR_TIMER_RESET 0x10u /* cs: Emulated.HW/Motorola/MFP/MC68901/HelperEnum.cs:190 */

/** UCR_FLAGS */
#define UCR_FLAGS_UCR_PARITY_ENABLED 0x04u /* cs: none - (1 << 2), HelperEnum.cs:197 */
#define UCR_FLAGS_UCR_PARITY_EVEN 0x02u /* cs: none - (1 << 1), HelperEnum.cs:198 */
#define UCR_FLAGS_UCR_PARITY_ODD 0x00u /* cs: Emulated.HW/Motorola/MFP/MC68901/HelperEnum.cs:199 */
#define UCR_FLAGS_UCR_WORD_LENGTH_8 0x00u /* cs: Emulated.HW/Motorola/MFP/MC68901/HelperEnum.cs:200 */
#define UCR_FLAGS_UCR_WORD_LENGTH_7 0x20u /* cs: none - (1 << 5), HelperEnum.cs:201 */
#define UCR_FLAGS_UCR_WORD_LENGTH_6 0x40u /* cs: none - (1 << 6), HelperEnum.cs:202 */
#define UCR_FLAGS_UCR_WORD_LENGTH_5 0x60u /* cs: none - (1 << 6) | (1 << 5), HelperEnum.cs:203 */
#define UCR_FLAGS_UCR_WORD_LENGTH_MASK 0x60u /* cs: none - (1 << 6) | (1 << 5), HelperEnum.cs:204 */
#define UCR_FLAGS_UCR_START_STOP_0_0 0x00u /* cs: Emulated.HW/Motorola/MFP/MC68901/HelperEnum.cs:205 */
#define UCR_FLAGS_UCR_START_STOP_1_1 0x08u /* cs: none - (1 << 3), HelperEnum.cs:206 */
#define UCR_FLAGS_UCR_START_STOP_1_15 0x10u /* cs: none - (1 << 4), HelperEnum.cs:207 */
#define UCR_FLAGS_UCR_START_STOP_1_2 0x18u /* cs: none - (1 << 4) | (1 << 3), HelperEnum.cs:208 */
#define UCR_FLAGS_UCR_CLOCK_DIVIDE_16 0x80u /* cs: none - (1 << 7), HelperEnum.cs:209 */
#define UCR_FLAGS_UCR_CLOCK_DIVIDE_1 0x00u /* cs: Emulated.HW/Motorola/MFP/MC68901/HelperEnum.cs:210 */

/** RSR_FLAGS (MATCH and CHAR_IN_PROGRESS share bit 2; FOUND_SEARCH and BREAK
 * share bit 3 - as in the C#) */
#define RSR_FLAGS_RSR_RCV_ENABLE 0x01u /* cs: none - (1 << 0), HelperEnum.cs:216 */
#define RSR_FLAGS_RSR_SYNC_STRIP_ENABLE 0x02u /* cs: none - (1 << 1), HelperEnum.cs:217 */
#define RSR_FLAGS_RSR_MATCH 0x04u /* cs: none - (1 << 2), HelperEnum.cs:218 */
#define RSR_FLAGS_RSR_CHAR_IN_PROGRESS 0x04u /* cs: none - (1 << 2), HelperEnum.cs:219 */
#define RSR_FLAGS_RSR_FOUND_SEARCH 0x08u /* cs: none - (1 << 3), HelperEnum.cs:220 */
#define RSR_FLAGS_RSR_BREAK 0x08u /* cs: none - (1 << 3), HelperEnum.cs:221 */
#define RSR_FLAGS_RSR_FRAME_ERROR 0x10u /* cs: none - (1 << 4), HelperEnum.cs:222 */
#define RSR_FLAGS_RSR_PARITY_ERROR 0x20u /* cs: none - (1 << 5), HelperEnum.cs:223 */
#define RSR_FLAGS_RSR_OVERRUN_ERROR 0x40u /* cs: none - (1 << 6), HelperEnum.cs:224 */
#define RSR_FLAGS_RSR_BUFFER_FULL 0x80u /* cs: none - (1 << 7), HelperEnum.cs:225 */

/** TSR_FLAGS */
#define TSR_FLAGS_TSR_OUTPUT_HI_Z 0x00u /* cs: Emulated.HW/Motorola/MFP/MC68901/HelperEnum.cs:231 */
#define TSR_FLAGS_TSR_XMIT_ENABLE 0x01u /* cs: none - (1 << 0), HelperEnum.cs:232 */
#define TSR_FLAGS_TSR_OUTPUT_LOW 0x02u /* cs: none - (1 << 1), HelperEnum.cs:233 */
#define TSR_FLAGS_TSR_OUTPUT_HIGH 0x04u /* cs: none - (1 << 2), HelperEnum.cs:234 */
#define TSR_FLAGS_TSR_OUTPUT_LOOP 0x06u /* cs: none - (1 << 2) | (1 << 1), HelperEnum.cs:235 */
#define TSR_FLAGS_TSR_BREAK 0x08u /* cs: none - (1 << 3), HelperEnum.cs:236 */
#define TSR_FLAGS_TSR_END_OF_XMIT 0x10u /* cs: none - (1 << 4), HelperEnum.cs:237 */
#define TSR_FLAGS_TSR_AUTO_TURNAROUND 0x20u /* cs: none - (1 << 5), HelperEnum.cs:238 */
#define TSR_FLAGS_TSR_UNDERRUN_ERROR 0x40u /* cs: none - (1 << 6), HelperEnum.cs:239 */
#define TSR_FLAGS_TSR_BUFFER_EMPTY 0x80u /* cs: none - (1 << 7), HelperEnum.cs:240 */
#define TSR_FLAGS_TSR_OUTPUT_MASK 0x06u /* cs: none - OUTPUT_LOW | OUTPUT_HIGH, HelperEnum.cs:241 */

/** SERIAL_STATE / XMIT_STATE: declared by the C#, not used by its code. */
typedef enum
{
    SERIAL_STATE_SERIAL_START = 0, /* cs: Emulated.HW/Motorola/MFP/MC68901/HelperEnum.cs:246 */
    SERIAL_STATE_SERIAL_DATA,
    SERIAL_STATE_SERIAL_PARITY,
    SERIAL_STATE_SERIAL_STOP
} SERIAL_STATE;

typedef enum
{
    XMIT_STATE_XMIT_OFF = 0, /* cs: Emulated.HW/Motorola/MFP/MC68901/HelperEnum.cs:254 */
    XMIT_STATE_XMIT_STARTING,
    XMIT_STATE_XMIT_ON,
    XMIT_STATE_XMIT_BREAK,
    XMIT_STATE_XMIT_STOPPING
} XMIT_STATE;

/** Mapper.INT_MASK_GPIO / INT_MASK_TIMER / GPIO_TIMER */
extern const uint16_t g_mfp_int_mask_gpio[8];
extern const uint16_t g_mfp_int_mask_timer[4];
extern const uint8_t g_mfp_gpio_timer[2];

/* ---- MfpTimer.cs ----------------------------------------------------------- */

typedef struct
{
    uint8_t tcr; /* TCR_STATE */
    uint8_t tdr;
    uint8_t tmc;
    bool in_latch;
    bool out_latch;
    bool is_enabled;
    int prescaler_count;
    int current_divisor;
    MfpTimerName timer_name;
    /* events OnTimerOutput / OnTimerElapsed */
    void (*on_timer_output)(void *ctx, MfpTimerName name, bool state);
    void (*on_timer_elapsed)(void *ctx, MfpTimerName name);
    void *ctx;
} MfpTimer;

extern const int g_mfptimer_prescaler[8];

/** @brief MfpTimer constructor. @param t timer @param name A-D */
void mfptimer_create(MfpTimer *t, MfpTimerName name);
/** @brief SetEnable. @param t timer @param enable new state */
void mfptimer_set_enable(MfpTimer *t, bool enable);
/** @brief Tick: one prescaler clock. @param t timer */
void mfptimer_tick(MfpTimer *t);
/** @brief TickEventCount: one event-count edge. @param t timer */
void mfptimer_tick_event_count(MfpTimer *t);
/** @brief SetControlRegister. @param t timer @param value control value */
void mfptimer_set_control_register(MfpTimer *t, uint8_t value);
/** @brief SetDataRegister. @param t timer @param data new TDR */
void mfptimer_set_data_register(MfpTimer *t, uint8_t data);
/** @brief Clear. @param t timer */
void mfptimer_clear(MfpTimer *t);

/* ---- Usart.cs -------------------------------------------------------------- */

typedef struct
{
    uint8_t ucr; /* UCR_FLAGS */
    uint8_t tsr; /* TSR_FLAGS */
    uint8_t rsr; /* RSR_FLAGS */
    uint8_t transmit_buffer;
    uint8_t receive_buffer;
    uint8_t scr;
    bool scr_parity;
    uint16_t rframe;
    uint8_t rclk;
    uint8_t rbits;
    uint8_t si_scan;
    uint8_t next_rsr; /* RSR_FLAGS */
    bool rc;
    bool si;
    bool last_si;
    bool rparity;
    uint16_t osr;
    uint8_t tclk;
    uint8_t tbits;
    bool tc;
    bool so;
    bool tparity;
    bool underrun;
    /* events OnSerialOutput / OnTakeInterrupt */
    void (*on_serial_output)(void *ctx, bool state);
    void (*on_take_interrupt)(void *ctx, uint16_t vector);
    void *ctx;
} Usart;

/** @brief Usart constructor (the C# constructor is empty). @param u USART */
void usart_create(Usart *u);
/** @brief Clear. @param u USART */
void usart_clear(Usart *u);
/** @brief ReadUDR. @param u USART @return receive buffer */
uint8_t usart_read_udr(Usart *u);
/** @brief tx_buffer_empty: raise that interrupt. @param u USART */
void usart_tx_buffer_empty(Usart *u);
/** @brief tx_error. @param u USART */
void usart_tx_error(Usart *u);
/** @brief rx_buffer_full. @param u USART */
void usart_rx_buffer_full(Usart *u);
/** @brief rx_error. @param u USART */
void usart_rx_error(Usart *u);
/** @brief set_receiver_clock. @param u USART @param state RC pin */
void usart_set_receiver_clock(Usart *u, bool state);
/** @brief set_serial_input. @param u USART @param state SI pin */
void usart_set_serial_input(Usart *u, bool state);
/** @brief set_transmitter_clock. @param u USART @param state TC pin */
void usart_set_transmitter_clock(Usart *u, bool state);
/** @brief rx_sync_found (no caller in the C#: sync mode is unsupported). @param u USART */
void usart_rx_sync_found(Usart *u);
/** @brief rx_clock. @param u USART @param si serial input sample */
void usart_rx_clock(Usart *u, bool si);

/* ---- Registers.cs ---------------------------------------------------------- */

typedef struct
{
    uint8_t gpip;
    uint8_t aer;
    uint8_t ddr;
    uint16_t ier; /* InterruptVectorNumber bits; A = high byte, B = low byte */
    uint16_t ipr;
    uint16_t isr;
    uint16_t imr;
    uint8_t vr;
    uint8_t gpio_input;
    uint8_t gpio_output;
    MfpTimer timer_a;
    MfpTimer timer_b;
    MfpTimer timer_c;
    MfpTimer timer_d;
    MfpTimer *timers[4];
    Usart usart;
    /* events OnTimerOutput / OnTimerElapsed / OnTakeInterrupt */
    void (*on_timer_output)(void *ctx, MfpTimerName name, bool state);
    void (*on_timer_elapsed)(void *ctx, MfpTimerName name);
    void (*on_take_interrupt)(void *ctx, uint16_t vector);
    void *ctx;
} Registers;

/** @brief Registers constructor (creates the timers, clears). @param r registers */
void mfpregs_create(Registers *r);
/** @brief Tick: tick all four timers. @param r registers */
void mfpregs_tick(Registers *r);
/** @brief Clear. @param r registers */
void mfpregs_clear(Registers *r);
uint8_t mfpregs_get_ier_a(const Registers *r);
void mfpregs_set_ier_a(Registers *r, uint8_t v);
uint8_t mfpregs_get_ier_b(const Registers *r);
void mfpregs_set_ier_b(Registers *r, uint8_t v);
uint8_t mfpregs_get_ipr_a(const Registers *r);
void mfpregs_set_ipr_a(Registers *r, uint8_t v);
uint8_t mfpregs_get_ipr_b(const Registers *r);
void mfpregs_set_ipr_b(Registers *r, uint8_t v);
uint8_t mfpregs_get_isr_a(const Registers *r);
void mfpregs_set_isr_a(Registers *r, uint8_t v);
uint8_t mfpregs_get_isr_b(const Registers *r);
void mfpregs_set_isr_b(Registers *r, uint8_t v);
uint8_t mfpregs_get_imr_a(const Registers *r);
void mfpregs_set_imr_a(Registers *r, uint8_t v);
uint8_t mfpregs_get_imr_b(const Registers *r);
void mfpregs_set_imr_b(Registers *r, uint8_t v);

/* ---- MC68901MFP.cs --------------------------------------------------------- */

#define MFP_SPURIOUS_VECTOR 0x18u /* cs: Emulated.HW/Motorola/MFP/MC68901/MC68901MFP.cs:156 */

typedef struct MC68901MFP MC68901MFP;

/** Events of MC68901MFP; any may be NULL. */
typedef struct
{
    void *ctx;
    void (*on_serial_output)(void *ctx, bool state);
    void (*on_serial_byte_transmit)(void *ctx, uint8_t data);
    void (*on_gpio)(void *ctx, uint8_t data);
    void (*on_irq)(void *ctx, bool state);
    void (*on_timer_output)(void *ctx, MfpTimerName name, bool state);
    void (*on_timer_interrupt)(void *ctx, MfpTimerName name);
    void (*on_register_write)(void *ctx, MFPRegister reg, uint8_t value);
    void (*on_vectored_irq)(void *ctx, uint8_t vector); /* declared in C#, never raised */
    void (*on_ieo)(void *ctx, bool state);
    uint8_t (*daisy_chain_callback)(void *ctx); /* NULL = no next device */
} MfpEvents;

struct MC68901MFP
{
    Registers regs;
    uint32_t start_address; /* IOMemoryBase(start, length): End = start + length - 1 */
    uint32_t end_address;
    bool use_system_vector_mapping;
    bool cycle_exact_cpu;
    int timer_b_event_cycle_pos;
    int current_line_cycle;
    bool iei;
    bool last_irq_state;
    bool separate_timer_clock;
    MfpEvents ev;
};

/** @brief Constructor MC68901MFP(start_address, length). The C# constructor
 *         does not call Reset; registers are cleared by Registers(). */
void mfp_create(MC68901MFP *m, uint32_t start_address, uint32_t length, const MfpEvents *events);
/** @brief IOMemoryBase.IsMappedAddress (no mirror for the MFP). */
bool mfp_is_mapped_address(const MC68901MFP *m, uint32_t address);
/** @brief Reset. */
void mfp_reset(MC68901MFP *m);
/** @brief SetTimerClock: empty in the C# (TODO there). */
void mfp_set_timer_clock(MC68901MFP *m, int xtal_speed);
/** @brief Read a register byte. */
uint8_t mfp_read(MC68901MFP *m, uint32_t address);
/** @brief Write a register byte. */
void mfp_write(MC68901MFP *m, uint32_t address, uint8_t value);
/** @brief HandleInterruptAcknowledge = get_vector. */
uint8_t mfp_handle_interrupt_acknowledge(MC68901MFP *m);
/** @brief get_vector: vector for an IACK (0x18 spurious if nothing local). */
uint8_t mfp_get_vector(MC68901MFP *m);
/** @brief GetInterruptVector = get_vector. */
uint8_t mfp_get_interrupt_vector(MC68901MFP *m);
void mfp_trigger_timer_c_interrupt(MC68901MFP *m);
void mfp_trigger_usart_receive_interrupt(MC68901MFP *m);
void mfp_trigger_usart_receive_error(MC68901MFP *m);
void mfp_trigger_usart_transmit_interrupt(MC68901MFP *m);
void mfp_trigger_usart_transmit_error(MC68901MFP *m);
void mfp_trigger_software_interrupt(MC68901MFP *m, int channel, uint8_t vector_base);
void mfp_trigger_interrupt(MC68901MFP *m, int gpio_pin);
/** @brief GetSystemVectorName. @return static text, or buf filled with
 *         "Unknown vector N" (buf may be NULL: then "Unknown vector"). */
const char *mfp_get_system_vector_name(uint8_t vector, char *buf, size_t len);
void mfp_timer_input_a(MC68901MFP *m, bool new_state);
void mfp_timer_input_b(MC68901MFP *m, bool new_state);
/** @brief Clock: ticks the timers unless separate_timer_clock. */
void mfp_clock(MC68901MFP *m);
/** @brief ClockTimers: always ticks the timers. */
void mfp_clock_timers(MC68901MFP *m);
void mfp_set_serial_input(MC68901MFP *m, bool state);
void mfp_set_receiver_clock(MC68901MFP *m, bool state);
void mfp_set_transmitter_clock(MC68901MFP *m, bool state);
void mfp_receive_serial_byte(MC68901MFP *m, uint8_t data);
/** @brief GPIO_n setter (gpio_input). */
void mfp_gpio_input(MC68901MFP *m, int bit, bool state);
/** @brief GPIO_n getter (GetGpioPinStatus). */
bool mfp_get_gpio_pin_status(const MC68901MFP *m, int bit);
/** @brief IsBitCountOdd. */
bool mfp_is_bit_count_odd(uint8_t b);
/** @brief IEI pin setter (C# property with no side effect). */
void mfp_set_iei(MC68901MFP *m, bool iei);

#endif /* ETH_MFP_H */
