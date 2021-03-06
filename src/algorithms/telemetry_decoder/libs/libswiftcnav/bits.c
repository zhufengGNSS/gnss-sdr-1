/*!
 * \file bits.c
 * \author Fergus Noble <fergus@swift-nav.com>
 *
 * -------------------------------------------------------------------------
 * This file was originally borrowed from libswiftnav
 * <https://github.com/swift-nav/libswiftnav>,
 * a portable C library implementing GNSS related functions and algorithms,
 * and then modified by J. Arribas and C. Fernandez
 *
 * Copyright (C) 2013, 2016 Swift Navigation Inc.
 * Contact: Fergus Noble <fergus@swift-nav.com>
 *
 * GNSS-SDR is a software defined Global Navigation
 *          Satellite Systems receiver
 *
 * This file is part of GNSS-SDR.
 *
 * This file is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as
 * published by the Free Software Foundation, version 3.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * Lesser General Lesser Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program. If not, see <https://www.gnu.org/licenses/>.
 */

#include "bits.h"
#include <limits.h>
#include <string.h>


static const uint8_t BITN[16] = {0, 1, 1, 2, 1, 2, 2, 3, 1, 2, 2, 3, 2, 3, 3, 4};

/** \defgroup bits Bit Utils
 * Bit field packing, unpacking and utility functions.
 * \{ */

/** Computes the parity of a 32-bit word.
 *
 * References:
 *   -# https://graphics.stanford.edu/~seander/bithacks.html#ParityParallel
 *
 * \param x Word for which to compute parity
 * \return 1 if there are an odd number of bits set.
 *         0 if there are an even number of bits set.
 */
uint8_t parity(uint32_t x)
{
    x ^= x >> 16;
    x ^= x >> 8;
    x ^= x >> 4;
    x &= 0xF;
    return (0x6996 >> x) & 1;
}


/** Get bit field from buffer as an unsigned integer.
 * Unpacks `len` bits at bit position `pos` from the start of the buffer.
 * Maximum bit field length is 32 bits, i.e. `len <= 32`.
 *
 * \param buff
 * \param pos Position in buffer of start of bit field in bits.
 * \param len Length of bit field in bits.
 * \return Bit field as an unsigned value.
 */
uint32_t getbitu(const uint8_t *buff, uint32_t pos, uint8_t len)
{
    uint32_t bits = 0;
    uint32_t i = 0;
    for (i = pos; i < pos + len; i++)
        {
            bits = (bits << 1) +
                   ((buff[i / 8] >> (7 - i % 8)) & 1u);
        }

    return bits;
}

/** Get bit field from buffer as a signed integer.
 * Unpacks `len` bits at bit position `pos` from the start of the buffer.
 * Maximum bit field length is 32 bits, i.e. `len <= 32`.
 *
 * This function sign extends the `len` bit field to a signed 32 bit integer.
 *
 * \param buff
 * \param pos Position in buffer of start of bit field in bits.
 * \param len Length of bit field in bits.
 * \return Bit field as a signed value.
 */
int32_t getbits(const uint8_t *buff, uint32_t pos, uint8_t len)
{
    int32_t bits = (int32_t)getbitu(buff, pos, len);

    /* Sign extend, taken from:
     * http://graphics.stanford.edu/~seander/bithacks.html#VariableSignExtend
     */
    int32_t m = 1u << (len - 1);
    return (bits ^ m) - m;
}

/** Set bit field in buffer from an unsigned integer.
 * Packs `len` bits into bit position `pos` from the start of the buffer.
 * Maximum bit field length is 32 bits, i.e. `len <= 32`.
 *
 * \param buff
 * \param pos Position in buffer of start of bit field in bits.
 * \param len Length of bit field in bits.
 * \param data Unsigned integer to be packed into bit field.
 */
void setbitu(uint8_t *buff, uint32_t pos, uint32_t len, uint32_t data)
{
    uint32_t mask = 1u << (len - 1);

    if (len <= 0 || 32 < len)
        {
            return;
        }
    uint32_t i = 0;
    for (i = pos; i < pos + len; i++, mask >>= 1)
        {
            if (data & mask)
                {
                    buff[i / 8] |= 1u << (7 - i % 8);
                }
            else
                {
                    buff[i / 8] &= ~(1u << (7 - i % 8));
                }
        }
}

/** Set bit field in buffer from a signed integer.
 * Packs `len` bits into bit position `pos` from the start of the buffer.
 * Maximum bit field length is 32 bits, i.e. `len <= 32`.
 *
 * \param buff
 * \param pos Position in buffer of start of bit field in bits.
 * \param len Length of bit field in bits.
 * \param data Signed integer to be packed into bit field.
 */
void setbits(uint8_t *buff, uint32_t pos, uint32_t len, int32_t data)
{
    setbitu(buff, pos, len, (uint32_t)data);
}

/**
 * Shift MSB bit buffer contents to the left.
 * The method performs in-place shift operation.
 *
 * \param[in,out] buf   Pointer to buffer head.
 * \param[in]     size  Number of bytes in the buffer.
 * \param[in]     shift Number of bits for left shift operation.
 *
 * \return None
 */
void bitshl(void *buf, uint32_t size, uint32_t shift)
{
    if (shift > size * CHAR_BIT)
        {
            /* Quick check: if the shift is larger, than the buffer, zero the data */
            memset(buf, 0, size);
            return;
        }

    unsigned char *dst = buf;                          /* Destination byte. */
    const unsigned char *src = dst + shift / CHAR_BIT; /* First source byte, possibly incomplete. */

    uint32_t copy_bits = size * CHAR_BIT - shift; /* Number of bits to move */
    uint32_t byte_shift = copy_bits % CHAR_BIT;   /* Shift of data */
    uint32_t full_bytes = copy_bits / CHAR_BIT;   /* Number of bytes to move */

    if (0 == byte_shift)
        {
            /* When moving data in character boundaries, use built-in functions: move
             * data, and then zero the tail. */
            memmove(dst, src, full_bytes);
            memset(dst + full_bytes, 0, size - full_bytes);
        }
    else
        {
            /* Create an accumulator: it will hold a value of two consecutive bytes */
            uint32_t acc = *src++;
            uint32_t i = 0;
            for (i = 0; i < full_bytes; ++i)
                {
                    acc = (acc << CHAR_BIT) | *src++;
                    *dst++ = acc >> byte_shift;
                }
            *dst++ = acc << CHAR_BIT >> byte_shift;
            if (full_bytes + 1 < size)
                {
                    memset(dst, 0, size - full_bytes - 1);
                }
        }
}

/**
 * Performs block bit copy operation.
 *
 * The function copies given number of bits from the source to destination.
 *
 * \param[in,out] dst       Pointer to destination buffer.
 * \param[in]     dst_index Destination bit index.
 * \param[in]     src       Source buffer.
 * \param[in]     src_index Source bit index.
 * \param[in]     count     Number of bits to copy.
 *
 * \return None
 *
 * \todo This function can be optimized for copying aligned data and using
 *       proper native type like long.
 */
void bitcopy(void *dst, uint32_t dst_index, const void *src, uint32_t src_index,
    uint32_t count)
{
    uint32_t limit1 = count / 32;
    uint32_t limit2 = count % 32;
    uint32_t idx = 0;
    for (idx = 0; idx < limit1; ++idx)
        {
            uint32_t tmp = getbitu(src, src_index, 32);
            setbitu(dst, dst_index, 32, tmp);
            src_index += 32;
            dst_index += 32;
        }
    if (0 != limit2)
        {
            uint32_t tmp = getbitu(src, src_index, limit2);
            setbitu(dst, dst_index, limit2, tmp);
        }
}

/**
 * Count number of bits set to certain value in 64 bits word
 *
 * \param[in]     v      input 64 bits word to count bits in
 * \param[in]     bv     1 or 0 - which value to count
 *
 * \return        Number of bits set to one or zero.
 */
uint8_t count_bits_u64(uint64_t v, uint8_t bv)
{
    uint8_t r = 0;
    int i = 0;
    for (i = 0; i < 16; i++)
        {
            r += BITN[(v >> (i * 4)) & 0xf];
        }
    return bv == 1 ? r : 64 - r;
}

/**
 * Count number of bits set to certain value in 32 bits word
 *
 * \param[in]     v      input 32 bits word to count bits in
 * \param[in]     bv     1 or 0 - which value to count
 *
 * \return        Number of bits set to one or zero.
 */
uint8_t count_bits_u32(uint32_t v, uint8_t bv)
{
    uint8_t r = 0;
    int i = 0;
    for (i = 0; i < 8; i++)
        {
            r += BITN[(v >> (i * 4)) & 0xf];
        }
    return bv == 1 ? r : 32 - r;
}

/**
 * Count number of bits set to certain value in 16 bits word
 *
 * \param[in]     v      input 16 bits word to count bits in
 * \param[in]     bv     1 or 0 - which value to count
 *
 * \return        Number of bits set to one or zero.
 */
uint8_t count_bits_u16(uint16_t v, uint8_t bv)
{
    uint8_t r = 0;
    int i = 0;
    for (i = 0; i < 4; i++)
        {
            r += BITN[(v >> (i * 4)) & 0xf];
        }
    return bv == 1 ? r : 16 - r;
}

/**
 * Count number of bits set to certain value in 8 bits word
 *
 * \param[in]     v      input 8 bits word to count bits in
 * \param[in]     bv     1 or 0 - which value to count
 *
 * \return        Number of bits set to one or zero.
 */
uint8_t count_bits_u8(uint8_t v, uint8_t bv)
{
    uint8_t r = 0;
    int i = 0;
    for (i = 0; i < 2; i++)
        {
            r += BITN[(v >> (i * 4)) & 0xf];
        }
    return bv == 1 ? r : 8 - r;
}

/** \} */
