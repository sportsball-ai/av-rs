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
*  \file   ni_bitstream.h
*
*  \brief  Utility functions to operate on bits in a bitstream
*
*******************************************************************************/

#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#ifdef _WIN32
  #ifdef XCODER_DLL
    #ifdef LIB_EXPORTS
      #define LIB_API_BITSTREAM __declspec(dllexport)
    #else
      #define LIB_API_BITSTREAM __declspec(dllimport)
    #endif
  #else
    #define LIB_API_BITSTREAM
  #endif
#elif __linux__ || __APPLE__
  #define LIB_API_BITSTREAM
#endif

// the following is for bitstream put operations
#define NI_DATA_CHUNK_SIZE 4096

typedef struct ni_data_chunk_t
{
    // buffer for the data
    uint8_t data[NI_DATA_CHUNK_SIZE];

    // number of bytes filled in this chunk
    uint32_t len;

    // next chunk in the list
    struct ni_data_chunk_t *next;
} ni_data_chunk_t;

// bitstream writer and operations
typedef struct _ni_bitstream_writer_t
{
    // total number of complete bytes
    uint32_t len;

    // pointer to the first chunk of data, or NULL
    ni_data_chunk_t *first;

    // pointer to the last chunk of data, or NULL
    ni_data_chunk_t *last;

    // the incomplete byte
    uint8_t data;

    // number of bits in the incomplete byte
    uint8_t cur_bit;
} ni_bitstream_writer_t;

// bitstream writer init
void ni_bitstream_writer_init(ni_bitstream_writer_t *stream);

// get the number of bits written to bitstream so far
uint64_t ni_bs_writer_tell(const ni_bitstream_writer_t *const stream);

// write a specified number (<= 32) of bits to bitstream
void ni_bs_writer_put(ni_bitstream_writer_t *stream, uint32_t data,
                      uint8_t bits);

// write unsigned/signed Exp-Golomb bit string to bitstream
void ni_bs_writer_put_ue(ni_bitstream_writer_t *stream, uint32_t data);
void ni_bs_writer_put_se(ni_bitstream_writer_t *stream, int32_t data);

// align the bitstream with zero
void ni_bs_writer_align_zero(ni_bitstream_writer_t *stream);

// copy bitstream data to dst
void ni_bs_writer_copy(uint8_t *dst, const ni_bitstream_writer_t *stream);

// clear and reset bitstream
void ni_bs_writer_clear(ni_bitstream_writer_t *stream);

// the following is for bitstream get operations

// bitstream reader and operations
typedef struct _ni_bitstream_reader_t
{
    const uint8_t *buf;   // data
    int byte_offset;      // byte offset of the current read position
    int bit_offset;       // bit offset of the current read position
    int size_in_bits;     // number of total bits in data
} ni_bitstream_reader_t;

// bitstream reader init
LIB_API_BITSTREAM void ni_bitstream_reader_init(ni_bitstream_reader_t *br, const uint8_t *data,
                              int bit_size);

// return number of bits already parsed
LIB_API_BITSTREAM int ni_bs_reader_bits_count(ni_bitstream_reader_t *br);

// return number of bits left to parse
LIB_API_BITSTREAM int ni_bs_reader_get_bits_left(ni_bitstream_reader_t *br);

// skip a number of bits ahead in the bitstream reader
LIB_API_BITSTREAM void ni_bs_reader_skip_bits(ni_bitstream_reader_t *br, int n);

// read bits (up to 32) from the bitstream reader, after reader init
LIB_API_BITSTREAM uint32_t ni_bs_reader_get_bits(ni_bitstream_reader_t *br, int n);

// read an unsigned Exp-Golomb code ue(v)
LIB_API_BITSTREAM uint32_t ni_bs_reader_get_ue(ni_bitstream_reader_t *br);

// read a signed Exp-Golomb code se(v)
LIB_API_BITSTREAM int32_t ni_bs_reader_get_se(ni_bitstream_reader_t *br);

#ifdef __cplusplus
}
#endif
