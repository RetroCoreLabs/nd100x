/*
 * dma_dcb.h - HDLC DMA descriptor control block (DCB): structure and accessors.
 *
 * nd100x - ND100 Virtual Machine
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * Copyright (c) 2025 Ronny Hansen
 *
 * This file is originated from the nd100x project and the RetroCore project
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program (in the main directory of the nd100em
 * distribution in the file COPYING); if not, see <http://www.gnu.org/licenses/>.
 */

#ifndef DMA_DCB_H
#define DMA_DCB_H

#include <stdint.h>
#include <stdbool.h>
#include "hdlc_constants.h"
#include "dma_enum.h"

/**
 * @brief DMA Control Block (DCB)
 *
 * In memory transmit and receive buffers
 * This structure represents a DMA Control Block used for HDLC data transfers.
 */
typedef struct
{
    /**
     * @brief Where in memory was this buffer description loaded from
     */
    uint32_t bufferAddress;

    /**
     * @brief For this Buffer description, what is the offset (in blocks) from the start list pointer
     */
    uint16_t offsetFromLP;

    /**
     * @brief KEY value containing flags and control information
     */
    uint16_t keyValue;

    /**
     * @brief Byte count - Number of information bytes
     */
    uint16_t byteCount;

    /**
     * @brief Data Address - Most significant part
     */
    uint16_t mostAddress;

    /**
     * @brief Data Address - Least significant part
     */
    uint16_t leastAddress;

    /**
     * @brief Displacement value
     */
    uint16_t displacement;

    /**
     * @brief List Pointer value
     */
    uint32_t listPointer;

    // Helper fields for DMA transfer

    /**
     * @brief Must be set to the Memory address we should read from
     */
    uint32_t dmaAddress;

    /**
     * @brief Number of bytes read during DMA transfer
     */
    int dmaBytesRead;

    /**
     * @brief Number of bytes written during DMA transfer
     */
    int dmaBytesWritten;

    /**
     * @brief Last DMA read from ND. Will be -1 if its not read
     */
    int dmaReadData;

} HdlcDCB;

// Function declarations

/**
 * @brief Initialize a HdlcDCB structure to default values
 * @param dcb Pointer to HdlcDCB structure to initialize
 */
void DCB_Init(HdlcDCB *dcb);

/**
 * @brief Clear a HdlcDCB structure (same as Init)
 * @param dcb Pointer to HdlcDCB structure to clear
 */
void DCB_Clear(HdlcDCB *dcb);

/**
 * @brief Get the Key flags from the KeyValue
 * @param dcb Pointer to HdlcDCB structure
 * @return Key flags masked with KEYFLAG_MASK_KEY
 */
KeyFlags DCB_GetKey(const HdlcDCB *dcb);

/**
 * @brief Check if HdlcDCB has Receiver Start of Message flag
 * @param dcb Pointer to HdlcDCB structure
 * @return true if RSOM flag is set, false otherwise
 */
bool DCB_HasRSOMFlag(const HdlcDCB *dcb);

/**
 * @brief Check if HdlcDCB has Receiver End of Message flag
 * @param dcb Pointer to HdlcDCB structure
 * @return true if REOM flag is set, false otherwise
 */
bool DCB_HasREOMFlag(const HdlcDCB *dcb);

/**
 * @brief Get the DataFlow Cost from KeyValue
 * @param dcb Pointer to HdlcDCB structure
 * @return DataFlow Cost value
 */
uint16_t DCB_GetDataFlowCost(const HdlcDCB *dcb);

/**
 * @brief Get the combined 24-bit data memory address
 * @param dcb Pointer to HdlcDCB structure
 * @return Combined address from mostAddress and leastAddress
 */
uint32_t DCB_GetDataMemoryAddress(const HdlcDCB *dcb);

/**
 * @brief Set the 24-bit data memory address
 * @param dcb Pointer to HdlcDCB structure
 * @param address 24-bit address to set
 */
void DCB_SetDataMemoryAddress(HdlcDCB *dcb, uint32_t address);

// Accessor functions

/**
 * @brief Set the memory address this buffer description was loaded from.
 * @param dcb Descriptor to modify.
 * @param address Memory address of the buffer.
 */
void DCB_SetBufferAddress(HdlcDCB *dcb, uint32_t address);

/**
 * @brief Get the memory address this buffer description was loaded from.
 * @param dcb Descriptor to read.
 * @return bufferAddress field, or 0 if dcb is NULL.
 */
uint32_t DCB_GetBufferAddress(const HdlcDCB *dcb);

/**
 * @brief Set the buffer's offset (in 4-word blocks) from the list pointer.
 * @param dcb Descriptor to modify.
 * @param offset Offset value to store.
 */
void DCB_SetOffsetFromLP(HdlcDCB *dcb, uint16_t offset);

/**
 * @brief Get the buffer's offset (in 4-word blocks) from the list pointer.
 * @param dcb Descriptor to read.
 * @return offsetFromLP field, or 0 if dcb is NULL.
 */
uint16_t DCB_GetOffsetFromLP(const HdlcDCB *dcb);

/**
 * @brief Set the raw KEY value (flags plus control information).
 * @param dcb Descriptor to modify.
 * @param keyValue Raw key value to store.
 */
void DCB_SetKeyValue(HdlcDCB *dcb, uint16_t keyValue);

/**
 * @brief Get the raw KEY value (flags plus control information).
 * @param dcb Descriptor to read.
 * @return keyValue field, or 0 if dcb is NULL.
 */
uint16_t DCB_GetKeyValue(const HdlcDCB *dcb);

/**
 * @brief Set the byte count (number of information bytes).
 * @param dcb Descriptor to modify.
 * @param byteCount Byte count to store.
 */
void DCB_SetByteCount(HdlcDCB *dcb, uint16_t byteCount);

/**
 * @brief Get the byte count (number of information bytes).
 * @param dcb Descriptor to read.
 * @return byteCount field, or 0 if dcb is NULL.
 */
uint16_t DCB_GetByteCount(const HdlcDCB *dcb);

/**
 * @brief Set the Displacement1/Displacement2 value applied to this buffer.
 * @param dcb Descriptor to modify.
 * @param displacement Displacement value, in bytes.
 */
void DCB_SetDisplacement(HdlcDCB *dcb, uint16_t displacement);

/**
 * @brief Get the Displacement1/Displacement2 value applied to this buffer.
 * @param dcb Descriptor to read.
 * @return displacement field, or 0 if dcb is NULL.
 */
uint16_t DCB_GetDisplacement(const HdlcDCB *dcb);

/**
 * @brief Set the list pointer value (start-of-list address this buffer belongs to).
 * @param dcb Descriptor to modify.
 * @param listPointer List pointer value to store.
 */
void DCB_SetListPointer(HdlcDCB *dcb, uint32_t listPointer);

/**
 * @brief Get the list pointer value (start-of-list address this buffer belongs to).
 * @param dcb Descriptor to read.
 * @return listPointer field, or 0 if dcb is NULL.
 */
uint32_t DCB_GetListPointer(const HdlcDCB *dcb);

// DMA helper functions

/**
 * @brief Set the memory address to use for the next DMA read/write.
 * @param dcb Descriptor to modify.
 * @param address Memory address to store.
 */
void DCB_SetDMAAddress(HdlcDCB *dcb, uint32_t address);

/**
 * @brief Get the memory address to use for the next DMA read/write.
 * @param dcb Descriptor to read.
 * @return dmaAddress field, or 0 if dcb is NULL.
 */
uint32_t DCB_GetDMAAddress(const HdlcDCB *dcb);

/**
 * @brief Set the count of bytes read so far during this buffer's DMA transfer.
 * @param dcb Descriptor to modify.
 * @param bytesRead Byte count to store.
 */
void DCB_SetDMABytesRead(HdlcDCB *dcb, int bytesRead);

/**
 * @brief Get the count of bytes read so far during this buffer's DMA transfer.
 * @param dcb Descriptor to read.
 * @return dmaBytesRead field, or 0 if dcb is NULL.
 */
int DCB_GetDMABytesRead(const HdlcDCB *dcb);

/**
 * @brief Set the count of bytes written so far during this buffer's DMA transfer.
 * @param dcb Descriptor to modify.
 * @param bytesWritten Byte count to store.
 */
void DCB_SetDMABytesWritten(HdlcDCB *dcb, int bytesWritten);

/**
 * @brief Get the count of bytes written so far during this buffer's DMA transfer.
 * @param dcb Descriptor to read.
 * @return dmaBytesWritten field, or 0 if dcb is NULL.
 */
int DCB_GetDMABytesWritten(const HdlcDCB *dcb);

/**
 * @brief Set the last 16-bit word read from ND memory for this buffer.
 * @param dcb Descriptor to modify.
 * @param data Word value to store, or -1 to mark as not read.
 */
void DCB_SetDMAReadData(HdlcDCB *dcb, int data);

/**
 * @brief Get the last 16-bit word read from ND memory for this buffer.
 * @param dcb Descriptor to read.
 * @return dmaReadData field, or -1 if dcb is NULL or the data was not read.
 */
int DCB_GetDMAReadData(const HdlcDCB *dcb);

/**
 * @brief Check whether a DMA read word is cached (dmaReadData != -1).
 * @param dcb Descriptor to read.
 * @return true if a read word is cached, false otherwise (including on NULL dcb).
 */
bool DCB_IsDMAReadDataValid(const HdlcDCB *dcb);

/**
 * @brief Reset the cached DMA read word to -1 (not read).
 * @param dcb Descriptor to modify.
 */
void DCB_ClearDMAReadData(HdlcDCB *dcb);

#endif // DMA_DCB_H
