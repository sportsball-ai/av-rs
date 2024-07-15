/*****************************************************************************
 * This file is part of Kvazaar HEVC encoder.
 *
 * Copyright (c) 2021, Tampere University, ITU/ISO/IEC, project contributors
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without modification,
 * are permitted provided that the following conditions are met:
 *
 * * Redistributions of source code must retain the above copyright notice, this
 *   list of conditions and the following disclaimer.
 *
 * * Redistributions in binary form must reproduce the above copyright notice, this
 *   list of conditions and the following disclaimer in the documentation and/or
 *   other materials provided with the distribution.
 *
 * * Neither the name of the Tampere University or ITU/ISO/IEC nor the names of its
 *   contributors may be used to endorse or promote products derived from
 *   this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR
 * ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON
 * ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 ****************************************************************************/

/*!*****************************************************************************
 *  \file   ni_bitstream.c
 *
 *  \brief  Utility definitions to operate on bits in a bitstream
 ******************************************************************************/

#include <stdio.h>
#include <string.h>
#include <assert.h>
#include "ni_util.h"
#include "ni_bitstream.h"

// the following is for bitstream put operations

const uint32_t ni_bit_set_mask[] = {
    0x00000001, 0x00000002, 0x00000004, 0x00000008, 0x00000010, 0x00000020,
    0x00000040, 0x00000080, 0x00000100, 0x00000200, 0x00000400, 0x00000800,
    0x00001000, 0x00002000, 0x00004000, 0x00008000, 0x00010000, 0x00020000,
    0x00040000, 0x00080000, 0x00100000, 0x00200000, 0x00400000, 0x00800000,
    0x01000000, 0x02000000, 0x04000000, 0x08000000, 0x10000000, 0x20000000,
    0x40000000, 0x80000000};

/*!*****************************************************************************
 * \brief allocate a new bitstream data chunk
 *
 * \return pointer to the new chunk, or NULL.
 ******************************************************************************/
static ni_data_chunk_t *ni_bs_writer_alloc_chunk(void)
{
    ni_data_chunk_t *chunk = malloc(sizeof(ni_data_chunk_t));
    if (chunk)
    {
        chunk->len = 0;
        chunk->next = NULL;
    }
    return chunk;
}

/*!*****************************************************************************
 * \brief write a byte to bitstream
 * Note:  the stream must be byte-aligned already
 *
 * \param stream  bitstream
 * \param byte    byte to write
 * \return        none
 ******************************************************************************/
static void ni_bs_writer_write_byte(ni_bitstream_writer_t *stream, uint8_t byte)
{
    assert(stream->cur_bit == 0);

    if (stream->last == NULL || stream->last->len == NI_DATA_CHUNK_SIZE)
    {
        // need to allocate a new chunk.
        ni_data_chunk_t *new_chunk = ni_bs_writer_alloc_chunk();
        if (!new_chunk)
        {
            ni_log(NI_LOG_ERROR, "%s error: no memory\n", __func__);
            return;
        }

        if (!stream->first)
            stream->first = new_chunk;
        if (stream->last)
            stream->last->next = new_chunk;
        stream->last = new_chunk;
    }
    if (stream->last->len >= NI_DATA_CHUNK_SIZE)
    {
        ni_log(NI_LOG_ERROR, "%s error: new_chunk size >= max %d\n", __func__,
               NI_DATA_CHUNK_SIZE);
        return;
    }

    stream->last->data[stream->last->len] = byte;
    stream->last->len += 1;
    stream->len += 1;
}

/*!*****************************************************************************
 * \brief free a list of chunks
 *
 * \param chunk  start of the chunk list
 * \return       none
 ******************************************************************************/
static void ni_bs_writer_free_chunks(ni_data_chunk_t *chunk)
{
    while (chunk != NULL)
    {
        ni_data_chunk_t *next = chunk->next;
        free(chunk);
        chunk = next;
    }
}

static inline unsigned ni_math_floor_log2(unsigned value)
{
    unsigned result = 0;
    assert(value > 0);
    int i;

    for (i = 4; i >= 0; --i)
    {
        unsigned bits = 1ull << i;
        unsigned shift = (value >= (1ull << bits)) ? bits : 0;
        result += shift;
        value >>= shift;
    }

    return result;
}

static inline unsigned ni_math_ceil_log2(unsigned value)
{
    assert(value > 0);

    // the ceil_log2 is just floor_log2 + 1, except for exact powers of 2.
    return ni_math_floor_log2(value) + ((value & (value - 1)) ? 1 : 0);
}

/*!*****************************************************************************
 * \brief init a bitstream writer
 *
 * \param stream  bitstream
 * \return        none
 ******************************************************************************/
void ni_bitstream_writer_init(ni_bitstream_writer_t *stream)
{
    memset(stream, 0, sizeof(ni_bitstream_writer_t));
}

/*!*****************************************************************************
 * \brief return the number of bits written to bitstream so far
 *
 * \param stream  bitstream
 * \return        position
 ******************************************************************************/
uint64_t ni_bs_writer_tell(const ni_bitstream_writer_t *const stream)
{
    uint64_t position = stream->len;
    return position * 8 + stream->cur_bit;
}

/*!*****************************************************************************
 * \brief write a specified number (<= 32) of bits to bitstream,
 *        buffer individual bits until a full byte is made
 * \param stream  bitstream
 * \param data    input data
 * \param bits    number of bits in data to write to stream, max 32
 * \return        none
 ******************************************************************************/
void ni_bs_writer_put(ni_bitstream_writer_t *stream, uint32_t data,
                      uint8_t bits)
{
    if (bits > 32)
    {
        ni_log(NI_LOG_ERROR, "%s error: too many bits to write: %u\n", __func__,
               bits);
        return;
    }

    while (bits--)
    {
        stream->data <<= 1;

        if (data & ni_bit_set_mask[bits])
        {
            stream->data |= 1;
        }
        stream->cur_bit++;

        // write the complete byte
        if (stream->cur_bit == 8)
        {
            stream->cur_bit = 0;
            ni_bs_writer_write_byte(stream, stream->data);
        }
    }
}

/*!*****************************************************************************
 * \brief write unsigned Exp-Golomb bit string to bitstream, 2^32-2 at most.
 *
 * \param stream  bitstream
 * \param data    input data
 * \return        none
 ******************************************************************************/
void ni_bs_writer_put_ue(ni_bitstream_writer_t *stream, uint32_t data)
{
    unsigned data_log2 = ni_math_floor_log2(data + 1);
    unsigned prefix = 1 << data_log2;
    unsigned suffix = data + 1 - prefix;
    unsigned num_bits = data_log2 * 2 + 1;
    unsigned value = prefix | suffix;

    if (data > 0xFFFFFFFE) // 2^32-2 at most
    {
        ni_log(NI_LOG_ERROR, "%s error: data overflow: %u\n", __func__,
               data);
        return;
    }

    if (num_bits <= 32)
    {
      ni_bs_writer_put(stream, value, num_bits);
    }
    else
    {
      // big endian
      ni_bs_writer_put(stream, 0, num_bits - 32); // high (num_bits - 32) bits
      ni_bs_writer_put(stream, value, 32); // low 32 bits
    }
}

/*!*****************************************************************************
 * \brief write signed Exp-Golomb bit string to bitstream
 *
 * \param stream  bitstream
 * \param data    input data
 * \return        none
 ******************************************************************************/
void ni_bs_writer_put_se(ni_bitstream_writer_t *stream, int32_t data)
{
    // map positive value to even and negative to odd value
    uint32_t data_num = data <= 0 ? (-data) << 1 : (data << 1) - 1;
    ni_bs_writer_put_ue(stream, data_num);
}

/*!*****************************************************************************
 * \brief align the bitstream with zero
 *
 * \param stream  bitstream
 * \return        none
 ******************************************************************************/
void ni_bs_writer_align_zero(ni_bitstream_writer_t *stream)
{
    if ((stream->cur_bit & 7) != 0)
    {
        ni_bs_writer_put(stream, 0, 8 - (stream->cur_bit & 7));
    }
}

/*!*****************************************************************************
 * \brief copy bitstream data to dst
 * Note: caller must ensure sufficient space in dst
 *
 * \param dst     copy destination
 * \param stream  bitstream
 * \return        none
 ******************************************************************************/
void ni_bs_writer_copy(uint8_t *dst, const ni_bitstream_writer_t *stream)
{
    ni_data_chunk_t *chunk = stream->first;
    uint8_t *p_dst = dst;
    while (chunk && chunk->len)
    {
        memcpy(p_dst, chunk->data, chunk->len);
        p_dst += chunk->len;
        chunk = chunk->next;
    }
}

/*!*****************************************************************************
 * \brief clear and reset bitstream
 *
 * \param stream  bitstream
 * \return        none
 ******************************************************************************/
void ni_bs_writer_clear(ni_bitstream_writer_t *stream)
{
    ni_bs_writer_free_chunks(stream->first);
    ni_bitstream_writer_init(stream);
}

// the following is for bitstream get operations

/*!*****************************************************************************
 * \brief init a bitstream reader
 * Note: bitstream_reader takes reading ownership of the data
 *
 * \param br        bitstream reader
 * \param data      data to be parsed
 * \param bit_size  number of bits in the data
 * \return        none
 ******************************************************************************/
void ni_bitstream_reader_init(ni_bitstream_reader_t *br, const uint8_t *data,
                              int bit_size)
{
    if (!br || !data)
    {
        ni_log(NI_LOG_ERROR, "%s input is NULL !\n", __func__);
        return;
    }

    br->buf = data;
    br->size_in_bits = bit_size;
    br->byte_offset = 0;
    br->bit_offset = 0;
}

/*!*****************************************************************************
 * \brief  return the number of bits already parsed in stream
 *
 * \param br  bitstream reader
 * \return    number of bits parsed
 ******************************************************************************/
int ni_bs_reader_bits_count(ni_bitstream_reader_t *br)
{
    return br->byte_offset * 8 + br->bit_offset;
}

/*!*****************************************************************************
 * \brief  return the number of bits left to parse in stream
 *
 * \param br  bitstream reader
 * \return    number of bits left
 ******************************************************************************/
int ni_bs_reader_get_bits_left(ni_bitstream_reader_t *br)
{
    return br->size_in_bits - ni_bs_reader_bits_count(br);
}

/*!*****************************************************************************
 * \brief   skip a number of bits ahead in the bitstream reader
 *
 * \param br  bitstream reader
 * \param n   number of bits to skip
 * \return    none
 ******************************************************************************/
void ni_bs_reader_skip_bits(ni_bitstream_reader_t *br, int n)
{
    int new_offset = 8 * br->byte_offset + br->bit_offset + n;
    if (new_offset > br->size_in_bits)
    {
        ni_log(NI_LOG_DEBUG,
               "%s: skip %d, current byte_offset "
               "%d bit_offset %d, over total size %d, stop !\n",
               __func__, n, br->byte_offset, br->bit_offset, br->size_in_bits);
        return;
    }

    br->byte_offset = new_offset / 8;
    br->bit_offset = new_offset % 8;
}

// read a single bit
uint8_t ni_bitstream_get_1bit(ni_bitstream_reader_t *br)
{
    uint8_t ret = 0;

    if (0 == br->bit_offset)
    {
        ret = (br->buf[br->byte_offset] >> 7);
        br->bit_offset = 1;
    } else
    {
        ret = ((br->buf[br->byte_offset] >> (7 - br->bit_offset)) & 0x1);
        if (7 == br->bit_offset)
        {
            br->bit_offset = 0;
            br->byte_offset++;
        } else
        {
            br->bit_offset++;
        }
    }

    return ret;
}

// read a single byte
uint8_t ni_bitstream_get_u8(ni_bitstream_reader_t *br)
{
    uint8_t ret = 0;

    ret = (br->buf[br->byte_offset] << br->bit_offset);
    br->byte_offset++;

    if (0 != br->bit_offset)
    {
        ret |= (br->buf[br->byte_offset] >> (8 - br->bit_offset));
    }

    return ret;
}

// read a 16 bit integer
uint16_t ni_bitstream_get_u16(ni_bitstream_reader_t *br)
{
    uint16_t ret = 0;
    int i;
    int offset;
    const uint8_t *src = NULL;

    src = &(br->buf[br->byte_offset]);
    offset = 16 + br->bit_offset;

    for (i = 0; i < 2; i++)
    {
        offset -= 8;
        ret |= ((uint16_t)src[i] << offset);
    }

    if (0 != offset)
    {
        ret |= (src[2] >> (8 - offset));
    }

    br->byte_offset += 2;

    return ret;
}

// read <= 8 bits
uint8_t ni_bitstream_get_8bits_or_less(ni_bitstream_reader_t *br, int n)
{
    uint8_t ret = 0;

    if (n > 8)
    {
        ni_log(NI_LOG_ERROR, "%s %d bits > 8, error!\n", __func__, n);
        return 0;
    }

    while (n)
    {
        ret = ((ret << 1) | ni_bitstream_get_1bit(br));
        n--;
    }
    return ret;
}

/*!*****************************************************************************
 * \brief  read bits (up to 32) from the bitstream reader, after reader init
 *
 * \param br  bitstream reader
 * \param n   number of bits to read
 * \return    value read
 ******************************************************************************/
uint32_t ni_bs_reader_get_bits(ni_bitstream_reader_t *br, int n)
{
    uint32_t ret = 0;
    int bits_left;

    if (n > 32)
    {
        ni_log(NI_LOG_ERROR, "%s %d bits > 32, not supported!\n", __func__, n);
        return 0;
    }

    if (n <= 0)
    {
        // return 0
    } else if (n < 8)
    {
        ret = ni_bitstream_get_8bits_or_less(br, n);
    } else if (8 == n)
    {
        ret = ni_bitstream_get_u8(br);
    } else if (n > 8 && n < 16)
    {
        bits_left = n % 8;
        ret = ((ni_bitstream_get_8bits_or_less(br, bits_left) << 8) |
               ni_bitstream_get_u8(br));
    } else if (16 == n)
    {
        ret = ni_bitstream_get_u16(br);
    } else if (n > 16 && n < 24)
    {
        bits_left = n % 16;
        ret = (ni_bitstream_get_8bits_or_less(br, bits_left) << 16);
        ret |= (ni_bitstream_get_u8(br) << 8);
        ret |= ni_bitstream_get_u8(br);
    } else   // 32 >= n >= 24
    {
        bits_left = n % 24;
        ret = (ni_bitstream_get_8bits_or_less(br, bits_left) << 24);
        ret |= (ni_bitstream_get_u8(br) << 16);
        ret |= (ni_bitstream_get_u8(br) << 8);
        ret |= ni_bitstream_get_u8(br);
    }
    return ret;
}

/*!*****************************************************************************
 * \brief   read an unsigned Exp-Golomb code ue(v)
 *
 * \param br  bitstream reader
 * \return    value read
 ******************************************************************************/
uint32_t ni_bs_reader_get_ue(ni_bitstream_reader_t *br)
{
    uint32_t ret = 0;
    int i = 0;   // leading zero bits

    // count leading zero bits
    while (0 == ni_bitstream_get_1bit(br) && i < 32)
    {
        i++;
    }
    if (i == 32)
        return 0;
    // calc get_bits(leading zero bits)
    ret = ni_bs_reader_get_bits(br, i);
    ret += (1U << i) - 1;
    return ret;
}

/*!*****************************************************************************
 * \brief    read a signed Exp-Golomb code se(v)
 *
 * \param br  bitstream reader
 * \return    value read
 ******************************************************************************/
int32_t ni_bs_reader_get_se(ni_bitstream_reader_t *br)
{
    // get ue
    int32_t ret = ni_bs_reader_get_ue(br);

    // determine if it's odd or even
    if (ret & 0x01)   // odd: value before encode > 0
    {
        ret = (ret + 1) / 2;
    } else   // even: value before encode <= 0
    {
        ret = -(ret / 2);
    }
    return ret;
}
