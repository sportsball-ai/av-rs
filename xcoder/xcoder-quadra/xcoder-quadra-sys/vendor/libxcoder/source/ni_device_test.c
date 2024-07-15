/*******************************************************************************
 *
 * Copyright (C) 2022 NETINT Technologies
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 *
 ******************************************************************************/

/*!*****************************************************************************
 *  \file   ni_device_test.c
 *
 *  \brief  Application for performing video processing with libxcoder API.
 *          Its code provides examples on how to programatically use libxcoder
 *          API.
 ******************************************************************************/

#ifdef _WIN32
#include <io.h>
#include "ni_getopt.h"
#elif __linux__ || __APPLE__
#define _POSIX_C_SOURCE 200809L
#include <getopt.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/poll.h>
#endif

#include <fcntl.h>
#include <errno.h>
#include "ni_device_api.h"
#include "ni_rsrc_api.h"
#include "ni_util.h"
#include "ni_device_test.h"
#include "ni_bitstream.h"
#include "ni_p2p_ioctl.h"

#ifdef _WIN32
#include <fcntl.h>
#include <stdint.h>
#ifdef lseek
#undef lseek
#endif
#define lseek(f,p,w) _lseeki64((f), (p), (w))

#endif /* defined(_WIN32) */

typedef struct _ni_filter_params_t
{
    bool p2p;
    int scale_width;
    int scale_height;
    int format;
} ni_filter_params_t;

typedef struct _ni_disp_buffer
{
    int fd;
    int bar;
    uint8_t *data;
    unsigned long len;
    volatile uint8_t *mmap_data;
} disp_buffer_t;

typedef enum _ni_nalu_type
{
    H264_NAL_UNSPECIFIED = 0,
    H264_NAL_SLICE = 1,
    H264_NAL_DPA = 2,
    H264_NAL_DPB = 3,
    H264_NAL_DPC = 4,
    H264_NAL_IDR_SLICE = 5,
    H264_NAL_SEI = 6,
    H264_NAL_SPS = 7,
    H264_NAL_PPS = 8,
    H264_NAL_AUD = 9,
    H264_NAL_END_SEQUENCE = 10,
    H264_NAL_END_STREAM = 11,
    H264_NAL_FILLER_DATA = 12,
    H264_NAL_SPS_EXT = 13,
    H264_NAL_PREFIX = 14,
    H264_NAL_SUB_SPS = 15,
    H264_NAL_DPS = 16,
    H264_NAL_AUXILIARY_SLICE = 19,
} ni_nalu_type_t;

#define MAX_LOG2_MAX_FRAME_NUM (12 + 4)
#define MIN_LOG2_MAX_FRAME_NUM 4
#define EXTENDED_SAR 255
#define QP_MAX_NUM (51 + 6 * 6)   // The maximum supported qp
#define NI_MAX_BUFFERED_FRAME 45
// max YUV frame size
#define MAX_YUV_FRAME_SIZE (7680 * 4320 * 3)

/**
 * Picture parameter set
 */
typedef struct _ni_h264_pps_t
{
    unsigned int sps_id;
    int cabac;               ///< entropy_coding_mode_flag
    int pic_order_present;   ///< pic_order_present_flag
    int slice_group_count;   ///< num_slice_groups_minus1 + 1
    int mb_slice_group_map_type;
    unsigned int ref_count[2];   ///< num_ref_idx_l0/1_active_minus1 + 1
    int weighted_pred;           ///< weighted_pred_flag
    int weighted_bipred_idc;
    int init_qp;   ///< pic_init_qp_minus26 + 26
    int init_qs;   ///< pic_init_qs_minus26 + 26
    int chroma_qp_index_offset[2];
    int deblocking_filter_parameters_present;   ///< deblocking_filter_parameters_present_flag
    int constrained_intra_pred;                 ///< constrained_intra_pred_flag
    int redundant_pic_cnt_present;   ///< redundant_pic_cnt_present_flag
    int transform_8x8_mode;          ///< transform_8x8_mode_flag
    uint8_t scaling_matrix4[6][16];
    uint8_t scaling_matrix8[6][64];
    uint8_t chroma_qp_table[2][QP_MAX_NUM + 1];   ///< pre-scaled (with chroma_qp_index_offset) version of qp_table
    int chroma_qp_diff;
    uint8_t data[4096];
    size_t data_size;
    uint32_t dequant4_buffer[6][QP_MAX_NUM + 1][16];
    uint32_t dequant8_buffer[6][QP_MAX_NUM + 1][64];
    uint32_t (*dequant4_coeff[6])[16];
    uint32_t (*dequant8_coeff[6])[64];
} ni_h264_pps_t;

typedef struct _ni_test_frame_list
{
    ni_session_data_io_t frames[NI_MAX_BUFFERED_FRAME];
    int head;
    int tail;
} ni_test_frame_list_t;

typedef struct dec_send_param
{
    ni_session_context_t *p_dec_ctx;
    ni_session_data_io_t *p_in_pkt;
    int input_video_width;
    int input_video_height;
    int pkt_size;
    int print_time;
    unsigned long *p_total_bytes_sent;
    device_state_t *p_xcoder_state;
    void *p_stream_info;
} dec_send_param_t;

typedef struct dec_recv_param
{
    ni_session_context_t *p_dec_ctx;
    ni_session_data_io_t *p_out_frame;
    int output_video_width;
    int output_video_height;
    FILE *p_file;
    unsigned long long *p_total_bytes_received;
    device_state_t *p_xcoder_state;
    int mode;
    ni_test_frame_list_t *frame_list;
} dec_recv_param_t;

typedef struct enc_send_param
{
    ni_session_context_t *p_enc_ctx;
    ni_session_data_io_t *p_in_frame;
    int input_video_width;
    int input_video_height;
    int pfs;
    void *yuv_buf;
    int input_arg_width[MAX_INPUT_FILES];
    int input_arg_height[MAX_INPUT_FILES];    
    int input_bit_depth[MAX_INPUT_FILES];
    int input_arg_pfs[MAX_INPUT_FILES];
    int output_total;
    unsigned long *p_total_bytes_sent;
    device_state_t *p_xcoder_state;
    int mode;
    ni_test_frame_list_t *frame_list;
    uint32_t dec_codec_format;   // used in transcode mode
    int input_total;
    ni_sw_pix_fmt_t sw_pix_fmt;
    ni_rate_emu_t *p_rate_emu;
} enc_send_param_t;

typedef struct enc_recv_param
{
    ni_session_context_t *p_enc_ctx;
    ni_session_data_io_t *p_out_packet;
    int output_video_width;
    int output_video_height;
    FILE **p_file;
    int output_total;
    unsigned long long *p_total_bytes_received;
    uint32_t *p_number_of_packets;
    device_state_t *p_xcoder_state;
    int mode;
    ni_session_data_io_t *p_buffered_frame;
} enc_recv_param_t;

typedef struct uploader_param
{
    ni_session_context_t *p_upl_ctx;
    ni_session_context_t *p_sca_ctx;
    ni_session_data_io_t *p_swin_frame;
    ni_session_data_io_t *p_scale_frame;
    int input_video_width;
    int input_video_height;
    int pfs;
    void *yuv_buf;
    unsigned long *p_total_bytes_sent;
    int pool_size;
    ni_test_frame_list_t *frame_list;
} uploader_param_t;

volatile unsigned int g_end_of_all_threads = 0;
volatile unsigned int number_of_frames = 0;
volatile unsigned int number_of_frames_in_file = 0;
volatile unsigned int muxed_number_of_packets = 0;
uint8_t *p_av1_seq_header = NULL;
uint32_t av1_seq_header_len = 0;
uint8_t av1_output_obu = 0;
struct timeval start_time, previous_time, current_time;
time_t start_timestamp = 0, privious_timestamp = 0, current_timestamp = 0;

static uint8_t *g_file_cache = NULL;
static uint8_t *g_curr_cache_pos = NULL;
// a counter for reconfigFile line entry index
static int g_reconfigCount = 0;
// HW frame (with reference count) pool tracker
static ni_hwframe_ref_t g_hwframe_pool[NI_MAX_DR_HWDESC_FRAME_INDEX];
volatile uint64_t total_file_size = 0;
volatile uint64_t data_left_size = 0;
volatile int g_repeat = 1;

static ni_pix_fmt_name_t g_ni_pix_fmt_name_list[] = {
    {"yuv420p",     NI_PIX_FMT_YUV420P},     /* 8-bit YUV420 planar       */
    {"yuv420p10le", NI_PIX_FMT_YUV420P10LE}, /* 10-bit YUV420 planar      */
    {"nv12",        NI_PIX_FMT_NV12},        /* 8-bit YUV420 semi-planar  */
    {"p010le",      NI_PIX_FMT_P010LE},      /* 10-bit YUV420 semi-planar */
    {"rgba",        NI_PIX_FMT_RGBA},        /* 32-bit RGBA packed        */
    {"bgra",        NI_PIX_FMT_BGRA},        /* 32-bit BGRA packed        */
    {"argb",        NI_PIX_FMT_ARGB},        /* 32-bit ARGB packed        */
    {"abgr",        NI_PIX_FMT_ABGR},        /* 32-bit ABGR packed        */
    {"bgr0",        NI_PIX_FMT_BGR0},        /* 32-bit RGB packed         */
    {"bgrp",        NI_PIX_FMT_BGRP},        /* 24bit RGB packed          */
    {"nv16",        NI_PIX_FMT_NV16},        /* 8-bit YUV422 semi-planar  */
    {"yuyv422",     NI_PIX_FMT_YUYV422},     /* 8-bit YUV422              */
    {"uyvy422",     NI_PIX_FMT_UYVY422},     /* 8-bit YUV422              */
    {"null",        NI_PIX_FMT_NONE},        /* invalid format            */
};

static ni_gc620_pix_fmt_t g_ni_gc620_pix_fmt_list[] = {
    {NI_PIX_FMT_NV12, GC620_NV12},
    {NI_PIX_FMT_YUV420P, GC620_I420},
    {NI_PIX_FMT_P010LE, GC620_P010_MSB},
    {NI_PIX_FMT_YUV420P10LE, GC620_I010},
    {NI_PIX_FMT_YUYV422, GC620_YUYV},
    {NI_PIX_FMT_UYVY422, GC620_UYVY},
    {NI_PIX_FMT_NV16, GC620_NV16},
    {NI_PIX_FMT_RGBA, GC620_RGBA8888},
    {NI_PIX_FMT_BGR0, GC620_BGRX8888},
    {NI_PIX_FMT_BGRA, GC620_BGRA8888},
    {NI_PIX_FMT_ABGR, GC620_ABGR8888},
    {NI_PIX_FMT_ARGB, GC620_ARGB8888},
    {NI_PIX_FMT_BGRP, GC620_RGB888_PLANAR},
};

static void ni_hw_frame_ref(const niFrameSurface1_t *p_surface);
static void ni_hw_frame_unref(uint16_t hwframe_index);
static niFrameSurface1_t *hwupload_frame(ni_session_context_t *p_upl_ctx,
                                  ni_session_context_t *p_sca_ctx,
                                  ni_session_data_io_t *p_sw_data,
                                  ni_session_data_io_t *p_hw_data,
                                  ni_session_data_io_t *p_scale_data,
                                  ni_pix_fmt_t pix_fmt, int width,
                                  int height, int pfs, void *yuv_buf,
                                  unsigned long *bytes_sent, int *eos);
static int encoder_receive(ni_session_context_t *enc_ctx_list,
                           ni_session_data_io_t *in_frame,
                           ni_session_data_io_t *pkt, int width, int height,
                           uint32_t *number_of_packets_list,
                           int output_total, FILE **pfs_list,
                           unsigned long long *total_bytes_received_list,
                           int print_time, device_state_t *xcoder_state);

static inline int need_time_print(struct timeval *curr, struct timeval *prev)
{
#ifdef _WIN32
    return curr->tv_sec - prev->tv_sec > 3;
#else
    return curr->tv_sec - prev->tv_sec > 1;
#endif
}

static inline uint64_t get_total_file_size(int pfs)
{
    total_file_size = (uint64_t)lseek(pfs, 0, SEEK_END);
    lseek(pfs, 0, SEEK_SET);
    data_left_size = total_file_size;
    return total_file_size;
}

void arg_error_exit(char *arg_name, char *param)
{
    ni_log(NI_LOG_ERROR, "Error: unrecognized argument for %s, \"%s\"\n", arg_name,
            param);
    exit(-1);
}

static int is_ni_enc_pix_fmt(ni_pix_fmt_t pix_fmt)
{
    return pix_fmt == NI_PIX_FMT_YUV420P || pix_fmt == NI_PIX_FMT_NV12 ||
           pix_fmt == NI_PIX_FMT_YUV420P10LE || pix_fmt == NI_PIX_FMT_P010LE;
}

static inline bool frame_list_is_empty(ni_test_frame_list_t *list)
{
    return (list->head == list->tail);
}

static inline bool frame_list_is_full(ni_test_frame_list_t *list)
{
    return (list->head == ((list->tail + 1) % NI_MAX_BUFFERED_FRAME));
}

static inline int frame_list_length(ni_test_frame_list_t *list)
{
    return ((list->tail - list->head + NI_MAX_BUFFERED_FRAME) %
            NI_MAX_BUFFERED_FRAME);
}

static inline bool uploader_frame_list_full(ni_test_frame_list_t *list,
                                            ni_pix_fmt_t pix_fmt)
{
    // There are two types of pixel formats for hw uploading. One is those
    // supported by the NI encoder such as yuv420p nv12 etc. The other is those
    // unsupported by the NI encoder such as rgba bgr0 etc. Such formats should
    // be restricted by the hw scaler pool size in number because they have to
    // be converted into the formats as former.
    if (is_ni_enc_pix_fmt(pix_fmt))
    {
        return frame_list_is_full(list);
    } else
    {
        return frame_list_length(list) >=
            (NI_MAX_FILTER_POOL_SIZE < NI_MAX_BUFFERED_FRAME ?
             NI_MAX_FILTER_POOL_SIZE : NI_MAX_BUFFERED_FRAME);
    }
}

static inline int frame_list_enqueue(ni_test_frame_list_t *list)
{
    if (frame_list_is_full(list))
    {
        return -1;
    }
    list->tail = (list->tail + 1) % NI_MAX_BUFFERED_FRAME;
    return 0;
}

static inline int frame_list_drain(ni_test_frame_list_t *list)
{
    if (frame_list_is_empty(list))
    {
        return -1;
    }
    list->head = (list->head + 1) % NI_MAX_BUFFERED_FRAME;
    return 0;
}

static ni_pix_fmt_t ni_pixel_format_search(const char *name)
{
    int i;

    for (i = 0; i < sizeof(g_ni_pix_fmt_name_list)/sizeof(ni_pix_fmt_name_t); i++)
    {
        if (!strcmp(name, g_ni_pix_fmt_name_list[i].name))
        {
            return g_ni_pix_fmt_name_list[i].pix_fmt;
        }
    }

    return NI_PIX_FMT_NONE;
}

static const char *ni_pixel_format_name(ni_pix_fmt_t pix_fmt)
{
    int i;

    for (i = 0; i < sizeof(g_ni_pix_fmt_name_list)/sizeof(ni_pix_fmt_name_t); i++)
    {
        if (pix_fmt == g_ni_pix_fmt_name_list[i].pix_fmt)
        {
            return g_ni_pix_fmt_name_list[i].name;
        }
    }

    return NULL;
}

static int ni_to_gc620_pix_fmt(ni_pix_fmt_t pix_fmt)
{
    int i;

    for (i = 0; i < sizeof(g_ni_gc620_pix_fmt_list)/sizeof(ni_gc620_pix_fmt_t); i++)
    {
        if (g_ni_gc620_pix_fmt_list[i].pix_fmt_ni == pix_fmt)
        {
            return g_ni_gc620_pix_fmt_list[i].pix_fmt_gc620;
        }
    }

    return -1;
}

//Applies only to hwframe where recycling HW frame to FW is needed
//Loop through unsent frames to set in tracking list for cleanup
static void hwframe_list_release(ni_test_frame_list_t *list, ni_pix_fmt_t pix_fmt)
{
    int i;

    // store the unsent frames in the tracker to be cleared out by scan at end
    while (!frame_list_is_empty(list))
    {
        ni_frame_t *p_frame = &list->frames[list->head].data.frame;
        niFrameSurface1_t *p_surface = (niFrameSurface1_t *)p_frame->p_data[3];
        ni_hw_frame_ref(p_surface);
        frame_list_drain(list);
    }

    if (is_ni_enc_pix_fmt(pix_fmt))
    {
        for (i = 0; i < NI_MAX_BUFFERED_FRAME; i++)
        {
            ni_frame_buffer_free(&list->frames[i].data.frame);
        }
    }
}

// return actual bytes copied from cache, in requested size
uint32_t read_next_chunk(uint8_t *p_dst, uint32_t to_read)
{
    unsigned int to_copy = to_read;

    if (data_left_size == 0)
    {
        if (g_repeat > 1)
        {
            data_left_size = total_file_size;
            g_repeat--;
            ni_log(NI_LOG_DEBUG, "input processed %d left\n", g_repeat);
            to_copy = to_read;
            g_curr_cache_pos = g_file_cache;
        } else
        {
            return 0;
        }
    } else if (data_left_size < to_read)
    {
        to_copy = data_left_size;
    }

    memcpy(p_dst, g_curr_cache_pos, to_copy);
    g_curr_cache_pos += to_copy;
    data_left_size -= to_copy;

    return to_copy;
}

// return actual bytes copied from cache, in requested size
static uint32_t read_next_chunk_from_file(int pfs, uint8_t *p_dst, uint32_t to_read)
{
    uint8_t *tmp_dst = p_dst;
    unsigned int to_copy = to_read;
    unsigned long tmpFileSize = to_read;

    ni_log(NI_LOG_DEBUG, "%s: p_dst %p len %u totalSize %llu left %llu\n",
           __func__, tmp_dst, to_read, (unsigned long long)total_file_size,
           (unsigned long long)data_left_size);

    if (data_left_size == 0)
    {
        if (g_repeat > 1)
        {
            if (number_of_frames_in_file == 0)
            { //run once to know total frame count of input
                number_of_frames_in_file = number_of_frames;
            }
            data_left_size = total_file_size;
            g_repeat--;
            ni_log(NI_LOG_DEBUG, "input processed %d left\n", g_repeat);
            lseek(pfs, 0, SEEK_SET);   //back to beginning
        } else
        {
            return 0;
        }
    } else if (data_left_size < to_read)
    {
        tmpFileSize = data_left_size;
        to_copy = data_left_size;
    }

    int one_read_size = read(pfs, tmp_dst, to_copy);
    if (one_read_size == -1)
    {
        ni_log(NI_LOG_ERROR, "Error: reading file, quit! left-to-read %lu\n",
                tmpFileSize);
        ni_log(NI_LOG_ERROR, "Error: input file read error\n");
        return -1;
    } else if (one_read_size != to_copy)
    {
        ni_log(NI_LOG_ERROR,
                "Error: read unexpected bytes from file, actual %d != expected "
                "%u\n",
                one_read_size, to_copy);
        return -1;
    }
    data_left_size -= one_read_size;

    return one_read_size;
}

// current position of the input data buffer
static uint64_t curr_found_pos = 0;

// reset input data buffer position to the start
void reset_data_buf_pos(void)
{
    curr_found_pos = 0;
}

// rewind input data buffer position by a number of bytes, if possible
void rewind_data_buf_pos_by(uint64_t nb_bytes)
{
    // curr_found_pos (current input parser offset) could be equal to nb_bytes (NAL size) when offset jumps back to 0 due to repeat option
    if (curr_found_pos >= nb_bytes)
    {
        curr_found_pos -= nb_bytes;
    } else
    {
        ni_log(NI_LOG_ERROR, "Error %s %d bytes!\n", __func__, nb_bytes);
    }
}

// find/copy next H.264 NAL unit (including start code) and its type;
// return NAL data size if found, 0 otherwise
uint64_t find_h264_next_nalu(uint8_t *p_dst, int *nal_type)
{
    uint64_t data_size;
    uint64_t i = curr_found_pos;

    if (i + 3 >= total_file_size)
    {
        ni_log(NI_LOG_DEBUG,
               "%s reaching end, curr_pos %llu "
               "total input size %llu\n",
               __func__, (unsigned long long)curr_found_pos, (unsigned long long)total_file_size);

        if (g_repeat > 1)
        {
            g_repeat--;
            ni_log(NI_LOG_DEBUG, "input processed, %d left\n", g_repeat);
            reset_data_buf_pos();
            i = curr_found_pos;
        } else {
            return 0;
        }
    }

    // search for start code 0x000001 or 0x00000001
    while ((g_curr_cache_pos[i] != 0x00 || g_curr_cache_pos[i + 1] != 0x00 ||
            g_curr_cache_pos[i + 2] != 0x01) &&
           (g_curr_cache_pos[i] != 0x00 || g_curr_cache_pos[i + 1] != 0x00 ||
            g_curr_cache_pos[i + 2] != 0x00 || g_curr_cache_pos[i + 3] != 0x01))
    {
        i++;
        if (i + 3 > total_file_size)
        {
            return 0;
        }
    }

    // found start code, advance to NAL unit start depends on actual start code
    if (g_curr_cache_pos[i] != 0x00 || g_curr_cache_pos[i + 1] != 0x00 ||
        g_curr_cache_pos[i + 2] != 0x01)
    {
        i++;
    }

    i += 3;

    // get the NAL type
    *nal_type = (g_curr_cache_pos[i] & 0x1f);

    // advance to the end of NAL, or stream
    while ((g_curr_cache_pos[i] != 0x00 || g_curr_cache_pos[i + 1] != 0x00 ||
            g_curr_cache_pos[i + 2] != 0x00) &&
           (g_curr_cache_pos[i] != 0x00 || g_curr_cache_pos[i + 1] != 0x00 ||
            g_curr_cache_pos[i + 2] != 0x01))
    {
        i++;
        // if reaching the stream end
        if (i + 3 > total_file_size)
        {
            data_size = total_file_size - curr_found_pos;
            memcpy(p_dst, &g_curr_cache_pos[curr_found_pos], data_size);
            curr_found_pos = total_file_size;
            return data_size;
        }
    }

    data_size = i - curr_found_pos;
    memcpy(p_dst, &g_curr_cache_pos[curr_found_pos], data_size);
    curr_found_pos = i;
    return data_size;
}

static const uint8_t default_scaling4[2][16] = {
    {6, 13, 20, 28, 13, 20, 28, 32, 20, 28, 32, 37, 28, 32, 37, 42},
    {10, 14, 20, 24, 14, 20, 24, 27, 20, 24, 27, 30, 24, 27, 30, 34}};

static const uint8_t default_scaling8[2][64] = {
    {6,  10, 13, 16, 18, 23, 25, 27, 10, 11, 16, 18, 23, 25, 27, 29,
     13, 16, 18, 23, 25, 27, 29, 31, 16, 18, 23, 25, 27, 29, 31, 33,
     18, 23, 25, 27, 29, 31, 33, 36, 23, 25, 27, 29, 31, 33, 36, 38,
     25, 27, 29, 31, 33, 36, 38, 40, 27, 29, 31, 33, 36, 38, 40, 42},
    {9,  13, 15, 17, 19, 21, 22, 24, 13, 13, 17, 19, 21, 22, 24, 25,
     15, 17, 19, 21, 22, 24, 25, 27, 17, 19, 21, 22, 24, 25, 27, 28,
     19, 21, 22, 24, 25, 27, 28, 30, 21, 22, 24, 25, 27, 28, 30, 32,
     22, 24, 25, 27, 28, 30, 32, 33, 24, 25, 27, 28, 30, 32, 33, 35}};

const uint8_t ni_zigzag_direct[64] = {
    0,  1,  8,  16, 9,  2,  3,  10, 17, 24, 32, 25, 18, 11, 4,  5,
    12, 19, 26, 33, 40, 48, 41, 34, 27, 20, 13, 6,  7,  14, 21, 28,
    35, 42, 49, 56, 57, 50, 43, 36, 29, 22, 15, 23, 30, 37, 44, 51,
    58, 59, 52, 45, 38, 31, 39, 46, 53, 60, 61, 54, 47, 55, 62, 63};

const uint8_t ni_zigzag_scan[16 + 1] = {
    0 + 0 * 4, 1 + 0 * 4, 0 + 1 * 4, 0 + 2 * 4, 1 + 1 * 4, 2 + 0 * 4,
    3 + 0 * 4, 2 + 1 * 4, 1 + 2 * 4, 0 + 3 * 4, 1 + 3 * 4, 2 + 2 * 4,
    3 + 1 * 4, 3 + 2 * 4, 2 + 3 * 4, 3 + 3 * 4,
};

// HRD parsing: return 0 if parsing ok, -1 otherwise
int parse_hrd(ni_bitstream_reader_t *br, ni_h264_sps_t *sps)
{
    int cpb_count, i;

    cpb_count = (int)ni_bs_reader_get_ue(br) + 1;
    if (cpb_count > 32U)
    {
        ni_log(NI_LOG_ERROR, "parse_hrd invalid cpb_count %d\n", cpb_count);
        return -1;
    }

    ni_bs_reader_get_bits(br, 4);   // bit_rate_scale
    ni_bs_reader_get_bits(br, 4);   // cpb_size_scale
    for (i = 0; i < cpb_count; i++)
    {
        ni_bs_reader_get_ue(br);        // bit_rate_value_minus1
        ni_bs_reader_get_ue(br);        // cpb_size_value_minus1
        ni_bs_reader_get_bits(br, 1);   // cbr_flag
    }
    sps->initial_cpb_removal_delay_length =
        (int)ni_bs_reader_get_bits(br, 5) + 1;
    sps->cpb_removal_delay_length = (int)ni_bs_reader_get_bits(br, 5) + 1;
    sps->dpb_output_delay_length = (int)ni_bs_reader_get_bits(br, 5) + 1;
    sps->time_offset_length = ni_bs_reader_get_bits(br, 5);
    sps->cpb_cnt = cpb_count;
    return 0;
}

// VUI parsing: return 0 if parsing ok, -1 otherwise
int parse_vui(ni_bitstream_reader_t *br, ni_h264_sps_t *sps)
{
    int ret = -1, aspect_ratio_info_present_flag;
    unsigned int aspect_ratio_idc;

    aspect_ratio_info_present_flag = ni_bs_reader_get_bits(br, 1);
    if (aspect_ratio_info_present_flag)
    {
        aspect_ratio_idc = ni_bs_reader_get_bits(br, 8);
        if (EXTENDED_SAR == aspect_ratio_idc)
        {
            sps->sar.num = ni_bs_reader_get_bits(br, 16);
            sps->sar.den = ni_bs_reader_get_bits(br, 16);
        } else if (aspect_ratio_idc < NI_NUM_PIXEL_ASPECT_RATIO)
        {
            sps->sar = ni_h264_pixel_aspect_list[aspect_ratio_idc];
        } else
        {
            ni_log(NI_LOG_ERROR, "parse_vui: illegal aspect ratio %u\n",
                           aspect_ratio_idc);
            goto end;
        }
    } else
    {
        sps->sar.num = sps->sar.den = 0;
    }

    if (ni_bs_reader_get_bits(br, 1))   // overscan_info_present_flag
    {
        ni_bs_reader_get_bits(br, 1);   // overscan_appropriate_flag
    }
    sps->video_signal_type_present_flag = ni_bs_reader_get_bits(br, 1);
    if (sps->video_signal_type_present_flag)
    {
        ni_bs_reader_get_bits(br, 3);   // video_format
        sps->full_range =
            ni_bs_reader_get_bits(br, 1);   // video_full_range_flag

        sps->colour_description_present_flag = ni_bs_reader_get_bits(br, 1);
        if (sps->colour_description_present_flag)
        {
            sps->color_primaries = ni_bs_reader_get_bits(br, 8);
            sps->color_trc = ni_bs_reader_get_bits(br, 8);
            sps->colorspace = ni_bs_reader_get_bits(br, 8);
            if (sps->color_primaries < NI_COL_PRI_RESERVED0 ||
                sps->color_primaries >= NI_COL_PRI_NB)
            {
                sps->color_primaries = NI_COL_PRI_UNSPECIFIED;
            }
            if (sps->color_trc < NI_COL_TRC_RESERVED0 ||
                sps->color_trc >= NI_COL_TRC_NB)
            {
                sps->color_trc = NI_COL_TRC_UNSPECIFIED;
            }
            if (sps->colorspace < NI_COL_SPC_RGB ||
                sps->colorspace >= NI_COL_SPC_NB)
            {
                sps->colorspace = NI_COL_SPC_UNSPECIFIED;
            }
        }
    }

    if (ni_bs_reader_get_bits(br, 1))   // chroma_location_info_present_flag
    {
        ni_bs_reader_get_ue(br);   // chroma_sample_location_type_top_field
        ni_bs_reader_get_ue(br);   // chroma_sample_location_type_bottom_field
    }

    sps->timing_info_present_flag = ni_bs_reader_get_bits(br, 1);
    if (sps->timing_info_present_flag)
    {
        unsigned num_units_in_tick = ni_bs_reader_get_bits(br, 32);
        unsigned time_scale = ni_bs_reader_get_bits(br, 32);
        if (!num_units_in_tick || !time_scale)
        {
            ni_log(NI_LOG_ERROR, "parse_vui: error num_units_in_tick/time_scale "
                           "(%u/%u)\n",
                           num_units_in_tick, time_scale);
            sps->timing_info_present_flag = 0;
        }
        sps->fixed_frame_rate_flag = ni_bs_reader_get_bits(br, 1);
    }

    sps->nal_hrd_parameters_present_flag = ni_bs_reader_get_bits(br, 1);
    if (sps->nal_hrd_parameters_present_flag && parse_hrd(br, sps) < 0)
    {
        ni_log(NI_LOG_ERROR, "parse_vui: nal_hrd_parameters_present and error "
                       "parse_hrd !\n");
        goto end;
    }

    sps->vcl_hrd_parameters_present_flag = ni_bs_reader_get_bits(br, 1);
    if (sps->vcl_hrd_parameters_present_flag && parse_hrd(br, sps) < 0)
    {
        ni_log(NI_LOG_ERROR, "parse_vui: vcl_hrd_parameters_present and error "
                       "parse_hrd !\n");
        goto end;
    }

    if (sps->nal_hrd_parameters_present_flag ||
        sps->vcl_hrd_parameters_present_flag)
    {
        ni_bs_reader_get_bits(br, 1);   // low_delay_hrd_flag
    }

    sps->pic_struct_present_flag = ni_bs_reader_get_bits(br, 1);

    sps->bitstream_restriction_flag = ni_bs_reader_get_bits(br, 1);
    if (sps->bitstream_restriction_flag)
    {
        ni_bs_reader_get_bits(br,
                              1);   // motion_vectors_over_pic_boundaries_flag
        ni_bs_reader_get_ue(br);    // max_bytes_per_pic_denom
        ni_bs_reader_get_ue(br);    // max_bits_per_mb_denom
        ni_bs_reader_get_ue(br);    // log2_max_mv_length_horizontal
        ni_bs_reader_get_ue(br);    // log2_max_mv_length_vertical
        sps->num_reorder_frames = ni_bs_reader_get_ue(br);
        sps->max_dec_frame_buffering = ni_bs_reader_get_ue(br);

        if (sps->num_reorder_frames > 16U)
        {
            ni_log(NI_LOG_ERROR, "parse_vui: clip illegal num_reorder_frames %d !\n",
                           sps->num_reorder_frames);
            sps->num_reorder_frames = 16;
            goto end;
        }
    }

    // everything is fine
    ret = 0;

end:
    return ret;
}

int parse_scaling_list(ni_bitstream_reader_t *br, uint8_t *factors, int size,
                       const uint8_t *jvt_list, const uint8_t *fallback_list)
{
    int i, last = 8, next = 8;
    const uint8_t *scan = (size == 16 ? ni_zigzag_scan : ni_zigzag_direct);

    // matrix not written, we use the predicted one */
    if (!ni_bs_reader_get_bits(br, 1))
    {
        memcpy(factors, fallback_list, size * sizeof(uint8_t));
    } else
    {
        for (i = 0; i < size; i++)
        {
            if (next)
            {
                int v = ni_bs_reader_get_se(br);
                if (v < -128 || v > 127)
                {
                    ni_log(NI_LOG_ERROR, "delta scale %d is invalid\n", v);
                    return -1;
                }
                next = (last + v) & 0xff;
            }
            if (!i && !next)
            {   // matrix not written, we use the preset one
                memcpy(factors, jvt_list, size * sizeof(uint8_t));
                break;
            }
            last = (factors[scan[i]] = next ? next : last);
        }
    }
    return 0;
}

// SPS seq scaling matrices parsing: return 0 if parsing ok, -1 otherwise
int parse_scaling_matrices(ni_bitstream_reader_t *br, const ni_h264_sps_t *sps,
                           const ni_h264_pps_t *pps, int is_sps,
                           uint8_t (*scaling_matrix4)[16],
                           uint8_t (*scaling_matrix8)[64])
{
    int ret = 0;
    int fallback_sps = !is_sps && sps->scaling_matrix_present;
    const uint8_t *fallback[4] = {
        fallback_sps ? sps->scaling_matrix4[0] : default_scaling4[0],
        fallback_sps ? sps->scaling_matrix4[3] : default_scaling4[1],
        fallback_sps ? sps->scaling_matrix8[0] : default_scaling8[0],
        fallback_sps ? sps->scaling_matrix8[3] : default_scaling8[1]};

    if (ni_bs_reader_get_bits(br, 1))   // scaling_matrix_present
    {
        // retrieve matrices
        ret |=
            parse_scaling_list(br, scaling_matrix4[0], 16, default_scaling4[0],
                               fallback[0]);   // Intra, Y
        ret |=
            parse_scaling_list(br, scaling_matrix4[1], 16, default_scaling4[0],
                               scaling_matrix4[0]);   // Intra, Cr
        ret |=
            parse_scaling_list(br, scaling_matrix4[2], 16, default_scaling4[0],
                               scaling_matrix4[1]);   // Intra, Cb
        ret |=
            parse_scaling_list(br, scaling_matrix4[3], 16, default_scaling4[1],
                               fallback[1]);   // Inter, Y
        ret |=
            parse_scaling_list(br, scaling_matrix4[4], 16, default_scaling4[1],
                               scaling_matrix4[3]);   // Inter, Cr
        ret |=
            parse_scaling_list(br, scaling_matrix4[5], 16, default_scaling4[1],
                               scaling_matrix4[4]);   // Inter, Cb

        if (is_sps || pps->transform_8x8_mode)
        {
            ret |= parse_scaling_list(br, scaling_matrix8[0], 64,
                                      default_scaling8[0],
                                      fallback[2]);   // Intra, Y
            ret |= parse_scaling_list(br, scaling_matrix8[3], 64,
                                      default_scaling8[1],
                                      fallback[3]);   // Inter, Y
            if (sps->chroma_format_idc == 3)
            {
                ret |= parse_scaling_list(
                    br, scaling_matrix8[1], 64,   // Intra, Cr
                    default_scaling8[0], scaling_matrix8[0]);
                ret |= parse_scaling_list(
                    br, scaling_matrix8[4], 64,   // Inter, Cr
                    default_scaling8[1], scaling_matrix8[3]);
                ret |= parse_scaling_list(
                    br, scaling_matrix8[2], 64,   // Intra, Cb
                    default_scaling8[0], scaling_matrix8[1]);
                ret |= parse_scaling_list(
                    br, scaling_matrix8[5], 64,   // Inter, Cb
                    default_scaling8[1], scaling_matrix8[4]);
            }
        }
        if (!ret)
        {
            ret = is_sps;
        }
    }
    return ret;
}

// SPS parsing: return 0 if parsing ok, -1 otherwise
int parse_sps(uint8_t *buf, int size_bytes, ni_h264_sps_t *sps)
{
    int ret = -1;
    ni_bitstream_reader_t br;
    int profile_idc, level_idc, constraint_set_flags = 0;
    uint32_t sps_id;
    int i, log2_max_frame_num_minus4;

    ni_bitstream_reader_init(&br, buf, 8 * size_bytes);
    // skip NAL header
    ni_bs_reader_skip_bits(&br, 8);

    profile_idc = ni_bs_reader_get_bits(&br, 8);
    // from constraint_set0_flag to constraint_set5_flag
    constraint_set_flags |= ni_bs_reader_get_bits(&br, 1) << 0;
    constraint_set_flags |= ni_bs_reader_get_bits(&br, 1) << 1;
    constraint_set_flags |= ni_bs_reader_get_bits(&br, 1) << 2;
    constraint_set_flags |= ni_bs_reader_get_bits(&br, 1) << 3;
    constraint_set_flags |= ni_bs_reader_get_bits(&br, 1) << 4;
    constraint_set_flags |= ni_bs_reader_get_bits(&br, 1) << 5;
    ni_bs_reader_skip_bits(&br, 2);   // reserved_zero_2bits
    level_idc = ni_bs_reader_get_bits(&br, 8);
    sps_id = ni_bs_reader_get_ue(&br);

    sps->sps_id = sps_id;
    sps->profile_idc = profile_idc;
    sps->constraint_set_flags = constraint_set_flags;
    sps->level_idc = level_idc;
    sps->full_range = -1;

    memset(sps->scaling_matrix4, 16, sizeof(sps->scaling_matrix4));
    memset(sps->scaling_matrix8, 16, sizeof(sps->scaling_matrix8));
    sps->scaling_matrix_present = 0;
    sps->colorspace = 2;   // NI_COL_SPC_UNSPECIFIED

    if (100 == profile_idc || 110 == profile_idc || 122 == profile_idc ||
        244 == profile_idc || 44 == profile_idc || 83 == profile_idc ||
        86 == profile_idc || 118 == profile_idc || 128 == profile_idc ||
        138 == profile_idc || 139 == profile_idc || 134 == profile_idc ||
        135 == profile_idc || 144 == profile_idc /* old High444 profile */)
    {
        sps->chroma_format_idc = ni_bs_reader_get_ue(&br);
        if (sps->chroma_format_idc > 3U)
        {
            ni_log(NI_LOG_ERROR, "parse_sps error: chroma_format_idc > 3 !\n");
            goto end;
        } else if (3 == sps->chroma_format_idc)
        {
            sps->residual_color_transform_flag = ni_bs_reader_get_bits(&br, 1);
            if (sps->residual_color_transform_flag)
            {
                ni_log(NI_LOG_ERROR, "parse_sps error: residual_color_transform not "
                               "supported !\n");
                goto end;
            }
        }
        sps->bit_depth_luma = (int)ni_bs_reader_get_ue(&br) + 8;
        sps->bit_depth_chroma = (int)ni_bs_reader_get_ue(&br) + 8;
        if (sps->bit_depth_luma != sps->bit_depth_chroma)
        {
            ni_log(NI_LOG_ERROR, "parse_sps error: different luma %d & chroma %d "
                           "bit depth !\n",
                           sps->bit_depth_luma, sps->bit_depth_chroma);
            goto end;
        }
        if (sps->bit_depth_luma < 8 || sps->bit_depth_luma > 12 ||
            sps->bit_depth_chroma < 8 || sps->bit_depth_chroma > 12)
        {
            ni_log(NI_LOG_ERROR, "parse_sps error: illegal luma/chroma bit depth "
                           "value (%d %d) !\n",
                           sps->bit_depth_luma, sps->bit_depth_chroma);
            goto end;
        }

        sps->transform_bypass = ni_bs_reader_get_bits(&br, 1);
        ret = parse_scaling_matrices(&br, sps, NULL, 1, sps->scaling_matrix4,
                                     sps->scaling_matrix8);
        if (ret < 0)
        {
            ni_log(NI_LOG_ERROR, "parse_sps error scaling matrices parse failed !\n");
            goto end;
        }
        sps->scaling_matrix_present |= ret;
    }   // profile_idc
    else
    {
        sps->chroma_format_idc = 1;
        sps->bit_depth_luma = 8;
        sps->bit_depth_chroma = 8;
    }

    log2_max_frame_num_minus4 = ni_bs_reader_get_ue(&br);
    if (log2_max_frame_num_minus4 < MIN_LOG2_MAX_FRAME_NUM - 4 ||
        log2_max_frame_num_minus4 > MAX_LOG2_MAX_FRAME_NUM - 4)
    {
        ni_log(NI_LOG_ERROR, "parse_sps error: log2_max_frame_num_minus4 %d out of "
                       "range (0-12)!\n",
                       log2_max_frame_num_minus4);
        goto end;
    }
    sps->log2_max_frame_num = log2_max_frame_num_minus4 + 4;

    sps->poc_type = ni_bs_reader_get_ue(&br);
    if (0 == sps->poc_type)
    {
        uint32_t v = ni_bs_reader_get_ue(&br);
        if (v > 12)
        {
            ni_log(NI_LOG_ERROR, "parse_sps error: log2_max_poc_lsb %u out of range! "
                           "\n",
                           v);
            goto end;
        }
        sps->log2_max_poc_lsb = (int)v + 4;
    } else if (1 == sps->poc_type)
    {
        sps->delta_pic_order_always_zero_flag = ni_bs_reader_get_bits(&br, 1);
        sps->offset_for_non_ref_pic = ni_bs_reader_get_se(&br);
        sps->offset_for_top_to_bottom_field = ni_bs_reader_get_se(&br);
        sps->poc_cycle_length = ni_bs_reader_get_ue(&br);
        if ((unsigned)sps->poc_cycle_length >= 256)
        {
            ni_log(NI_LOG_ERROR, "parse_sps error: poc_cycle_length %d out of range! "
                           "\n",
                           sps->poc_cycle_length);
            goto end;
        }
        for (i = 0; i < sps->poc_cycle_length; i++)
        {
            sps->offset_for_ref_frame[i] = ni_bs_reader_get_se(&br);
        }
    } else if (2 != sps->poc_type)
    {
        ni_log(NI_LOG_ERROR, "parse_sps error: illegal PIC type %d!\n",
                       sps->poc_type);
        goto end;
    }
    sps->ref_frame_count = ni_bs_reader_get_ue(&br);
    sps->gaps_in_frame_num_allowed_flag = ni_bs_reader_get_bits(&br, 1);
    sps->mb_width = (int)ni_bs_reader_get_ue(&br) + 1;
    sps->mb_height = (int)ni_bs_reader_get_ue(&br) + 1;

    sps->frame_mbs_only_flag = ni_bs_reader_get_bits(&br, 1);
    sps->mb_height *= 2 - sps->frame_mbs_only_flag;

    if (!sps->frame_mbs_only_flag)
    {
        sps->mb_aff = ni_bs_reader_get_bits(&br, 1);
    } else
    {
        sps->mb_aff = 0;
    }

    sps->direct_8x8_inference_flag = ni_bs_reader_get_bits(&br, 1);

    sps->crop = ni_bs_reader_get_bits(&br, 1);
    if (sps->crop)
    {
        unsigned int crop_left = ni_bs_reader_get_ue(&br);
        unsigned int crop_right = ni_bs_reader_get_ue(&br);
        unsigned int crop_top = ni_bs_reader_get_ue(&br);
        unsigned int crop_bottom = ni_bs_reader_get_ue(&br);

        // no range checking
        int vsub = (sps->chroma_format_idc == 1) ? 1 : 0;
        int hsub =
            (sps->chroma_format_idc == 1 || sps->chroma_format_idc == 2) ? 1 :
                                                                           0;
        int step_x = 1 << hsub;
        int step_y = (2 - sps->frame_mbs_only_flag) << vsub;

        sps->crop_left = crop_left * step_x;
        sps->crop_right = crop_right * step_x;
        sps->crop_top = crop_top * step_y;
        sps->crop_bottom = crop_bottom * step_y;
    } else
    {
        sps->crop_left = sps->crop_right = sps->crop_top = sps->crop_bottom =
            sps->crop = 0;
    }

    // deduce real width/heigh
    sps->width = (int)(16 * sps->mb_width - sps->crop_left - sps->crop_right);
    sps->height = (int)(16 * sps->mb_height - sps->crop_top - sps->crop_bottom);

    sps->vui_parameters_present_flag = ni_bs_reader_get_bits(&br, 1);
    if (sps->vui_parameters_present_flag)
    {
        int ret1 = parse_vui(&br, sps);
        if (ret1 < 0)
        {
            ni_log(NI_LOG_ERROR, "parse_sps error: parse_vui failed %d!\n", ret);
            goto end;
        }
    }

    // everything is fine
    ret = 0;

end:

    return ret;
}

int parse_sei(uint8_t *buf, int size_bytes, ni_h264_sps_t *sps, int *sei_type,
              int *is_interlaced)
{
    ni_bitstream_reader_t br;
    *is_interlaced = 0;
    int ret = -1, dummy;
    int cpb_dpb_delays_present_flag = (sps->nal_hrd_parameters_present_flag ||
                                       sps->vcl_hrd_parameters_present_flag);
    //pic_struct_present_flag

    ni_bitstream_reader_init(&br, buf, 8 * size_bytes);
    // skip NAL header
    ni_bs_reader_skip_bits(&br, 8);

    while (ni_bs_reader_get_bits_left(&br) > 16)
    {
        int next, size = 0;
        unsigned type = 0, tmp;

        do
        {
            if (ni_bs_reader_get_bits_left(&br) < 8)
            {
                ni_log(NI_LOG_ERROR, "parse_sei type parse error !\n");
                goto end;
            }
            tmp = ni_bs_reader_get_bits(&br, 8);
            type += tmp;
        } while (tmp == 0xFF);

        *sei_type = (int)type;
        do
        {
            if (ni_bs_reader_get_bits_left(&br) < 8)
            {
                ni_log(NI_LOG_ERROR, "parse_sei type %u size parse error !\n", type);
                goto end;
            }
            tmp = ni_bs_reader_get_bits(&br, 8);
            size += (int)tmp;
        } while (tmp == 0xFF);

        if (size > ni_bs_reader_get_bits_left(&br) / 8)
        {
            ni_log(NI_LOG_DEBUG, "parse_sei SEI type %u size %d truncated at %d\n",
                           type, size, ni_bs_reader_get_bits_left(&br));
            goto end;
        }
        next = ni_bs_reader_bits_count(&br) + 8 * size;

        switch (type)
        {
            case NI_H264_SEI_TYPE_PIC_TIMING:
                if (cpb_dpb_delays_present_flag)
                {
                    ni_bs_reader_get_bits(&br, sps->cpb_removal_delay_length);
                    ni_bs_reader_get_bits(&br, sps->dpb_output_delay_length);
                }
                if (sps->pic_struct_present_flag)
                {
                    dummy = ni_bs_reader_get_bits(&br, 4);
                    if (dummy < NI_H264_SEI_PIC_STRUCT_FRAME ||
                        dummy > NI_H264_SEI_PIC_STRUCT_FRAME_TRIPLING)
                    {
                        ni_log(NI_LOG_DEBUG, 
                            "parse_sei pic_timing SEI invalid pic_struct: "
                            "%d\n",
                            dummy);
                        goto end;
                    }
                    if (dummy > NI_H264_SEI_PIC_STRUCT_FRAME)
                    {
                        *is_interlaced = 1;
                    }
                    goto success;
                }
                break;
            default:
                // skip all other SEI types
                ;
        }
        ni_bs_reader_skip_bits(&br, next - ni_bs_reader_bits_count(&br));
    }   // while in SEI

success:
    ret = 0;

end:
    return ret;
}

// probe h.264 stream info; return 0 if stream can be decoded, -1 otherwise
int probe_h264_stream_info(ni_h264_sps_t *sps)
{
    int ret = -1;
    uint8_t *buf = NULL;
    uint8_t *p_buf;
    uint32_t nal_size, ep3_removed = 0, vcl_nal_count = 0;
    int nal_type = -1, sei_type = -1;
    int sps_parsed = 0, is_interlaced = 0;

    if (NULL == (buf = calloc(1, NI_MAX_TX_SZ)))
    {
        ni_log(NI_LOG_ERROR, "Error probe_h264_stream_info: allocate stream buf\n");
        goto end;
    }

    reset_data_buf_pos();
    // probe at most 100 VCL before stops
    while ((!sps_parsed || !is_interlaced) && vcl_nal_count < 100 &&
           (nal_size = find_h264_next_nalu(buf, &nal_type)) > 0)
    {
        ni_log(NI_LOG_DEBUG, "nal %d  nal_size %d\n", nal_type, nal_size);
        p_buf = buf;

        // skip the start code
        while (!(p_buf[0] == 0x00 && p_buf[1] == 0x00 && p_buf[2] == 0x01) &&
               nal_size > 3)
        {
            p_buf++;
            nal_size--;
        }
        if (nal_size <= 3)
        {
            ni_log(NI_LOG_ERROR, "Error probe_h264_stream_info NAL has no header\n");
            continue;
        }

        p_buf += 3;
        nal_size -= 3;

        ep3_removed = ni_remove_emulation_prevent_bytes(p_buf, nal_size);
        nal_size -= ep3_removed;

        if (H264_NAL_SPS == nal_type && !sps_parsed)
        {
            if (vcl_nal_count > 0)
            {
                ni_log(NI_LOG_DEBUG,
                       "Warning: %s has %d slice NAL units ahead of SPS!\n",
                       __func__, vcl_nal_count);
            }

            if (parse_sps(p_buf, nal_size, sps))
            {
                ni_log(NI_LOG_ERROR, "probe_h264_stream_info: parse_sps error\n");
                break;
            }
            sps_parsed = 1;
        } else if (H264_NAL_SEI == nal_type)
        {
            parse_sei(p_buf, nal_size, sps, &sei_type, &is_interlaced);
        } else if (H264_NAL_SLICE == nal_type || H264_NAL_IDR_SLICE == nal_type)
        {
            vcl_nal_count++;
        }

        if (sps_parsed &&
            (sps->pic_struct_present_flag ||
             sps->nal_hrd_parameters_present_flag ||
             sps->vcl_hrd_parameters_present_flag) &&
            NI_H264_SEI_TYPE_PIC_TIMING == sei_type && is_interlaced)
        {
            ni_log(NI_LOG_ERROR, 
                "probe_h264_stream_info interlaced NOT supported!\n");
            break;
        }
    }   // while for each NAL unit

    reset_data_buf_pos();

    ni_log(NI_LOG_DEBUG, "VCL NAL parsed: %d, SPS parsed: %s, is interlaced: %s\n",
                   vcl_nal_count, sps_parsed ? "Yes" : "No",
                   is_interlaced ? "Yes" : "No");
    if (sps_parsed && !is_interlaced)
    {
        ret = 0;
    } else
    {
        ni_log(NI_LOG_ERROR, "Input is either interlaced, or unable to determine, "
                       "probing failed.\n");
    }

    static const char csp[4][5] = {"Gray", "420", "422", "444"};
    ni_log(NI_LOG_DEBUG, 
        "H.264 stream probed %d VCL NAL units, sps:%u "
        "profile:%d/%d poc %d ref:%d %dx%d [SAR: %d:%d] %s %s "
        "%" PRId32 "/%" PRId32 " %d bits max_reord:%d max_dec_buf:"
        "%d\n",
        vcl_nal_count, sps->sps_id, sps->profile_idc, sps->level_idc,
        sps->poc_type, sps->ref_frame_count, sps->width, sps->height,
        /*sps->crop_left, sps->crop_right, sps->crop_top, sps->crop_bottom,*/
        sps->sar.num, sps->sar.den,
        sps->vui_parameters_present_flag ? "VUI" : "no-VUI",
        csp[sps->chroma_format_idc],
        sps->timing_info_present_flag ? sps->num_units_in_tick : 0,
        sps->timing_info_present_flag ? sps->time_scale : 0,
        sps->bit_depth_luma,
        sps->bitstream_restriction_flag ? sps->num_reorder_frames : -1,
        sps->bitstream_restriction_flag ? sps->max_dec_frame_buffering : -1);

end:
    free(buf);
    buf = NULL;
    return ret;
}

// parse H.264 slice header to get frame_num; return 0 if success, -1 otherwise
int parse_h264_slice_header(uint8_t *buf, int size_bytes, ni_h264_sps_t *sps,
                            int32_t *frame_num, unsigned int *first_mb_in_slice)
{
    ni_bitstream_reader_t br;
    uint8_t *p_buf = buf;
    unsigned int slice_type, pps_id;

    // skip the start code
    while (!(p_buf[0] == 0x00 && p_buf[1] == 0x00 && p_buf[2] == 0x01) &&
           size_bytes > 3)
    {
        p_buf++;
        size_bytes--;
    }
    if (size_bytes <= 3)
    {
        ni_log(NI_LOG_ERROR, "Error parse_h264_slice_header slice has no header\n");
        return -1;
    }

    p_buf += 3;
    size_bytes -= 3;

    ni_bitstream_reader_init(&br, p_buf, 8 * size_bytes);

    // skip NAL header
    ni_bs_reader_skip_bits(&br, 8);

    *first_mb_in_slice = ni_bs_reader_get_ue(&br);
    slice_type = ni_bs_reader_get_ue(&br);
    if (slice_type > 9)
    {
        ni_log(NI_LOG_ERROR, "parse_h264_slice_header error: slice type %u too "
                       "large at %u\n",
                       slice_type, *first_mb_in_slice);
        return -1;
    }
    pps_id = ni_bs_reader_get_ue(&br);
    *frame_num = ni_bs_reader_get_bits(&br, sps->log2_max_frame_num);

    ni_log(NI_LOG_DEBUG, "parse_h264_slice_header slice type %u frame_num %d "
                   "pps_id %u size %d first_mb %u\n",
                   slice_type, *frame_num, pps_id, size_bytes,
                   *first_mb_in_slice);

    return 0;
}

typedef enum _ni_hevc_nalu_type
{
    HEVC_NAL_TRAIL_N = 0,
    HEVC_NAL_TRAIL_R = 1,
    HEVC_NAL_TSA_N = 2,
    HEVC_NAL_TSA_R = 3,
    HEVC_NAL_STSA_N = 4,
    HEVC_NAL_STSA_R = 5,
    HEVC_NAL_RADL_N = 6,
    HEVC_NAL_RADL_R = 7,
    HEVC_NAL_RASL_N = 8,
    HEVC_NAL_RASL_R = 9,
    HEVC_NAL_IDR_W_RADL = 19,
    HEVC_NAL_IDR_N_LP = 20,
    HEVC_NAL_CRA_NUT = 21,
    HEVC_NAL_VPS = 32,
    HEVC_NAL_SPS = 33,
    HEVC_NAL_PPS = 34,
    HEVC_NAL_AUD = 35,
    HEVC_NAL_EOS_NUT = 36,
    HEVC_NAL_EOB_NUT = 37,
    HEVC_NAL_FD_NUT = 38,
    HEVC_NAL_SEI_PREFIX = 39,
    HEVC_NAL_SEI_SUFFIX = 40,
} ni_hevc_nalu_type;

static const ni_rational_t vui_sar[] = {
    {0, 1},   {1, 1},    {12, 11}, {10, 11}, {16, 11}, {40, 33},
    {24, 11}, {20, 11},  {32, 11}, {80, 33}, {18, 11}, {15, 11},
    {64, 33}, {160, 99}, {4, 3},   {3, 2},   {2, 1},
};

static const uint8_t hevc_sub_width_c[] = {1, 2, 2, 1};

static const uint8_t hevc_sub_height_c[] = {1, 2, 1, 1};

const uint8_t hevc_diag_scan4x4_x[16] = {
    0, 0, 1, 0, 1, 2, 0, 1, 2, 3, 1, 2, 3, 2, 3, 3,
};

const uint8_t hevc_diag_scan4x4_y[16] = {
    0, 1, 0, 2, 1, 0, 3, 2, 1, 0, 3, 2, 1, 3, 2, 3,
};

const uint8_t hevc_diag_scan8x8_x[64] = {
    0, 0, 1, 0, 1, 2, 0, 1, 2, 3, 0, 1, 2, 3, 4, 0, 1, 2, 3, 4, 5, 0,
    1, 2, 3, 4, 5, 6, 0, 1, 2, 3, 4, 5, 6, 7, 1, 2, 3, 4, 5, 6, 7, 2,
    3, 4, 5, 6, 7, 3, 4, 5, 6, 7, 4, 5, 6, 7, 5, 6, 7, 6, 7, 7,
};

const uint8_t hevc_diag_scan8x8_y[64] = {
    0, 1, 0, 2, 1, 0, 3, 2, 1, 0, 4, 3, 2, 1, 0, 5, 4, 3, 2, 1, 0, 6,
    5, 4, 3, 2, 1, 0, 7, 6, 5, 4, 3, 2, 1, 0, 7, 6, 5, 4, 3, 2, 1, 7,
    6, 5, 4, 3, 2, 7, 6, 5, 4, 3, 7, 6, 5, 4, 7, 6, 5, 7, 6, 7,
};

static const uint8_t default_scaling_list_inter[] = {
    16, 16, 16, 16, 17, 18, 20, 24, 16, 16, 16, 17, 18, 20, 24, 25,
    16, 16, 17, 18, 20, 24, 25, 28, 16, 17, 18, 20, 24, 25, 28, 33,
    17, 18, 20, 24, 25, 28, 33, 41, 18, 20, 24, 25, 28, 33, 41, 54,
    20, 24, 25, 28, 33, 41, 54, 71, 24, 25, 28, 33, 41, 54, 71, 91};

static const uint8_t default_scaling_list_intra[] = {
    16, 16, 16, 16, 17, 18, 21, 24, 16, 16, 16, 16, 17, 19, 22, 25,
    16, 16, 17, 18, 20, 22, 25, 29, 16, 16, 18, 21, 24, 27, 31, 36,
    17, 17, 20, 24, 30, 35, 41, 47, 18, 19, 22, 27, 35, 44, 54, 65,
    21, 22, 25, 31, 41, 54, 70, 88, 24, 25, 29, 36, 47, 65, 88, 115};

/**
 * find/copy next H.265 NAL unit (including start code) and its type;
 * return NAL data size if found, 0 otherwise
*/
uint64_t find_h265_next_nalu(uint8_t *p_dst, int *nal_type)
{
    uint64_t data_size;
    uint64_t i = curr_found_pos;

    if (i + 3 >= total_file_size)
    {
        ni_log(NI_LOG_DEBUG,
               "%s reaching end, curr_pos %llu "
               "total input size %llu\n",
               __func__, (unsigned long long)curr_found_pos, (unsigned long long)total_file_size);

        if (g_repeat > 1)
        {
            g_repeat--;
            ni_log(NI_LOG_DEBUG, "input processed, %d left\n", g_repeat);
            reset_data_buf_pos();
            i = curr_found_pos;
        } else {
            return 0;
        }
    }

    // search for start code 0x000001 or 0x00000001
    while ((g_curr_cache_pos[i] != 0x00 || g_curr_cache_pos[i + 1] != 0x00 ||
            g_curr_cache_pos[i + 2] != 0x01) &&
           (g_curr_cache_pos[i] != 0x00 || g_curr_cache_pos[i + 1] != 0x00 ||
            g_curr_cache_pos[i + 2] != 0x00 || g_curr_cache_pos[i + 3] != 0x01))
    {
        i++;
        if (i + 3 > total_file_size)
        {
            return 0;
        }
    }

    // found start code, advance to NAL unit start depends on actual start code
    if (g_curr_cache_pos[i] != 0x00 || g_curr_cache_pos[i + 1] != 0x00 ||
        g_curr_cache_pos[i + 2] != 0x01)
    {
        i++;
    }

    i += 3;

    // get the NAL type
    *nal_type = (g_curr_cache_pos[i] & 0x7E) >> 1;

    // advance to the end of NAL, or stream
    while ((g_curr_cache_pos[i] != 0x00 || g_curr_cache_pos[i + 1] != 0x00 ||
            g_curr_cache_pos[i + 2] != 0x00) &&
           (g_curr_cache_pos[i] != 0x00 || g_curr_cache_pos[i + 1] != 0x00 ||
            g_curr_cache_pos[i + 2] != 0x01))
    {
        i++;
        // if reaching the stream end
        if (i + 3 > total_file_size)
        {
            data_size = total_file_size - curr_found_pos;
            memcpy(p_dst, &g_curr_cache_pos[curr_found_pos], data_size);
            curr_found_pos = total_file_size;
            return data_size;
        }
    }

    data_size = i - curr_found_pos;
    memcpy(p_dst, &g_curr_cache_pos[curr_found_pos], data_size);
    curr_found_pos = i;
    return data_size;
}

static void h265_decode_sublayer_hrd(ni_bitstream_reader_t *br,
                                     unsigned int nb_cpb,
                                     int subpic_params_present)
{
    uint32_t i;

    for (i = 0; i < nb_cpb; i++)
    {
        ni_bs_reader_get_ue(br);   // bit_rate_value_minus1
        ni_bs_reader_get_ue(br);   // cpb_size_value_minus1

        if (subpic_params_present)
        {
            ni_bs_reader_get_ue(br);   // cpb_size_du_value_minus1
            ni_bs_reader_get_ue(br);   // bit_rate_du_value_minus1
        }
        ni_bs_reader_skip_bits(br, 1);   // cbr_flag
    }
}

static int h265_decode_profile_tier_level(ni_bitstream_reader_t *br,
                                          PTLCommon *ptl)
{
    int i;

    if (ni_bs_reader_get_bits_left(br) < 2 + 1 + 5 + 32 + 4 + 43 + 1)
        return -1;

    ptl->profile_space = ni_bs_reader_get_bits(br, 2);
    ptl->tier_flag = ni_bs_reader_get_bits(br, 1);
    ptl->profile_idc = ni_bs_reader_get_bits(br, 5);

    for (i = 0; i < 32; i++)
    {
        ptl->profile_compatibility_flag[i] = ni_bs_reader_get_bits(br, 1);

        if (ptl->profile_idc == 0 && i > 0 &&
            ptl->profile_compatibility_flag[i])
            ptl->profile_idc = i;
    }
    ptl->progressive_source_flag = ni_bs_reader_get_bits(br, 1);
    ptl->interlaced_source_flag = ni_bs_reader_get_bits(br, 1);
    ptl->non_packed_constraint_flag = ni_bs_reader_get_bits(br, 1);
    ptl->frame_only_constraint_flag = ni_bs_reader_get_bits(br, 1);

#define check_profile_idc(idc)                                                 \
    ptl->profile_idc == (idc) || ptl->profile_compatibility_flag[idc]

    if (check_profile_idc(4) || check_profile_idc(5) || check_profile_idc(6) ||
        check_profile_idc(7) || check_profile_idc(8) || check_profile_idc(9) ||
        check_profile_idc(10))
    {
        ptl->max_12bit_constraint_flag = ni_bs_reader_get_bits(br, 1);
        ptl->max_10bit_constraint_flag = ni_bs_reader_get_bits(br, 1);
        ptl->max_8bit_constraint_flag = ni_bs_reader_get_bits(br, 1);
        ptl->max_422chroma_constraint_flag = ni_bs_reader_get_bits(br, 1);
        ptl->max_420chroma_constraint_flag = ni_bs_reader_get_bits(br, 1);
        ptl->max_monochrome_constraint_flag = ni_bs_reader_get_bits(br, 1);
        ptl->intra_constraint_flag = ni_bs_reader_get_bits(br, 1);
        ptl->one_picture_only_constraint_flag = ni_bs_reader_get_bits(br, 1);
        ptl->lower_bit_rate_constraint_flag = ni_bs_reader_get_bits(br, 1);

        if (check_profile_idc(5) || check_profile_idc(9) ||
            check_profile_idc(10))
        {
            ptl->max_14bit_constraint_flag = ni_bs_reader_get_bits(br, 1);
            ni_bs_reader_skip_bits(br, 33);   // XXX_reserved_zero_33bits[0..32]
        } else
        {
            ni_bs_reader_skip_bits(br, 34);   // XXX_reserved_zero_34bits[0..33]
        }
    } else if (check_profile_idc(2))
    {
        ni_bs_reader_skip_bits(br, 7);
        ptl->one_picture_only_constraint_flag = ni_bs_reader_get_bits(br, 1);
        ni_bs_reader_skip_bits(br, 35);   // XXX_reserved_zero_35bits[0..34]
    } else
    {
        ni_bs_reader_skip_bits(br, 43);   // XXX_reserved_zero_43bits[0..42]
    }

    if (check_profile_idc(1) || check_profile_idc(2) || check_profile_idc(3) ||
        check_profile_idc(4) || check_profile_idc(5) || check_profile_idc(9))
        ptl->inbld_flag = ni_bs_reader_get_bits(br, 1);
    else
        ni_bs_reader_skip_bits(br, 1);
#undef check_profile_idc

    return 0;
}

static int h265_parse_ptl(ni_bitstream_reader_t *br, PTL *ptl,
                          int max_num_sub_layers)
{
    int i;
    if (h265_decode_profile_tier_level(br, &ptl->general_ptl) < 0 ||
        ni_bs_reader_get_bits_left(br) <
            8 + (8 * 2 * (max_num_sub_layers - 1 > 0)))
    {
        ni_log(NI_LOG_ERROR, "PTL information too short\n");
        return -1;
    }

    ptl->general_ptl.level_idc = ni_bs_reader_get_bits(br, 8);

    for (i = 0; i < max_num_sub_layers - 1; i++)
    {
        ptl->sub_layer_profile_present_flag[i] = ni_bs_reader_get_bits(br, 1);
        ptl->sub_layer_level_present_flag[i] = ni_bs_reader_get_bits(br, 1);
    }

    if (max_num_sub_layers - 1 > 0)
        for (i = max_num_sub_layers - 1; i < 8; i++)
            ni_bs_reader_skip_bits(br, 2);   // reserved_zero_2bits[i]
    for (i = 0; i < max_num_sub_layers - 1; i++)
    {
        if (ptl->sub_layer_profile_present_flag[i] &&
            h265_decode_profile_tier_level(br, &ptl->sub_layer_ptl[i]) < 0)
        {
            ni_log(NI_LOG_ERROR, "PTL information for sublayer %i too short\n",
                   i);
            return -1;
        }
        if (ptl->sub_layer_level_present_flag[i])
        {
            if (ni_bs_reader_get_bits_left(br) < 8)
            {
                ni_log(NI_LOG_ERROR,
                       "Not enough data for sublayer %i level_idc\n", i);
                return -1;
            } else
                ptl->sub_layer_ptl[i].level_idc = ni_bs_reader_get_bits(br, 8);
        }
    }

    return 0;
}

static int h265_decode_hrd(ni_bitstream_reader_t *br, int common_inf_present,
                           int max_sublayers)
{
    int nal_params_present = 0, vcl_params_present = 0;
    int subpic_params_present = 0;
    int i;

    if (common_inf_present)
    {
        nal_params_present = ni_bs_reader_get_bits(br, 1);
        vcl_params_present = ni_bs_reader_get_bits(br, 1);

        if (nal_params_present || vcl_params_present)
        {
            subpic_params_present = ni_bs_reader_get_bits(br, 1);

            if (subpic_params_present)
            {
                ni_bs_reader_skip_bits(br, 8);   // tick_divisor_minus2
                ni_bs_reader_skip_bits(
                    br, 5);   // du_cpb_removal_delay_increment_length_minus1
                ni_bs_reader_skip_bits(
                    br, 1);   // sub_pic_cpb_params_in_pic_timing_sei_flag
                ni_bs_reader_skip_bits(
                    br, 5);   // dpb_output_delay_du_length_minus1
            }

            ni_bs_reader_skip_bits(br, 4);   // bit_rate_scale
            ni_bs_reader_skip_bits(br, 4);   // cpb_size_scale

            if (subpic_params_present)
                ni_bs_reader_skip_bits(br, 4);   // cpb_size_du_scale

            ni_bs_reader_skip_bits(
                br, 5);   // initial_cpb_removal_delay_length_minus1
            ni_bs_reader_skip_bits(br,
                                   5);   // au_cpb_removal_delay_length_minus1
            ni_bs_reader_skip_bits(br, 5);   // dpb_output_delay_length_minus1
        }
    }

    for (i = 0; i < max_sublayers; i++)
    {
        int low_delay = 0;
        unsigned int nb_cpb = 1;
        int fixed_rate = ni_bs_reader_get_bits(br, 1);

        if (!fixed_rate)
            fixed_rate = ni_bs_reader_get_bits(br, 1);

        if (fixed_rate)
            ni_bs_reader_get_ue(br);   // elemental_duration_in_tc_minus1
        else
            low_delay = ni_bs_reader_get_bits(br, 1);

        if (!low_delay)
        {
            nb_cpb = ni_bs_reader_get_ue(br) + 1;
            if (nb_cpb < 1 || nb_cpb > 32)
            {
                ni_log(NI_LOG_ERROR, "nb_cpb %d invalid\n", nb_cpb);
                return -1;
            }
        }

        if (nal_params_present)
            h265_decode_sublayer_hrd(br, nb_cpb, subpic_params_present);
        if (vcl_params_present)
            h265_decode_sublayer_hrd(br, nb_cpb, subpic_params_present);
    }
    return 0;
}

static void h265_set_default_scaling_list_data(ScalingList *sl)
{
    int matrixId;

    for (matrixId = 0; matrixId < 6; matrixId++)
    {
        // 4x4 default is 16
        memset(sl->sl[0][matrixId], 16, 16);
        sl->sl_dc[0][matrixId] = 16;   // default for 16x16
        sl->sl_dc[1][matrixId] = 16;   // default for 32x32
    }
    memcpy(sl->sl[1][0], default_scaling_list_intra, 64);
    memcpy(sl->sl[1][1], default_scaling_list_intra, 64);
    memcpy(sl->sl[1][2], default_scaling_list_intra, 64);
    memcpy(sl->sl[1][3], default_scaling_list_inter, 64);
    memcpy(sl->sl[1][4], default_scaling_list_inter, 64);
    memcpy(sl->sl[1][5], default_scaling_list_inter, 64);
    memcpy(sl->sl[2][0], default_scaling_list_intra, 64);
    memcpy(sl->sl[2][1], default_scaling_list_intra, 64);
    memcpy(sl->sl[2][2], default_scaling_list_intra, 64);
    memcpy(sl->sl[2][3], default_scaling_list_inter, 64);
    memcpy(sl->sl[2][4], default_scaling_list_inter, 64);
    memcpy(sl->sl[2][5], default_scaling_list_inter, 64);
    memcpy(sl->sl[3][0], default_scaling_list_intra, 64);
    memcpy(sl->sl[3][1], default_scaling_list_intra, 64);
    memcpy(sl->sl[3][2], default_scaling_list_intra, 64);
    memcpy(sl->sl[3][3], default_scaling_list_inter, 64);
    memcpy(sl->sl[3][4], default_scaling_list_inter, 64);
    memcpy(sl->sl[3][5], default_scaling_list_inter, 64);
}

static int h265_scaling_list_data(ni_bitstream_reader_t *br, ScalingList *sl,
                                  ni_h265_sps_t *sps)
{
    uint8_t scaling_list_pred_mode_flag;
    int32_t scaling_list_dc_coef[2][6];
    int size_id, matrix_id, pos;
    int i;

    for (size_id = 0; size_id < 4; size_id++)
        for (matrix_id = 0; matrix_id < 6;
             matrix_id += ((size_id == 3) ? 3 : 1))
        {
            scaling_list_pred_mode_flag = ni_bs_reader_get_bits(br, 1);
            if (!scaling_list_pred_mode_flag)
            {
                int delta = ni_bs_reader_get_ue(br);
                /* Only need to handle non-zero delta. Zero means default,
                 * which should already be in the arrays. */
                if (delta)
                {
                    // Copy from previous array.
                    delta *= (size_id == 3) ? 3 : 1;
                    if (matrix_id < delta)
                    {
                        ni_log(NI_LOG_ERROR,
                               "Invalid delta in scaling list data: %d.\n",
                               delta);
                        return -1;
                    }

                    memcpy(sl->sl[size_id][matrix_id],
                           sl->sl[size_id][matrix_id - delta],
                           size_id > 0 ? 64 : 16);
                    if (size_id > 1)
                        sl->sl_dc[size_id - 2][matrix_id] =
                            sl->sl_dc[size_id - 2][matrix_id - delta];
                }
            } else
            {
                int32_t next_coef, coef_num;
                int32_t scaling_list_delta_coef;

                next_coef = 8;
                coef_num = 1 << (4 + (size_id << 1));
                if (coef_num >= 64)
                    coef_num = 64;
                if (size_id > 1)
                {
                    scaling_list_dc_coef[size_id - 2][matrix_id] =
                        ni_bs_reader_get_se(br) + 8;
                    next_coef = scaling_list_dc_coef[size_id - 2][matrix_id];
                    sl->sl_dc[size_id - 2][matrix_id] = next_coef;
                }
                for (i = 0; i < coef_num; i++)
                {
                    if (size_id == 0)
                        pos =
                            4 * hevc_diag_scan4x4_y[i] + hevc_diag_scan4x4_x[i];
                    else
                        pos =
                            8 * hevc_diag_scan8x8_y[i] + hevc_diag_scan8x8_x[i];

                    scaling_list_delta_coef = ni_bs_reader_get_se(br);
                    next_coef =
                        (next_coef + 256U + scaling_list_delta_coef) % 256;
                    sl->sl[size_id][matrix_id][pos] = next_coef;
                }
            }
        }

    if (sps->chroma_format_idc == 3)
    {
        for (i = 0; i < 64; i++)
        {
            sl->sl[3][1][i] = sl->sl[2][1][i];
            sl->sl[3][2][i] = sl->sl[2][2][i];
            sl->sl[3][4][i] = sl->sl[2][4][i];
            sl->sl[3][5][i] = sl->sl[2][5][i];
        }
        sl->sl_dc[1][1] = sl->sl_dc[0][1];
        sl->sl_dc[1][2] = sl->sl_dc[0][2];
        sl->sl_dc[1][4] = sl->sl_dc[0][4];
        sl->sl_dc[1][5] = sl->sl_dc[0][5];
    }

    return 0;
}

static int h265_decode_short_term_rps(ni_bitstream_reader_t *br,
                                      ShortTermRPS *rps,
                                      const ni_h265_sps_t *sps,
                                      int is_slice_header)
{
    uint8_t rps_predict = 0;
    int32_t delta_poc;
    int k0 = 0;
    int k1 = 0;
    int32_t k = 0;
    int i;

    if (rps != sps->st_rps && sps->nb_st_rps)
        rps_predict = ni_bs_reader_get_bits(br, 1);

    if (rps_predict)
    {
        const ShortTermRPS *rps_ridx;
        int32_t delta_rps;
        int32_t abs_delta_rps;
        uint8_t use_delta_flag = 0;
        uint8_t delta_rps_sign;

        if (is_slice_header)
        {
            unsigned int delta_idx = ni_bs_reader_get_ue(br) + 1;
            if (delta_idx > sps->nb_st_rps)
            {
                ni_log(NI_LOG_ERROR,
                       "Invalid value of delta_idx in slice header RPS: %d > "
                       "%d.\n",
                       delta_idx, sps->nb_st_rps);
                return -1;
            }
            rps_ridx = &sps->st_rps[sps->nb_st_rps - delta_idx];
            rps->rps_idx_num_delta_pocs = rps_ridx->num_delta_pocs;
        } else
            rps_ridx = &sps->st_rps[rps - sps->st_rps - 1];

        delta_rps_sign = ni_bs_reader_get_bits(br, 1);
        abs_delta_rps = (int)(ni_bs_reader_get_ue(br) + 1);
        if (abs_delta_rps < 1 || abs_delta_rps > 32768)
        {
            ni_log(NI_LOG_ERROR, "Invalid value of abs_delta_rps: %d\n",
                   abs_delta_rps);
            return -1;
        }
        delta_rps = (1 - (delta_rps_sign << 1)) * abs_delta_rps;
        for (i = 0; i <= rps_ridx->num_delta_pocs; i++)
        {
            int used = rps->used[k] = ni_bs_reader_get_bits(br, 1);

            if (!used)
                use_delta_flag = ni_bs_reader_get_bits(br, 1);

            if (used || use_delta_flag)
            {
                if (i < rps_ridx->num_delta_pocs)
                    delta_poc = delta_rps + rps_ridx->delta_poc[i];
                else
                    delta_poc = delta_rps;
                rps->delta_poc[k] = delta_poc;
                if (delta_poc < 0)
                    k0++;
                else
                    k1++;
                k++;
            }
        }

        if (k >= (sizeof(rps->used) / sizeof(rps->used[0])))
        {
            ni_log(NI_LOG_ERROR, "Invalid num_delta_pocs: %d\n", k);
            return -1;
        }

        rps->num_delta_pocs = k;
        rps->num_negative_pics = k0;
        // sort in increasing order (smallest first)
        if (rps->num_delta_pocs != 0)
        {
            int used, tmp;
            for (i = 1; i < rps->num_delta_pocs; i++)
            {
                delta_poc = rps->delta_poc[i];
                used = rps->used[i];
                for (k = i - 1; k >= 0; k--)
                {
                    tmp = rps->delta_poc[k];
                    if (delta_poc < tmp)
                    {
                        rps->delta_poc[k + 1] = tmp;
                        rps->used[k + 1] = rps->used[k];
                        rps->delta_poc[k] = delta_poc;
                        rps->used[k] = used;
                    }
                }
            }
        }
        if ((rps->num_negative_pics >> 1) != 0)
        {
            int used;
            k = rps->num_negative_pics - 1;
            // flip the negative values to largest first
            for (i = 0; i < (int)(rps->num_negative_pics >> 1); i++)
            {
                delta_poc = rps->delta_poc[i];
                used = rps->used[i];
                rps->delta_poc[i] = rps->delta_poc[k];
                rps->used[i] = rps->used[k];
                rps->delta_poc[k] = delta_poc;
                rps->used[k] = used;
                k--;
            }
        }
    } else
    {
        int prev, nb_positive_pics;
        rps->num_negative_pics = ni_bs_reader_get_ue(br);
        nb_positive_pics = ni_bs_reader_get_ue(br);

        if (rps->num_negative_pics >= HEVC_MAX_REFS ||
            nb_positive_pics >= HEVC_MAX_REFS)
        {
            ni_log(NI_LOG_ERROR, "Too many refs in a short term RPS.\n");
            return -1;
        }

        rps->num_delta_pocs = (int)(rps->num_negative_pics + nb_positive_pics);
        if (rps->num_delta_pocs)
        {
            prev = 0;
            for (i = 0; i < (int)rps->num_negative_pics; i++)
            {
                delta_poc = ni_bs_reader_get_ue(br) + 1;
                if (delta_poc < 1 || delta_poc > 32768)
                {
                    ni_log(NI_LOG_ERROR, "Invalid value of delta_poc: %d\n",
                           delta_poc);
                    return -1;
                }
                prev -= delta_poc;
                rps->delta_poc[i] = prev;
                rps->used[i] = ni_bs_reader_get_bits(br, 1);
            }
            prev = 0;
            for (i = 0; i < nb_positive_pics; i++)
            {
                delta_poc = ni_bs_reader_get_ue(br) + 1;
                if (delta_poc < 1 || delta_poc > 32768)
                {
                    ni_log(NI_LOG_ERROR, "Invalid value of delta_poc: %d\n",
                           delta_poc);
                    return -1;
                }
                prev += delta_poc;
                rps->delta_poc[rps->num_negative_pics + i] = prev;
                rps->used[rps->num_negative_pics + i] =
                    ni_bs_reader_get_bits(br, 1);
            }
        }
    }
    return 0;
}

static int h265_decode_vui(ni_bitstream_reader_t *br, int apply_defdispwin,
                           ni_h265_sps_t *sps)
{
    VUI backup_vui, *vui = &sps->vui;
    ni_bitstream_reader_t br_backup;
    int sar_present, alt = 0;

    sar_present = ni_bs_reader_get_bits(br, 1);
    if (sar_present)
    {
        uint8_t sar_idx = ni_bs_reader_get_bits(br, 8);
        if (sar_idx < (sizeof(vui_sar) / sizeof(vui_sar[0])))
            vui->sar = vui_sar[sar_idx];
        else if (sar_idx == 255)
        {
            vui->sar.num = ni_bs_reader_get_bits(br, 16);
            vui->sar.den = ni_bs_reader_get_bits(br, 16);
        } else
        {
            ni_log(NI_LOG_ERROR, "Unknown SAR Index: %u.\n", sar_idx);
        }
    }

    vui->overscan_info_present_flag = ni_bs_reader_get_bits(br, 1);
    if (vui->overscan_info_present_flag)
        vui->overscan_appropriate_flag = ni_bs_reader_get_bits(br, 1);

    vui->video_signal_type_present_flag = ni_bs_reader_get_bits(br, 1);
    if (vui->video_signal_type_present_flag)
    {
        vui->video_format = ni_bs_reader_get_bits(br, 3);
        vui->video_full_range_flag = ni_bs_reader_get_bits(br, 1);
        vui->colour_description_present_flag = ni_bs_reader_get_bits(br, 1);
        if (vui->video_full_range_flag && sps->pix_fmt == NI_PIX_FMT_YUV420P)
            sps->pix_fmt = NI_PIX_FMT_YUV420P;
        if (vui->colour_description_present_flag)
        {
            vui->colour_primaries = ni_bs_reader_get_bits(br, 8);
            vui->transfer_characteristic = ni_bs_reader_get_bits(br, 8);
            vui->matrix_coeffs = ni_bs_reader_get_bits(br, 8);

            if (vui->colour_primaries >= NI_COL_PRI_NB)
            {
                vui->colour_primaries = NI_COL_PRI_UNSPECIFIED;
            }
            if (vui->transfer_characteristic >= NI_COL_TRC_NB)
            {
                vui->transfer_characteristic = NI_COL_TRC_UNSPECIFIED;
            }
            if (vui->matrix_coeffs >= NI_COL_SPC_NB)
            {
                vui->matrix_coeffs = NI_COL_SPC_UNSPECIFIED;
            }
            if (vui->matrix_coeffs == NI_COL_SPC_RGB)
            {
                if (sps->pix_fmt)
                {
                    ni_log(NI_LOG_ERROR,
                           "Invalid format, only support yuv420p\n");
                    return -1;
                }
            }
        }
    }

    vui->chroma_loc_info_present_flag = ni_bs_reader_get_bits(br, 1);
    if (vui->chroma_loc_info_present_flag)
    {
        vui->chroma_sample_loc_type_top_field = ni_bs_reader_get_ue(br);
        vui->chroma_sample_loc_type_bottom_field = ni_bs_reader_get_ue(br);
    }

    vui->neutra_chroma_indication_flag = ni_bs_reader_get_bits(br, 1);
    vui->field_seq_flag = ni_bs_reader_get_bits(br, 1);
    vui->frame_field_info_present_flag = ni_bs_reader_get_bits(br, 1);

    // Backup context in case an alternate header is detected
    memcpy(&br_backup, br, sizeof(br_backup));
    memcpy(&backup_vui, vui, sizeof(backup_vui));
    vui->default_display_window_flag = ni_bs_reader_get_bits(br, 1);

    if (vui->default_display_window_flag)
    {
        int vert_mult = hevc_sub_height_c[sps->chroma_format_idc];
        int horiz_mult = hevc_sub_width_c[sps->chroma_format_idc];
        vui->def_disp_win.left_offset = ni_bs_reader_get_ue(br) * horiz_mult;
        vui->def_disp_win.right_offset = ni_bs_reader_get_ue(br) * horiz_mult;
        vui->def_disp_win.top_offset = ni_bs_reader_get_ue(br) * vert_mult;
        vui->def_disp_win.bottom_offset = ni_bs_reader_get_ue(br) * vert_mult;

        if (apply_defdispwin)
        {
            ni_log(NI_LOG_DEBUG,
                   "discarding vui default display window, "
                   "original values are l:%u r:%u t:%u b:%u\n",
                   vui->def_disp_win.left_offset,
                   vui->def_disp_win.right_offset, vui->def_disp_win.top_offset,
                   vui->def_disp_win.bottom_offset);

            vui->def_disp_win.left_offset = vui->def_disp_win.right_offset =
                vui->def_disp_win.top_offset = vui->def_disp_win.bottom_offset =
                    0;
        }
    }

timing_info:
    vui->vui_timing_info_present_flag = ni_bs_reader_get_bits(br, 1);

    if (vui->vui_timing_info_present_flag)
    {
        if (ni_bs_reader_get_bits_left(br) < 66 && !alt)
        {
            // The alternate syntax seem to have timing info located
            // at where def_disp_win is normally located
            ni_log(NI_LOG_INFO,
                   "Strange VUI timing information, retrying...\n");
            memcpy(vui, &backup_vui, sizeof(backup_vui));
            memcpy(br, &br_backup, sizeof(br_backup));
            alt = 1;
            goto timing_info;
        }
        vui->vui_num_units_in_tick = ni_bs_reader_get_bits(br, 32);
        vui->vui_time_scale = ni_bs_reader_get_bits(br, 32);
        if (alt)
        {
            ni_log(NI_LOG_INFO, "Retry got %u/%ufps\n", vui->vui_time_scale,
                   vui->vui_num_units_in_tick);
        }
        vui->vui_poc_proportional_to_timing_flag = ni_bs_reader_get_bits(br, 1);
        if (vui->vui_poc_proportional_to_timing_flag)
            vui->vui_num_ticks_poc_diff_one_minus1 = ni_bs_reader_get_ue(br);
        vui->vui_hrd_parameters_present_flag = ni_bs_reader_get_bits(br, 1);
        if (vui->vui_hrd_parameters_present_flag)
            h265_decode_hrd(br, 1, sps->max_sub_layers);
    }

    vui->bitstream_restriction_flag = ni_bs_reader_get_bits(br, 1);
    if (vui->bitstream_restriction_flag)
    {
        if (ni_bs_reader_get_bits_left(br) < 8 && !alt)
        {
            ni_log(NI_LOG_INFO,
                   "Strange VUI bitstream restriction information, retrying"
                   " from timing information...\n");
            memcpy(vui, &backup_vui, sizeof(backup_vui));
            memcpy(br, &br_backup, sizeof(br_backup));
            alt = 1;
            goto timing_info;
        }
        vui->tiles_fixed_structure_flag = ni_bs_reader_get_bits(br, 1);
        vui->motion_vectors_over_pic_boundaries_flag =
            ni_bs_reader_get_bits(br, 1);
        vui->restricted_ref_pic_lists_flag = ni_bs_reader_get_bits(br, 1);
        vui->min_spatial_segmentation_idc = ni_bs_reader_get_ue(br);
        vui->max_bytes_per_pic_denom = ni_bs_reader_get_ue(br);
        vui->max_bits_per_min_cu_denom = ni_bs_reader_get_ue(br);
        vui->log2_max_mv_length_horizontal = ni_bs_reader_get_ue(br);
        vui->log2_max_mv_length_vertical = ni_bs_reader_get_ue(br);
    }

    if (ni_bs_reader_get_bits_left(br) < 1 && !alt)
    {
        ni_log(NI_LOG_INFO,
               "Overread in VUI, retrying from timing information...\n");
        memcpy(vui, &backup_vui, sizeof(backup_vui));
        memcpy(br, &br_backup, sizeof(br_backup));
        alt = 1;
        goto timing_info;
    }
    return 0;
}

static int h265_parse_sps(ni_h265_sps_t *sps, uint8_t *buf, int size_bytes)
{
    ni_h265_window_t *ow;
    int ret = 0;
    int log2_diff_max_min_transform_block_size;
    int bit_depth_chroma, start, vui_present, sublayer_ordering_info;
    int i;

    ni_bitstream_reader_t br;
    uint32_t sps_id;
    ni_bitstream_reader_init(&br, buf, 8 * size_bytes);

    ni_bs_reader_skip_bits(&br, 16);   // skip NAL header

    sps->vps_id = ni_bs_reader_get_bits(&br, 4);

    sps->max_sub_layers = (int)ni_bs_reader_get_bits(&br, 3) + 1;
    if (sps->max_sub_layers > HEVC_MAX_SUB_LAYERS)
    {
        ni_log(NI_LOG_ERROR, "sps_max_sub_layers out of range: %d\n",
               sps->max_sub_layers);
        return -1;
    }

    sps->temporal_id_nesting_flag = ni_bs_reader_get_bits(&br, 1);

    if ((ret = h265_parse_ptl(&br, &sps->ptl, sps->max_sub_layers)) < 0)
        return ret;

    sps_id = ni_bs_reader_get_ue(&br);
    if (sps_id >= HEVC_MAX_SPS_COUNT)
    {
        ni_log(NI_LOG_ERROR, "SPS id out of range: %d\n", sps_id);
        return -1;
    }

    sps->chroma_format_idc = ni_bs_reader_get_ue(&br);
    if (sps->chroma_format_idc > 3U)
    {
        ni_log(NI_LOG_ERROR, "chroma_format_idc %d is invalid\n",
               sps->chroma_format_idc);
        return -1;
    }

    if (sps->chroma_format_idc == 3)
        sps->separate_colour_plane_flag = ni_bs_reader_get_bits(&br, 1);

    if (sps->separate_colour_plane_flag)
        sps->chroma_format_idc = 0;

    sps->width = (int)ni_bs_reader_get_ue(&br);
    sps->height = (int)ni_bs_reader_get_ue(&br);

    if (ni_bs_reader_get_bits(&br, 1))
    {   // pic_conformance_flag
        int vert_mult = hevc_sub_height_c[sps->chroma_format_idc];
        int horiz_mult = hevc_sub_width_c[sps->chroma_format_idc];
        sps->pic_conf_win.left_offset = ni_bs_reader_get_ue(&br) * horiz_mult;
        sps->pic_conf_win.right_offset = ni_bs_reader_get_ue(&br) * horiz_mult;
        sps->pic_conf_win.top_offset = ni_bs_reader_get_ue(&br) * vert_mult;
        sps->pic_conf_win.bottom_offset = ni_bs_reader_get_ue(&br) * vert_mult;

        sps->output_window = sps->pic_conf_win;
    }

    sps->bit_depth = (int)(ni_bs_reader_get_ue(&br) + 8);
    bit_depth_chroma = (int)(ni_bs_reader_get_ue(&br) + 8);
    if (sps->chroma_format_idc && bit_depth_chroma != sps->bit_depth)
    {
        ni_log(NI_LOG_ERROR,
               "Luma bit depth (%d) is different from chroma bit depth (%d), "
               "this is unsupported.\n",
               sps->bit_depth, bit_depth_chroma);
        return -1;
    }
    sps->bit_depth_chroma = bit_depth_chroma;
    if (((sps->bit_depth != 8) && (sps->bit_depth != 10)) ||
        (sps->chroma_format_idc != 1))
    {
        ni_log(NI_LOG_ERROR,
               "only support 8bit/10bit yuv420p, bit_depth %d, "
               "chroma_format_idc %d\n",
               sps->bit_depth, sps->chroma_format_idc);
        return -1;
    }
    sps->pix_fmt = 0;
    sps->hshift[0] = sps->vshift[0] = 0;
    sps->hshift[2] = sps->hshift[1] = 1;
    sps->vshift[2] = sps->vshift[1] = 1;
    sps->pixel_shift = sps->bit_depth > 8;

    sps->log2_max_poc_lsb = ni_bs_reader_get_ue(&br) + 4;
    if (sps->log2_max_poc_lsb > 16)
    {
        ni_log(NI_LOG_ERROR,
               "log2_max_pic_order_cnt_lsb_minus4 out range: %d\n",
               sps->log2_max_poc_lsb - 4);
        return -1;
    }

    sublayer_ordering_info = ni_bs_reader_get_bits(&br, 1);
    start = sublayer_ordering_info ? 0 : sps->max_sub_layers - 1;
    for (i = start; i < sps->max_sub_layers; i++)
    {
        sps->temporal_layer[i].max_dec_pic_buffering =
            (int)(ni_bs_reader_get_ue(&br) + 1);
        sps->temporal_layer[i].num_reorder_pics = (int)ni_bs_reader_get_ue(&br);
        sps->temporal_layer[i].max_latency_increase =
            (int)(ni_bs_reader_get_ue(&br) - 1);
        if (sps->temporal_layer[i].num_reorder_pics >
            sps->temporal_layer[i].max_dec_pic_buffering - 1)
        {
            ni_log(NI_LOG_ERROR, "sps_max_num_reorder_pics out of range: %d\n",
                   sps->temporal_layer[i].num_reorder_pics);
            sps->temporal_layer[i].max_dec_pic_buffering =
                sps->temporal_layer[i].num_reorder_pics + 1;
        }
    }

    if (!sublayer_ordering_info)
    {
        for (i = 0; i < start; i++)
        {
            sps->temporal_layer[i].max_dec_pic_buffering =
                sps->temporal_layer[start].max_dec_pic_buffering;
            sps->temporal_layer[i].num_reorder_pics =
                sps->temporal_layer[start].num_reorder_pics;
            sps->temporal_layer[i].max_latency_increase =
                sps->temporal_layer[start].max_latency_increase;
        }
    }

    sps->log2_min_cb_size = ni_bs_reader_get_ue(&br) + 3;
    sps->log2_diff_max_min_coding_block_size = ni_bs_reader_get_ue(&br);
    sps->log2_min_tb_size = ni_bs_reader_get_ue(&br) + 2;
    log2_diff_max_min_transform_block_size = ni_bs_reader_get_ue(&br);
    sps->log2_max_trafo_size =
        log2_diff_max_min_transform_block_size + sps->log2_min_tb_size;

    if (sps->log2_min_cb_size < 3 || sps->log2_min_cb_size > 30)
    {
        ni_log(NI_LOG_ERROR, "Invalid value %d for log2_min_cb_size",
               sps->log2_min_cb_size);
        return -1;
    }

    if (sps->log2_diff_max_min_coding_block_size > 30)
    {
        ni_log(NI_LOG_ERROR,
               "Invalid value %d for log2_diff_max_min_coding_block_size",
               sps->log2_diff_max_min_coding_block_size);
        return -1;
    }

    if (sps->log2_min_tb_size >= sps->log2_min_cb_size ||
        sps->log2_min_tb_size < 2)
    {
        ni_log(NI_LOG_ERROR, "Invalid value for log2_min_tb_size");
        return -1;
    }

    if (log2_diff_max_min_transform_block_size < 0 ||
        log2_diff_max_min_transform_block_size > 30)
    {
        ni_log(NI_LOG_ERROR,
               "Invalid value %d for log2_diff_max_min_transform_block_size",
               log2_diff_max_min_transform_block_size);
        return -1;
    }

    sps->max_transform_hierarchy_depth_inter = ni_bs_reader_get_ue(&br);
    sps->max_transform_hierarchy_depth_intra = ni_bs_reader_get_ue(&br);

    sps->scaling_list_enable_flag = ni_bs_reader_get_bits(&br, 1);
    if (sps->scaling_list_enable_flag)
    {
        h265_set_default_scaling_list_data(&sps->scaling_list);

        if (ni_bs_reader_get_bits(&br, 1))
        {
            ret = h265_scaling_list_data(&br, &sps->scaling_list, sps);
            if (ret < 0)
                return ret;
        }
    }

    sps->amp_enabled_flag = ni_bs_reader_get_bits(&br, 1);
    sps->sao_enabled = ni_bs_reader_get_bits(&br, 1);

    sps->pcm_enabled_flag = ni_bs_reader_get_bits(&br, 1);
    if (sps->pcm_enabled_flag)
    {
        sps->pcm.bit_depth = ni_bs_reader_get_bits(&br, 4) + 1;
        sps->pcm.bit_depth_chroma = ni_bs_reader_get_bits(&br, 4) + 1;
        sps->pcm.log2_min_pcm_cb_size = ni_bs_reader_get_ue(&br) + 3;
        sps->pcm.log2_max_pcm_cb_size =
            sps->pcm.log2_min_pcm_cb_size + ni_bs_reader_get_ue(&br);
        if ((sps->pcm.bit_depth > sps->bit_depth) ||
            (sps->pcm.bit_depth_chroma > sps->bit_depth))
        {
            ni_log(NI_LOG_ERROR,
                   "PCM bit depth (%d, %d) is greater than normal bit depth "
                   "(%d)\n",
                   sps->pcm.bit_depth, sps->pcm.bit_depth_chroma,
                   sps->bit_depth);
            return -1;
        }

        sps->pcm.loop_filter_disable_flag = ni_bs_reader_get_bits(&br, 1);
    }

    sps->nb_st_rps = ni_bs_reader_get_ue(&br);
    if (sps->nb_st_rps > HEVC_MAX_SHORT_TERM_REF_PIC_SETS)
    {
        ni_log(NI_LOG_ERROR, "Too many short term RPS: %d.\n", sps->nb_st_rps);
        return -1;
    }
    for (i = 0; i < (int)sps->nb_st_rps; i++)
    {
        if ((ret = h265_decode_short_term_rps(&br, &sps->st_rps[i], sps, 0)) <
            0)
            return ret;
    }

    sps->long_term_ref_pics_present_flag = ni_bs_reader_get_bits(&br, 1);
    if (sps->long_term_ref_pics_present_flag)
    {
        sps->num_long_term_ref_pics_sps = ni_bs_reader_get_ue(&br);
        if (sps->num_long_term_ref_pics_sps > HEVC_MAX_LONG_TERM_REF_PICS)
        {
            ni_log(NI_LOG_ERROR, "Too many long term ref pics: %d.\n",
                   sps->num_long_term_ref_pics_sps);
            return -1;
        }
        for (i = 0; i < sps->num_long_term_ref_pics_sps; i++)
        {
            sps->lt_ref_pic_poc_lsb_sps[i] =
                ni_bs_reader_get_bits(&br, (int)sps->log2_max_poc_lsb);
            sps->used_by_curr_pic_lt_sps_flag[i] =
                ni_bs_reader_get_bits(&br, 1);
        }
    }

    sps->sps_temporal_mvp_enabled_flag = ni_bs_reader_get_bits(&br, 1);
    sps->sps_strong_intra_smoothing_enable_flag = ni_bs_reader_get_bits(&br, 1);
    sps->vui.sar = (ni_rational_t){0, 1};
    vui_present = ni_bs_reader_get_bits(&br, 1);
    if (vui_present)
        h265_decode_vui(&br, 0, sps);

    if (ni_bs_reader_get_bits(&br, 1))
    {   // sps_extension_flag
        sps->sps_range_extension_flag = ni_bs_reader_get_bits(&br, 1);
        ni_bs_reader_skip_bits(
            &br, 7);   //sps_extension_7bits = ni_bs_reader_get_bits(br, 7);
        if (sps->sps_range_extension_flag)
        {
            sps->transform_skip_rotation_enabled_flag =
                ni_bs_reader_get_bits(&br, 1);
            sps->transform_skip_context_enabled_flag =
                ni_bs_reader_get_bits(&br, 1);
            sps->implicit_rdpcm_enabled_flag = ni_bs_reader_get_bits(&br, 1);

            sps->explicit_rdpcm_enabled_flag = ni_bs_reader_get_bits(&br, 1);

            sps->extended_precision_processing_flag =
                ni_bs_reader_get_bits(&br, 1);
            if (sps->extended_precision_processing_flag)
                ni_log(
                    NI_LOG_INFO,
                    "extended_precision_processing_flag not yet implemented\n");

            sps->intra_smoothing_disabled_flag = ni_bs_reader_get_bits(&br, 1);
            sps->high_precision_offsets_enabled_flag =
                ni_bs_reader_get_bits(&br, 1);
            if (sps->high_precision_offsets_enabled_flag)
                ni_log(NI_LOG_INFO,
                       "high_precision_offsets_enabled_flag not yet "
                       "implemented\n");

            sps->persistent_rice_adaptation_enabled_flag =
                ni_bs_reader_get_bits(&br, 1);

            sps->cabac_bypass_alignment_enabled_flag =
                ni_bs_reader_get_bits(&br, 1);
            if (sps->cabac_bypass_alignment_enabled_flag)
                ni_log(NI_LOG_INFO,
                       "cabac_bypass_alignment_enabled_flag not yet "
                       "implemented\n");
        }
    }

    ow = &sps->output_window;
    if (ow->left_offset >= INT32_MAX - ow->right_offset ||
        ow->top_offset >= INT32_MAX - ow->bottom_offset ||
        ow->left_offset + ow->right_offset >= (uint32_t)sps->width ||
        ow->top_offset + ow->bottom_offset >= (uint32_t)sps->height)
    {
        ni_log(NI_LOG_INFO, "Invalid cropping offsets: %u/%u/%u/%u\n",
               ow->left_offset, ow->right_offset, ow->top_offset,
               ow->bottom_offset);
        ni_log(NI_LOG_INFO, "Displaying the whole video surface.\n");
        memset(ow, 0, sizeof(*ow));
        memset(&sps->pic_conf_win, 0, sizeof(sps->pic_conf_win));
    }

    // Inferred parameters
    sps->log2_ctb_size =
        sps->log2_min_cb_size + sps->log2_diff_max_min_coding_block_size;
    sps->log2_min_pu_size = sps->log2_min_cb_size - 1;

    if (sps->log2_ctb_size > HEVC_MAX_LOG2_CTB_SIZE)
    {
        ni_log(NI_LOG_ERROR, "CTB size out of range: 2^%d\n",
               sps->log2_ctb_size);
        return -1;
    }
    if (sps->log2_ctb_size < 4)
    {
        ni_log(
            NI_LOG_ERROR,
            "log2_ctb_size %d differs from the bounds of any known profile\n",
            sps->log2_ctb_size);
        return -1;
    }

    sps->ctb_width =
        (sps->width + (1 << sps->log2_ctb_size) - 1) >> sps->log2_ctb_size;
    sps->ctb_height =
        (sps->height + (1 << sps->log2_ctb_size) - 1) >> sps->log2_ctb_size;
    sps->ctb_size = sps->ctb_width * sps->ctb_height;

    sps->min_cb_width = sps->width >> sps->log2_min_cb_size;
    sps->min_cb_height = sps->height >> sps->log2_min_cb_size;
    sps->min_tb_width = sps->width >> sps->log2_min_tb_size;
    sps->min_tb_height = sps->height >> sps->log2_min_tb_size;
    sps->min_pu_width = sps->width >> sps->log2_min_pu_size;
    sps->min_pu_height = sps->height >> sps->log2_min_pu_size;
    sps->tb_mask = (1 << (sps->log2_ctb_size - sps->log2_min_tb_size)) - 1;

    sps->qp_bd_offset = 6 * (sps->bit_depth - 8);

    if ((sps->width & ((1U << sps->log2_min_cb_size) - 1)) ||
        (sps->height & ((1U << sps->log2_min_cb_size) - 1)))
    {
        ni_log(NI_LOG_ERROR, "Invalid coded frame dimensions.\n");
        return -1;
    }

    if (sps->max_transform_hierarchy_depth_inter >
        (int)(sps->log2_ctb_size - sps->log2_min_tb_size))
    {
        ni_log(NI_LOG_ERROR,
               "max_transform_hierarchy_depth_inter out of range: %d\n",
               sps->max_transform_hierarchy_depth_inter);
        return -1;
    }
    if (sps->max_transform_hierarchy_depth_intra >
        (int)(sps->log2_ctb_size - sps->log2_min_tb_size))
    {
        ni_log(NI_LOG_ERROR,
               "max_transform_hierarchy_depth_intra out of range: %d\n",
               sps->max_transform_hierarchy_depth_intra);
        return -1;
    }
    if ((sps->log2_max_trafo_size > sps->log2_ctb_size) &&
        (sps->log2_max_trafo_size > 5))
    {
        ni_log(NI_LOG_ERROR, "max transform block size out of range: %d\n",
               sps->log2_max_trafo_size);
        return -1;
    }
    if (ni_bs_reader_get_bits_left(&br) < 0)
    {
        ni_log(NI_LOG_ERROR, "Overread SPS by %d bits\n",
               -ni_bs_reader_get_bits_left(&br));
        return -1;
    }

    return 0;
}

// probe h.265 stream info; return 0 if stream can be decoded, -1 otherwise
static int probe_h265_stream_info(ni_h265_sps_t *sps)
{
    int ret = -1;
    uint8_t *buf = NULL;
    uint8_t *p_buf;
    uint32_t nal_size, ep3_removed = 0, vcl_nal_count = 0;
    int nal_type = -1;
    int sps_parsed = 0;

    if (NULL == (buf = calloc(1, NI_MAX_TX_SZ)))
    {
        ni_log(NI_LOG_ERROR,
               "Error probe_h265_stream_info: allocate stream buf failed\n");
        goto end;
    }

    reset_data_buf_pos();
    // probe at most 100 VCL before stops
    while ((!sps_parsed) && vcl_nal_count < 100 &&
           (nal_size = find_h265_next_nalu(buf, &nal_type)) > 0)
    {
        p_buf = buf;

        // skip the start code
        while (!(p_buf[0] == 0x00 && p_buf[1] == 0x00 && p_buf[2] == 0x01) &&
               (nal_size > 3))
        {
            p_buf++;
            nal_size--;
        }
        if (nal_size <= 3)
        {
            ni_log(NI_LOG_ERROR,
                   "Error probe_h265_stream_info NAL has no header\n");
            continue;
        }

        p_buf += 3;   // skip start code
        nal_size -= 3;
        ep3_removed = ni_remove_emulation_prevent_bytes(p_buf, nal_size);
        nal_size -= ep3_removed;
        ni_log(NI_LOG_DEBUG, "nal %d  nal_size %d\n", nal_type, nal_size);

        if (HEVC_NAL_SPS == nal_type && !sps_parsed)
        {
            if (vcl_nal_count > 0)
            {
                ni_log(NI_LOG_INFO,
                       "Warning: %s has %d slice NAL units ahead of SPS!\n",
                       __func__, vcl_nal_count);
            }

            if (h265_parse_sps(sps, p_buf, nal_size))
            {
                ni_log(NI_LOG_ERROR,
                       "probe_h265_stream_info: parse_sps error\n");
                break;
            }
            sps_parsed = 1;
        } else if (nal_type < 32)
        {
            vcl_nal_count++;
        }
    }

    reset_data_buf_pos();
    if (sps_parsed)
    {
        ret = 0;
    } else
    {
        ni_log(NI_LOG_ERROR, "probing failed.\n");
    }

end:
    free(buf);
    buf = NULL;
    return ret;
}

static uint64_t find_vp9_next_packet(uint8_t *p_dst, ni_vp9_header_info_t *vp9_info)
{
    uint64_t data_size;
    uint64_t i = curr_found_pos ? curr_found_pos : vp9_info->header_length;
    if (i + 12 >= total_file_size)
    {
        ni_log(NI_LOG_DEBUG,
               "%s reaching end, curr_pos %llu "
               "total input size %llu\n",
               __func__, (unsigned long long)curr_found_pos, (unsigned long long)total_file_size);

        if (g_repeat > 1)
        {
            g_repeat--;
            ni_log(NI_LOG_DEBUG, "input processed, %d left\n", g_repeat);
            reset_data_buf_pos();
            i = vp9_info->header_length;
        } else {
            return 0;
        }
    }
    /** packet structure: 
     * bytes 0-3: size of frame in bytes (not including the 12-byte header)
     * bytes 4-11: 64-bit presentation timestamp
     * bytes 12.. frame data
     */
    data_size =
        ((g_curr_cache_pos[i]) + (g_curr_cache_pos[i + 1] << 8) +
         (g_curr_cache_pos[i + 2] << 16) + (g_curr_cache_pos[i + 3] << 24));
    ni_log(NI_LOG_DEBUG, "vp9 packet data_size %u\n", data_size);
    i += 12;
    curr_found_pos = i;
    if (i + data_size > total_file_size)
    {
        data_size = total_file_size - i;
        memcpy(p_dst, &g_curr_cache_pos[i], data_size);
        curr_found_pos = total_file_size;
        return data_size;
    }

    memcpy(p_dst, &g_curr_cache_pos[i], data_size);
    curr_found_pos = i + data_size;   // point to the start of data packet
    return data_size;
}

static int vp9_parse_header(ni_vp9_header_info_t *vp9_info, uint8_t *buf,
                            int size_bytes)
{
    ni_bitstream_reader_t br;
    ni_bitstream_reader_init(&br, buf, 8 * size_bytes);

    ni_bs_reader_skip_bits(&br, 32);   // skip signature
    ni_bs_reader_skip_bits(&br, 16);   // skip version

    vp9_info->header_length = ni_bs_reader_get_bits(&br, 8);
    vp9_info->header_length |= ni_bs_reader_get_bits(&br, 8) << 8;

    ni_bs_reader_skip_bits(&br, 32);   // skip codec fucc

    vp9_info->width = ni_bs_reader_get_bits(&br, 8);
    vp9_info->width |= ni_bs_reader_get_bits(&br, 8) << 8;

    vp9_info->height = ni_bs_reader_get_bits(&br, 8);
    vp9_info->height |= ni_bs_reader_get_bits(&br, 8) << 8;

    vp9_info->timebase.den = ni_bs_reader_get_bits(&br, 8);
    vp9_info->timebase.den |= ni_bs_reader_get_bits(&br, 8) << 8;
    vp9_info->timebase.den |= ni_bs_reader_get_bits(&br, 8) << 16;
    vp9_info->timebase.den |= ni_bs_reader_get_bits(&br, 8) << 24;

    vp9_info->timebase.num = ni_bs_reader_get_bits(&br, 8);
    vp9_info->timebase.num |= ni_bs_reader_get_bits(&br, 8) << 8;
    vp9_info->timebase.num |= ni_bs_reader_get_bits(&br, 8) << 16;
    vp9_info->timebase.num |= ni_bs_reader_get_bits(&br, 8) << 24;

    vp9_info->total_frames = ni_bs_reader_get_bits(&br, 8);
    vp9_info->total_frames |= ni_bs_reader_get_bits(&br, 8) << 8;
    vp9_info->total_frames |= ni_bs_reader_get_bits(&br, 8) << 16;
    vp9_info->total_frames |= ni_bs_reader_get_bits(&br, 8) << 24;

    if (vp9_info->header_length != 32)
    {
        ni_log(NI_LOG_ERROR, "Parse faled: header_length %d != 32\n",
               vp9_info->header_length);
        return -1;
    }
    ni_bs_reader_skip_bits(&br, 32);   // unused bytes
    // here we skip frame header(12 bytes) to get profile
    ni_bs_reader_skip_bits(&br, 8 * 12);
    if (ni_bs_reader_get_bits(&br, 2) != 0x2)   // frame marker
    {
        ni_log(NI_LOG_ERROR, "Invalid frame marker\n");
        return -1;
    }
    int profile = 0;
    profile = ni_bs_reader_get_bits(&br, 1);
    profile |= ni_bs_reader_get_bits(&br, 1) << 1;
    if ((profile != 0) && (profile != 2))
    {
        ni_log(
            NI_LOG_ERROR,
            "Only support profile0(yuv420,8bit) and profile2(yuv420, 10bit)\n");
        return -1;
    }
    vp9_info->profile = profile;
    return 0;
}

// probe vp9 stream info; return 0 if stream can be decoded, -1 otherwise
static int probe_vp9_stream_info(ni_vp9_header_info_t *vp9_info)
{
    int ret = -1;
    uint8_t *buf = NULL;

    if (NULL == (buf = calloc(1, 64)))
    {
        ni_log(NI_LOG_ERROR,
               "Error probe_vp9_stream_info: allocate stream buf failed\n");
        goto end;
    }

    reset_data_buf_pos();
    uint32_t size_bytes = 64;
    if (32 + 12 + 1 >= total_file_size)
    {
        ni_log(NI_LOG_ERROR, "No frame data probed!\n");
        goto end;
    } else
    {
        if (size_bytes > total_file_size)
            size_bytes = total_file_size;
        memcpy(buf, &g_curr_cache_pos[curr_found_pos], size_bytes);
    }

    ret = vp9_parse_header(vp9_info, buf, size_bytes);
    if (ret)
    {
        ni_log(NI_LOG_ERROR, "Failed to parse vp9 header info\n");
        goto end;
    }
    reset_data_buf_pos();
    curr_found_pos +=
        vp9_info->header_length;   // packets data starts after ivf file header

end:
    free(buf);
    buf = NULL;
    return ret;
}

static int write_dmabuf_data(uint8_t *pdata, FILE *fp, int width, int height,
                             int format)
{
    if (!pdata || !fp)
        return -1;

    switch (format)
    {
        case GC620_I420:
        {
            int write_width = width;
            int write_height = height;
            int plane_width;
            int plane_height;
            int j;
            uint8_t *src = pdata;

            // write Y stride
            if (fwrite(pdata, write_width * write_height, 1, fp) != 1)
            {
                ni_log(NI_LOG_ERROR, "Error: writing Y stride error!\n");
                ni_log(NI_LOG_ERROR, "Error: ferror rc = %d\n", ferror(fp));
                return -1;
            }

            int width_aligned = (((width + 127) / 128) * 128);
            int height_aligned = ((height + 1) / 2) * 2;
            int luma_size = width_aligned * height_aligned;
            pdata = src + luma_size;

            // write U stride
            plane_height = height / 2;
            plane_width = (((int)(write_width) / 2 + 127) / 128) * 128;
            write_height /= 2;
            write_width /= 2;
            for (j = 0; j < plane_height; j++)
            {
                if (j < write_height && fwrite(pdata, write_width, 1, fp) != 1)
                {
                    ni_log(NI_LOG_ERROR,
                            "Error: writing U stride: height %d error!\n",
                            height);
                    ni_log(NI_LOG_ERROR, "Error: ferror rc = %d\n", ferror(fp));
                    return -1;
                }
                pdata += plane_width;
            }

            int chroma_b_size;
            int chroma_width_aligned = ((((width / 2) + 127) / 128) * 128);
            int chroma_height_aligned = height_aligned / 2;
            chroma_b_size = chroma_width_aligned * chroma_height_aligned;

            // write V stride
            pdata = src + luma_size + chroma_b_size;

            for (j = 0; j < plane_height; j++)
            {
                if (j < write_height && fwrite(pdata, write_width, 1, fp) != 1)
                {
                    ni_log(NI_LOG_ERROR,
                            "Error: writing V stride: height %d error!\n",
                            height);
                    ni_log(NI_LOG_ERROR, "Error: ferror rc = %d\n", ferror(fp));
                    return -1;
                }
                pdata += plane_width;
            }
            break;
        }
        case GC620_RGBA8888:
        {
            if (fwrite(pdata, width * height * 4, 1, fp) != 1)
            {
                ni_log(NI_LOG_ERROR, "Error: writing rgba data error!\n");
                ni_log(NI_LOG_ERROR, "Error: ferror rc = %d\n", ferror(fp));
                return -1;
            }
            break;
        }
        case GC620_RGB888_PLANAR:
        {
            int stride_size = NI_VPU_ALIGN32(width * height);
            for (int i = 0; i < 3; i++)
            {
                if (fwrite(pdata, width * height, 1, fp) != 1)
                {
                    ni_log(NI_LOG_ERROR, "Error: writing bgrp data error!\n");
                    ni_log(NI_LOG_ERROR, "Error: ferror rc = %d\n", ferror(fp));
                    return -1;
                }
                pdata += stride_size;
            }
            break;
        }

        default:
            break;
    }

    if (fflush(fp))
    {
        ni_log(NI_LOG_ERROR, "Error: writing data frame flush failed! errno %d\n",
                errno);
    }

    return 0;
}

// Note we do not need to consider padding bytes from yuv/rgba file reading
static inline int frame_read_buffer_size(int w, int h, ni_pix_fmt_t pix_fmt,
                                         ni_sw_pix_fmt_t sw_pix_fmt)
{
    int data_len = 0;

    if (sw_pix_fmt == NI_SW_PIX_FMT_YUV444P)
    {
        data_len = w * h * 3;
    } else if (sw_pix_fmt == NI_SW_PIX_FMT_YUV444P10LE)
    {
        data_len = w * h * 6;
    } else
    {
        switch (pix_fmt)
        {
            case NI_PIX_FMT_NV12:
            case NI_PIX_FMT_YUV420P:
                data_len = w * h * 3 / 2;
                break;
            case NI_PIX_FMT_P010LE:
            case NI_PIX_FMT_YUV420P10LE:
                data_len = w * h * 3;
                break;
            case NI_PIX_FMT_RGBA:
            case NI_PIX_FMT_BGRA:
            case NI_PIX_FMT_ARGB:
            case NI_PIX_FMT_ABGR:
            case NI_PIX_FMT_BGR0:
            case NI_PIX_FMT_BGRP:
                data_len = w * 4 * h;
                break;
            default:
                break;
        }
    }

    return data_len;
}

static int frame_buffer_size_calc(int width, int height, int format)
{
    int factor;
    int data_len = 0;

    switch (format)
    {
        case GC620_I420:
        {
            factor = 1;
            int luma_width_aligned = NI_VPU_ALIGN128(width * factor);
            int luma_height_aligned = ((height + 1) / 2) * 2;
            int luma_size = luma_width_aligned * luma_height_aligned;
            int chroma_width_aligned = NI_VPU_ALIGN128(width / 2 * factor);
            int chroma_height_aligned = luma_height_aligned / 2;
            int chroma_size = chroma_width_aligned * chroma_height_aligned;
            data_len = luma_size + chroma_size * 2;
            break;
        }
        case GC620_RGBA8888:
            factor = 4;
            data_len = NI_VPU_ALIGN64(width * factor) * height;
            break;
        case GC620_RGB888_PLANAR:
            factor = 3;
            data_len = NI_VPU_ALIGN32(width * factor) * height;
            break;
        default:
            break;
    }

    return data_len;
}

static ni_pixel_planar_format get_pixel_planar(ni_pix_fmt_t pix_fmt)
{
    ni_pixel_planar_format ret = -1;
    switch (pix_fmt)
    {
        case NI_PIX_FMT_NV12:
        case NI_PIX_FMT_P010LE:
            ret = NI_PIXEL_PLANAR_FORMAT_SEMIPLANAR;
            break;
        case NI_PIX_FMT_8_TILED4X4:
        case NI_PIX_FMT_10_TILED4X4:
            ret = NI_PIXEL_PLANAR_FORMAT_TILED4X4;
            break;
        case NI_PIX_FMT_YUV420P:
        case NI_PIX_FMT_YUV420P10LE:
        case NI_PIX_FMT_ABGR: /* 32-bit ABGR packed        */
        case NI_PIX_FMT_ARGB:
        case NI_PIX_FMT_RGBA:
        case NI_PIX_FMT_BGRA:
            ret = NI_PIXEL_PLANAR_FORMAT_PLANAR;
            break;
        default:
            break;
    }

    return ret;
}

static int read_yuv_from_file(int pfs, void *yuv_buf, int width, int height,
                              ni_pix_fmt_t pix_fmt, ni_sw_pix_fmt_t sw_pix_fmt,
                              int *eos, ni_session_run_state_t run_state)
{
    int chunk_size, frame_size;

    if (run_state == SESSION_RUN_STATE_SEQ_CHANGE_DRAINING)
    {
        // The first YUV frame was consumed on sequence change. Reset the file
        // pointer until the end of encoded packet is read.
        get_total_file_size(pfs);
        return 0;
    }

    frame_size = frame_read_buffer_size(width, height, pix_fmt, sw_pix_fmt);

    chunk_size = read_next_chunk_from_file(pfs, yuv_buf, frame_size);
    if (chunk_size < 0)
    {
        ni_log(NI_LOG_ERROR, "Error: could not read file!");
        return -1;
    } else if (chunk_size == 0)
    {
        *eos = 1;
        ni_log(NI_LOG_DEBUG, "%s: read chunk size 0, eos!\n", __func__);
        return 0;
    } else
    {
        *eos = 0;
        return chunk_size;
    }
}

static int convert_yuv_444p_to_420p(ni_session_data_io_t *p_frame,
                                    void *yuv_buf, int width, int height,
                                    ni_sw_pix_fmt_t sw_pix_fmt, int mode,
                                    ni_codec_format_t codec_format)
{
    int i, factor;
    uint8_t *p_src[NI_MAX_NUM_DATA_POINTERS] = { NULL };
    int dst_stride[NI_MAX_NUM_DATA_POINTERS] = { 0 };
    int height_aligned[NI_MAX_NUM_DATA_POINTERS] = { 0 };

    if (yuv_buf == NULL)
    {
        // EOS
        return 0;
    }

    switch (sw_pix_fmt)
    {
        case NI_SW_PIX_FMT_YUV444P:
            factor = 1;
            break;
        case NI_SW_PIX_FMT_YUV444P10LE:
            factor = 2;
            break;
        default:
            ni_log(NI_LOG_ERROR, "Error: invalid sw pix fmt %d\n", sw_pix_fmt);
            return -1;
    }

    ni_get_hw_yuv420p_dim(width, height, factor, 0, dst_stride, height_aligned);

    for (i = 0; i < 2; i++)
    {
        ni_frame_t *frame = &p_frame[i].data.frame;
        ni_encoder_frame_buffer_alloc(frame, width, height, dst_stride,
                                      codec_format == NI_CODEC_FORMAT_H264,
                                      NI_APP_ENC_FRAME_META_DATA_SIZE, 0);
        if (frame->p_data[0] == NULL)
        {
            ni_log(NI_LOG_ERROR, "Error: could not allocate YUV frame buffer!\n");
            return -1;
        }
    }

    p_src[0] = yuv_buf;
    p_src[1] = p_src[0] + width * factor * height;
    p_src[2] = p_src[1] + width * factor * height;

    ni_copy_yuv_444p_to_420p(p_frame[0].data.frame.p_data,
                             p_frame[1].data.frame.p_data,
                             p_src, width, height, factor, mode);

    return 0;
}

/*!*****************************************************************************
 *  \brief  Write hwdl data to files.
 *
 *  \param  
 *
 *  \return 0 if successful, < 0 otherwise
 ******************************************************************************/
int write_rawvideo_data(FILE *p_file, int width, int height, int format,
                        ni_frame_t *p_out_frame)
{
    if (p_file && p_out_frame)
    {
        if (format == GC620_I420)
        {
            int i, j;
            for (i = 0; i < 3; i++)
            {
                uint8_t *src = p_out_frame->p_data[i];
                int write_width = width;
                int write_height = height;
                int plane_width = width;
                int plane_height = height;

                write_width *= 1;   // bit depth 1

                if (i == 1 || i == 2)
                {
                    plane_height /= 2;
                    // U/V stride size is multiple of 128, following the calculation
                    // in ni_decoder_frame_buffer_alloc
                    plane_width =
                        (((int)(write_width) / 2 * 1 + 127) / 128) * 128;
                    write_height /= 2;
                    write_width /= 2;
                }

                for (j = 0; j < plane_height; j++)
                {
                    if (j < write_height &&
                        fwrite(src, write_width, 1, p_file) != 1)
                    {
                        ni_log(
                            NI_LOG_ERROR,
                            "Error: writing data plane %d: height %d error!\n",
                            i, plane_height);
                        ni_log(NI_LOG_ERROR, "Error: ferror rc = %d\n",
                                ferror(p_file));
                    }
                    src += plane_width;
                }
            }
        } else if (format == GC620_RGBA8888)
        {
            uint8_t *src = p_out_frame->p_data[0];
            if (fwrite(src, width * height * 4, 1, p_file) != 1)
            {
                ni_log(NI_LOG_ERROR, "Error: ferror rc = %d\n", ferror(p_file));
            }
        } else if (format == GC620_RGB888_PLANAR)
        {
            uint8_t *src;
            for (int i = 0; i < 3; i++)
            {
                src = p_out_frame->p_data[i];
                if (fwrite(src, width * height, 1, p_file) != 1)
                {
                    ni_log(NI_LOG_ERROR, "Error: ferror rc = %d\n", ferror(p_file));
                }
            }
        }

        if (fflush(p_file))
        {
            ni_log(NI_LOG_ERROR,
                    "Error: writing data frame flush failed! errno %d\n",
                    errno);
        }
    }
    return 0;
}

#ifndef _WIN32
/*!*****************************************************************************
 *  \brief  Read dmabuf data(4k aligned) to cpu for test.
 *
 *  \param  
 *
 *  \return 0 if successful, < 0 otherwise
 ******************************************************************************/
int read_from_dmabuf(ni_session_context_t *p_ctx, niFrameSurface1_t *p_surface,
                     disp_buffer_t *disp, int format, FILE *fp)
{
    int ret = 0;
    int data_len;
    niFrameSurface1_t tmp_surf;

    if (!disp)
    {
        ni_log(NI_LOG_ERROR, "Error: Invalid input params\n");
        return -1;
    }

    memcpy(&tmp_surf, p_surface, sizeof(niFrameSurface1_t));

    data_len = frame_buffer_size_calc(p_surface->ui16width,
                                      p_surface->ui16height, format);
    if (!data_len)
    {
        ni_log(NI_LOG_ERROR, "Error: read size is 0!\n");
        return -1;
    }
    // must be 4096 bytes aligned
    data_len = NI_VPU_ALIGN4096(data_len);

    disp->fd = -1;
    disp->mmap_data = MAP_FAILED;
    disp->len = data_len;
    disp->bar = 4;

    ret = ni_scaler_p2p_frame_acquire(p_ctx, p_surface, data_len);
    if (ret != 0)
    {
        ni_log(NI_LOG_ERROR, "failed to export dma buf\n");
        return -1;
    }
    disp->fd = p_surface->dma_buf_fd;

    //mmap
    disp->mmap_data =
        mmap(0, data_len, PROT_READ | PROT_WRITE, MAP_SHARED, disp->fd, 0);
    if (disp->mmap_data == MAP_FAILED)
    {
        ni_log(NI_LOG_ERROR, "failed to mmap dmabuf: %s\n", strerror(errno));
        return -1;
    }

    uint8_t *data = NULL;
    ret = ni_posix_memalign((void **)&data, sysconf(_SC_PAGESIZE), disp->len);
    if (ret)
    {
        ni_log(NI_LOG_ERROR, "failed to allocate memory\n");
        return -1;
    }
    disp->data = data;

    // write data to output file
    uint8_t *pdata = NULL;
    pdata = malloc(data_len);
    if (!pdata)
    {
        ni_log(NI_LOG_ERROR, "failed to allocate data\n");
        if (fp)
            fclose(fp);
        free(pdata);
        return -1;
    }

    struct netint_iocmd_issue_request uis;
    memset(&uis, 0, sizeof(uis));
    uis.fd = disp->fd;
    uis.data = disp->data;
    uis.len = disp->len;
    uis.dir = NI_DMABUF_READ_FROM_DEVICE;
    memset(disp->data, 0, disp->len);
    ret = ioctl(p_ctx->netint_fd, NETINT_IOCTL_ISSUE_REQ, &uis);
    if (ret < 0)
    {
        ni_log(NI_LOG_ERROR, "failed to send req: %s\n", strerror(errno));
        if (fp)
            fclose(fp);
        free(pdata);
        return -1;
    }

    // poll to check if read complete
    struct pollfd pfds[1];
    pfds[0].fd = disp->fd;
    pfds[0].events = POLLOUT;
    pfds[0].revents = 0;
    ret = poll(pfds, 1, -1);
    if (ret < 0)
    {
        ni_log(NI_LOG_ERROR, "failed to poll\n");
        if (fp)
            fclose(fp);
        free(pdata);
        return -1;
    }

    ni_log(NI_LOG_DEBUG, "data ox%lx, data_len %u\n", (unsigned long)disp->data,
           data_len);
    memcpy(pdata, disp->data, data_len);

    if (fp)
    {
        if (0 !=
            write_dmabuf_data(pdata, fp, p_surface->ui16width,
                              p_surface->ui16height, format))
        {
            ni_log(NI_LOG_ERROR, "failed to write file\n");
            if (fp)
                fclose(fp);
            free(pdata);
            return -1;
        }
        free(pdata);
    }

    return 0;
}
#endif

/*!*****************************************************************************
 *  \brief  Download hw frames by HwDesc.
 *
 *  \param  
 *
 *  \return 0 if successful, < 0 otherwise
 ******************************************************************************/
int hwdl_frame(ni_session_context_t *p_ctx,
               ni_session_data_io_t *p_session_data, ni_frame_t *p_src_frame,
               int output_format)
{
    niFrameSurface1_t *src_surf = (niFrameSurface1_t *)(p_src_frame->p_data[3]);
    int ret = 0;
    int pixel_format;

    switch (output_format)
    {
        case GC620_I420:
            pixel_format = NI_PIX_FMT_YUV420P;
            break;
        case GC620_RGBA8888:
            pixel_format = NI_PIX_FMT_RGBA;
            break;
        case GC620_RGB888_PLANAR:
            pixel_format = NI_PIX_FMT_BGRP;
            break;
        default:
            ni_log(NI_LOG_ERROR, "Pixel format not supported.\n");
            return NI_RETCODE_INVALID_PARAM;
    }

    ret = ni_frame_buffer_alloc_dl(&(p_session_data->data.frame),
                                   src_surf->ui16width, src_surf->ui16height,
                                   pixel_format);

    if (ret != NI_RETCODE_SUCCESS)
    {
        return NI_RETCODE_ERROR_MEM_ALOC;
    }

    p_ctx->is_auto_dl = false;
    ret = ni_device_session_hwdl(p_ctx, p_session_data, src_surf);
    if (ret <= 0)
    {
        ni_frame_buffer_free(&p_session_data->data.frame);
        return ret;
    }
    return ret;
}

/*!*****************************************************************************
 *  \brief  Send decoder input data
 *
 *  \param  
 *
 *  \return
 ******************************************************************************/
ni_retcode_t decoder_send_data(ni_session_context_t *p_dec_ctx,
                               ni_session_data_io_t *p_in_data,
                               int input_video_width, int input_video_height,
                               int pkt_size, unsigned long *total_bytes_sent,
                               int print_time, device_state_t *p_device_state,
                               void *stream_info)
{
    static uint8_t tmp_buf[NI_MAX_TX_SZ] = {0};
    uint8_t *tmp_buf_ptr = tmp_buf;
    int packet_size = pkt_size;
    // int chunk_size = 0;
    uint32_t frame_pkt_size = 0, nal_size;
    int nal_type = -1;
    int tx_size = 0;
    uint32_t send_size = 0;
    int new_packet = 0;
    int saved_prev_size = 0;
    int32_t frame_num = -1, curr_frame_num;
    unsigned int first_mb_in_slice = 0;
    ni_packet_t *p_in_pkt = &(p_in_data->data.packet);
    ni_retcode_t retval = NI_RETCODE_SUCCESS;

    ni_log(NI_LOG_DEBUG, "===> decoder_send_data <===\n");

    if (p_device_state->dec_eos_sent)
    {
        ni_log(NI_LOG_DEBUG, "decoder_send_data: ALL data (incl. eos) sent "
                       "already!\n");
        LRETURN;
    }

    if (0 == p_in_pkt->data_len)
    {
        memset(p_in_pkt, 0, sizeof(ni_packet_t));

        if (NI_CODEC_FORMAT_H264 == p_dec_ctx->codec_format)
        {
            ni_h264_sps_t *sps;
            sps = (ni_h264_sps_t *)stream_info;
            // send whole encoded packet which ends with a slice NAL
            while ((nal_size = find_h264_next_nalu(tmp_buf_ptr, &nal_type)) > 0)
            {
                frame_pkt_size += nal_size;
                tmp_buf_ptr += nal_size;
                ni_log(NI_LOG_DEBUG, "%s nal %d  nal_size %d\n", __func__,
                       nal_type, nal_size);

                // save parsed out sps/pps as stream headers in the decode session
                if (H264_NAL_PPS == nal_type)
                {
                    if (NI_RETCODE_SUCCESS !=
                        ni_device_dec_session_save_hdrs(p_dec_ctx, tmp_buf,
                                                        frame_pkt_size))
                    {
                        ni_log(NI_LOG_ERROR, "decoder_send_data: save_hdr failed!\n");
                    }
                }

                if (H264_NAL_SLICE == nal_type ||
                    H264_NAL_IDR_SLICE == nal_type)
                {
                    if (!parse_h264_slice_header(tmp_buf_ptr - nal_size,
                                                 nal_size, sps, &curr_frame_num,
                                                 &first_mb_in_slice))
                    {
                        if (-1 == frame_num)
                        {
                            // first slice, continue to check
                            frame_num = curr_frame_num;
                        } else if (curr_frame_num != frame_num ||
                                   0 == first_mb_in_slice)
                        {
                            // this slice has diff. frame_num or first_mb_in_slice addr is
                            // 0: not the same frame and return
                            rewind_data_buf_pos_by(nal_size);
                            frame_pkt_size -= nal_size;
                            break;
                        }
                        // this slice is in the same frame, so continue to check and see
                        // if there is more
                    } else
                    {
                        ni_log(NI_LOG_ERROR,
                               "decoder_send_data: parse_slice_header error "
                               "NAL type %d size %u, continue\n",
                               nal_type, nal_size);
                    }
                } else if (-1 != frame_num)
                {
                    // already got a slice and this is non-slice NAL: return
                    rewind_data_buf_pos_by(nal_size);
                    frame_pkt_size -= nal_size;
                    break;
                }
                // otherwise continue until a slice is found
            }   // while there is still NAL
        } else if (NI_CODEC_FORMAT_H265 == p_dec_ctx->codec_format)
        {
            while ((nal_size = find_h265_next_nalu(tmp_buf_ptr, &nal_type)) > 0)
            {
                frame_pkt_size += nal_size;
                tmp_buf_ptr += nal_size;
                ni_log(NI_LOG_DEBUG, "%s nal_type %d nal_size %d\n", __func__,
                       nal_type, nal_size);

                if (nal_type == HEVC_NAL_VPS || nal_type == HEVC_NAL_SPS ||
                    nal_type == HEVC_NAL_PPS)   // save vps, sps, pps
                {
                    if (nal_type == 32 && p_dec_ctx->prev_size > 0)
                    {
                        // sequence change situation, replace previous p_leftover with new headers
                        p_dec_ctx->prev_size = 0;
                    }
                    // copy the nal to the end of p_leftover
                    memcpy(p_dec_ctx->p_leftover + p_dec_ctx->prev_size,
                           tmp_buf_ptr - nal_size, nal_size);
                    p_dec_ctx->prev_size += nal_size;
                }

                if (nal_type >= 0 && nal_type <= 23)   // vcl units
                {
                    ni_log(NI_LOG_DEBUG, "%s send vcl_nal %d nal_size %d\n",
                           __func__, nal_type, nal_size);
                    break;
                }
            }
        } else if (NI_CODEC_FORMAT_VP9 == p_dec_ctx->codec_format)
        {
            while ((packet_size = find_vp9_next_packet(tmp_buf_ptr, stream_info)) > 0)
            {
                frame_pkt_size += packet_size;
                ni_log(NI_LOG_DEBUG, "%s vp9 packet_size %d\n", __func__,
                       packet_size);
                break;
            }
        } else
        {
            frame_pkt_size = read_next_chunk(tmp_buf, packet_size);
            // chunk_size = frame_pkt_size;
        }
        ni_log(NI_LOG_DEBUG, "decoder_send_data * frame_pkt_size %d\n",
                       frame_pkt_size);

        p_in_pkt->p_data = NULL;
        p_in_pkt->data_len = frame_pkt_size;

        if (frame_pkt_size + p_dec_ctx->prev_size > 0)
        {
            ni_packet_buffer_alloc(p_in_pkt,
                                   (int)frame_pkt_size + p_dec_ctx->prev_size);
        }

        new_packet = 1;
        send_size = frame_pkt_size + p_dec_ctx->prev_size;
        saved_prev_size = p_dec_ctx->prev_size;
    } else
    {
        send_size = p_in_pkt->data_len;
    }

    p_in_pkt->start_of_stream = 0;
    if (!p_device_state->dec_sos_sent)
    {
        p_in_pkt->start_of_stream = 1;
        p_device_state->dec_sos_sent = 1;
    }
    p_in_pkt->end_of_stream = 0;
    p_in_pkt->video_width = input_video_width;
    p_in_pkt->video_height = input_video_height;

    if (send_size == 0)
    {
        if (new_packet)
        {
            send_size =
                ni_packet_copy(p_in_pkt->p_data, tmp_buf, 0,
                               p_dec_ctx->p_leftover, &p_dec_ctx->prev_size);
        }
        p_in_pkt->data_len = send_size;

        if (curr_found_pos)
        {
            p_in_pkt->end_of_stream = 1;
            ni_log(NI_LOG_ERROR, "Sending p_last packet (size %u) + eos\n", p_in_pkt->data_len);
        }
    } else
    {
        if (new_packet)
        {
            send_size =
                ni_packet_copy(p_in_pkt->p_data, tmp_buf, frame_pkt_size,
                               p_dec_ctx->p_leftover, &p_dec_ctx->prev_size);
            // p_in_pkt->data_len is the actual packet size to be sent to decoder
            p_in_pkt->data_len += saved_prev_size;
        }
    }

    tx_size =
        ni_device_session_write(p_dec_ctx, p_in_data, NI_DEVICE_TYPE_DECODER);

    if (tx_size < 0)
    {
        // Error
        ni_log(NI_LOG_ERROR, "Error: sending data error. rc:%d\n", tx_size);
        retval = NI_RETCODE_FAILURE;
        LRETURN;
    } else if (tx_size == 0)
    {
        ni_log(NI_LOG_DEBUG, "0 byte sent this time, sleep and will re-try.\n");
        ni_usleep(10000);
    } else if ((uint32_t)tx_size < send_size)
    {
        if (print_time)
        {
            //ni_log(NI_LOG_ERROR, "Sent %d < %d , re-try next time ?\n", tx_size, send_size);
        }
    }

    *total_bytes_sent += tx_size;

    if (p_dec_ctx->ready_to_close)
    {
        p_device_state->dec_eos_sent = 1;
    }

    if (print_time)
    {
        ni_log(NI_LOG_ERROR, "decoder_send_data: success, total sent: %lu\n",
               *total_bytes_sent);
    }

    if (tx_size > 0)
    {
        ni_log(NI_LOG_DEBUG, "decoder_send_data: reset packet_buffer.\n");
        ni_packet_buffer_free(p_in_pkt);
    }

    retval = NI_RETCODE_SUCCESS;

END:

    return retval;
}

/*!*****************************************************************************
 *  \brief  Receive decoded output data from decoder
 *
 *  \param  
 *
 *  \return 0: got YUV frame;  1: end-of-stream;  2: got nothing
 ******************************************************************************/
int decoder_receive_data(ni_session_context_t *p_dec_ctx,
                         ni_session_data_io_t *p_out_data,
                         int output_video_width, int output_video_height,
                         FILE *p_file, unsigned long long *total_bytes_received,
                         int print_time, int write_to_file,
                         device_state_t *p_device_state,
                         int * p_rx_size)
{
    int rc = NI_RETCODE_FAILURE;
    int end_flag = 0;
    int rx_size = 0;
    bool b_is_hwframe = p_dec_ctx->hw_action;
    ni_frame_t *p_out_frame = &(p_out_data->data.frame);
    int width, height;
    // In decoder session read function it will allocate the actual YUV
    // transfer size for the very first read. And the pixel format of session
    // context would be set as well. So it does not matter with the planar
    // format for the first call of this function.
    int is_planar = get_pixel_planar(p_dec_ctx->pixel_format) == NI_PIXEL_PLANAR_FORMAT_PLANAR;

    ni_log(NI_LOG_DEBUG,
           "===> decoder_receive_data hwframe %d pixel_format %d <===\n",
           b_is_hwframe, p_dec_ctx->pixel_format);

    if (p_device_state->dec_eos_received)
    {
        ni_log(NI_LOG_DEBUG, "decoder_receive_data eos received already, Done!\n");
        rc = 2;
        LRETURN;
    }

    // prepare memory buffer for receiving decoded frame
    width = p_dec_ctx->actual_video_width > 0 ?
        (int)(p_dec_ctx->actual_video_width) :
        output_video_width;
    height = p_dec_ctx->active_video_height > 0 ?
        (int)(p_dec_ctx->active_video_height) :
        output_video_height;

    // allocate memory only after resolution is known (for buffer pool set up)
    int alloc_mem = (p_dec_ctx->active_video_width > 0 &&
                             p_dec_ctx->active_video_height > 0 ?
                         1 :
                         0);
    if (!b_is_hwframe)
    {
        rc = ni_decoder_frame_buffer_alloc(
            p_dec_ctx->dec_fme_buf_pool, &(p_out_data->data.frame), alloc_mem,
            width, height, p_dec_ctx->codec_format == NI_CODEC_FORMAT_H264,
            p_dec_ctx->bit_depth_factor, is_planar);
        if (NI_RETCODE_SUCCESS != rc)
        {
            LRETURN;
        }
        rx_size = ni_device_session_read(p_dec_ctx, p_out_data,
                                         NI_DEVICE_TYPE_DECODER);
    } else
    {
        rc = ni_frame_buffer_alloc(
            &(p_out_data->data.frame), width, height,
            p_dec_ctx->codec_format == NI_CODEC_FORMAT_H264, 1,
            p_dec_ctx->bit_depth_factor,
            3 /*3 is max supported hwframe output count per frame*/, is_planar);
        if (NI_RETCODE_SUCCESS != rc)
        {
            LRETURN;
        }
        rx_size = ni_device_session_read_hwdesc(p_dec_ctx, p_out_data,
                                                NI_DEVICE_TYPE_DECODER);
    }

    // the actual pix_fmt is known and updated in ctx only after the first
    // frame is decoded, so check/update it here again to be used below
    is_planar = get_pixel_planar(p_dec_ctx->pixel_format) ==
        NI_PIXEL_PLANAR_FORMAT_PLANAR;

    end_flag = p_out_frame->end_of_stream;

    if (rx_size < 0)
    {
        ni_log(NI_LOG_ERROR, "Error: receiving data error. rc:%d\n", rx_size);
        if (!b_is_hwframe)
        {
            ni_decoder_frame_buffer_free(&(p_out_data->data.frame));
        } else
        {
            ni_frame_buffer_free(&(p_out_data->data.frame));
        }
        rc = NI_RETCODE_FAILURE;
        LRETURN;
    } else if (rx_size > 0)
    {
        number_of_frames++;
        ni_log(NI_LOG_DEBUG, "Got frame # %" PRIu64 " bytes %d\n",
                       p_dec_ctx->frame_num, rx_size);

        ni_dec_retrieve_aux_data(p_out_frame);
    }
    // rx_size == 0 means no decoded frame is available now
    if (rx_size > 0 && p_file && write_to_file)
    {
        int i, j;
        uint8_t *src;
        for (i = 0; i < 3; i++)
        {
            src = p_out_frame->p_data[i];

            int plane_height = p_dec_ctx->active_video_height;
            int plane_width = p_dec_ctx->active_video_width;
            int write_height = output_video_height;
            int write_width = output_video_width;

            // support for 8/10 bit depth
            // plane_width is the actual Y stride size
            write_width *= p_dec_ctx->bit_depth_factor;

            if (i == 1 || i == 2)
            {
                plane_height /= 2;
                // U/V stride size is multiple of 128, following the calculation
                // in ni_decoder_frame_buffer_alloc
                plane_width = (((int)(p_dec_ctx->actual_video_width) / 2 *
                                    p_dec_ctx->bit_depth_factor +
                                127) /
                               128) *
                    128;

                if (!is_planar)
                {
                    plane_width = ((((int)(p_dec_ctx->actual_video_width) *
                                         p_dec_ctx->bit_depth_factor +
                                     127) /
                                    128) *
                                   128) /
                        p_dec_ctx->bit_depth_factor;
                    // for semi-planar format, output UV at same time (data[1])
                    // and skip data[2]
                    if (i == 1)
                    {
                        write_width *= 2;
                    }
                    if (i == 2)
                    {
                        plane_height = 0;
                    }
                }

                write_height /= 2;
                write_width /= 2;
            }

            // apply the cropping windown in writing out the YUV frame
            // for now the windown is usually crop-left = crop-top = 0, and we
            // use this to simplify the cropping logic
            for (j = 0; j < plane_height; j++)
            {
                if (j < write_height &&
                    fwrite(src, write_width, 1, p_file) != 1)
                {
                    ni_log(NI_LOG_ERROR,
                            "Error: writing data plane %d: height %d error!\n",
                            i, plane_height);
                    ni_log(NI_LOG_ERROR, "Error: ferror rc = %d\n", ferror(p_file));
                }
                src += plane_width;
            }
        }

        if (fflush(p_file))
        {
            ni_log(NI_LOG_ERROR,
                    "Error: writing data frame flush failed! errno %d\n",
                    errno);
        }
    }

    *total_bytes_received += rx_size;
    *p_rx_size = rx_size;

    if (print_time)
    {
        ni_log(NI_LOG_ERROR, "[R] Got:%d  Frames= %u  fps=%f  Total bytes %llu\n", rx_size,
               number_of_frames,
               ((float)number_of_frames /
                (float)(current_time.tv_sec - start_time.tv_sec)),
               (unsigned long long)*total_bytes_received);
    }

    if (end_flag)
    {
        ni_log(NI_LOG_INFO, "Decoder Receiving done.\n");
        p_device_state->dec_eos_received = 1;
        rc = 1;
    } else if (0 == rx_size)
    {
        rc = 2;
    }

    ni_log(NI_LOG_DEBUG, "decoder_receive_data: success\n");

END:
    ni_log(NI_LOG_DEBUG, "decoder_receive_data: rc %d rx_size %d\n", rc, rx_size);

    return rc;
}

// convert various reconfig and demo modes (stored in encoder configuration) to
// aux data and store them in frame
void prep_reconf_demo_data(ni_session_context_t *p_enc_ctx, ni_frame_t *frame)
{
    ni_xcoder_params_t *api_param =
        (ni_xcoder_params_t *)p_enc_ctx->p_session_config;
    ni_aux_data_t *aux_data = NULL;

    switch (api_param->reconf_demo_mode)
    {
        case XCODER_TEST_RECONF_BR:
            if (p_enc_ctx->frame_num ==
                api_param->reconf_hash[g_reconfigCount][0])
            {
                aux_data = ni_frame_new_aux_data(
                    frame, NI_FRAME_AUX_DATA_BITRATE, sizeof(int32_t));
                if (!aux_data)
                {
                    ni_log(NI_LOG_ERROR, 
                        "Error %s(): no mem for reconf BR aux_data\n",
                        __func__);
                    return;
                }
                *((int32_t *)aux_data->data) =
                    api_param->reconf_hash[g_reconfigCount][1];
                ni_log(NI_LOG_DEBUG, "%s(): frame #%lu reconfig BR %d by frame aux data\n",
                               __func__, p_enc_ctx->frame_num,
                               api_param->reconf_hash[g_reconfigCount][1]);

                g_reconfigCount++;
            }
            break;
        case XCODER_TEST_RECONF_INTRAPRD:
            if (p_enc_ctx->frame_num ==
                api_param->reconf_hash[g_reconfigCount][0])
            {
                aux_data = ni_frame_new_aux_data(
                    frame, NI_FRAME_AUX_DATA_INTRAPRD, sizeof(int32_t));
                if (!aux_data)
                {
                    return;
                }
                int32_t intraprd = *((int32_t *)aux_data->data) =
                    api_param->reconf_hash[g_reconfigCount][1];
                ni_log(NI_LOG_TRACE,
                        "xcoder_send_frame: frame #%lu reconf "
                        "intraPeriod %d\n",
                        p_enc_ctx->frame_num,
                        intraprd);
                g_reconfigCount++;
            }
            break;
        case XCODER_TEST_RECONF_VUI_HRD:
            if (p_enc_ctx->frame_num ==
                api_param->reconf_hash[g_reconfigCount][0])
            {
                p_enc_ctx->enc_change_params->enable_option |=
                    NI_SET_CHANGE_PARAM_VUI_HRD_PARAM;
                p_enc_ctx->enc_change_params->colorDescPresent =
                    api_param->reconf_hash[g_reconfigCount][1];
                p_enc_ctx->enc_change_params->colorPrimaries =
                    api_param->reconf_hash[g_reconfigCount][2];
                p_enc_ctx->enc_change_params->colorTrc =
                    api_param->reconf_hash[g_reconfigCount][3];
                p_enc_ctx->enc_change_params->colorSpace =
                    api_param->reconf_hash[g_reconfigCount][4];
                p_enc_ctx->enc_change_params->aspectRatioWidth =
                    api_param->reconf_hash[g_reconfigCount][5];
                p_enc_ctx->enc_change_params->aspectRatioHeight =
                    api_param->reconf_hash[g_reconfigCount][6];
                p_enc_ctx->enc_change_params->videoFullRange =
                    api_param->reconf_hash[g_reconfigCount][7];

                // frame reconf_len needs to be set here
                frame->reconf_len = sizeof(ni_encoder_change_params_t);
                g_reconfigCount++;
            }
            break;
        case XCODER_TEST_RECONF_LONG_TERM_REF:
            // the reconf file data line format for this is:
            // <frame-number>:useCurSrcAsLongtermPic,useLongtermRef where
            // values will stay the same on every frame until changed.
            if (p_enc_ctx->frame_num ==
                api_param->reconf_hash[g_reconfigCount][0])
            {
                aux_data = ni_frame_new_aux_data(
                    frame, NI_FRAME_AUX_DATA_LONG_TERM_REF,
                    sizeof(ni_long_term_ref_t));
                if (!aux_data)
                {
                    ni_log(NI_LOG_ERROR, 
                        "Error %s(): no mem for reconf LTR aux_data\n",
                        __func__);
                    return;
                }
                ni_long_term_ref_t *ltr = (ni_long_term_ref_t *)aux_data->data;
                ltr->use_cur_src_as_long_term_pic =
                    (uint8_t)api_param->reconf_hash[g_reconfigCount][1];
                ltr->use_long_term_ref =
                    (uint8_t)api_param->reconf_hash[g_reconfigCount][2];

                ni_log(NI_LOG_DEBUG, 
                    "%s(): frame #%lu reconf LTR "
                    "use_cur_src_as_long_term_pic %u use_long_term_ref "
                    "%u\n",
                    __func__, p_enc_ctx->frame_num,
                    ltr->use_cur_src_as_long_term_pic, ltr->use_long_term_ref);

                g_reconfigCount++;
            }
            break;
        case XCODER_TEST_RECONF_RC_MIN_MAX_QP:
        case XCODER_TEST_RECONF_RC_MIN_MAX_QP_REDUNDANT:
            if (p_enc_ctx->frame_num ==
                api_param->reconf_hash[g_reconfigCount][0])
            {
                aux_data = ni_frame_new_aux_data(
                    frame, NI_FRAME_AUX_DATA_MAX_MIN_QP, sizeof(ni_rc_min_max_qp));
                if (!aux_data) {
                    ni_log(NI_LOG_ERROR, 
                        "Error %s(): no mem for reconf max&min QP aux_data\n",
                        __func__);
                    return;
                }
                ni_rc_min_max_qp *qp_info = (ni_rc_min_max_qp *)aux_data->data;
                qp_info->minQpI     = api_param->reconf_hash[g_reconfigCount][1];
                qp_info->maxQpI     = api_param->reconf_hash[g_reconfigCount][2];
                qp_info->maxDeltaQp = api_param->reconf_hash[g_reconfigCount][3];
                qp_info->minQpPB    = api_param->reconf_hash[g_reconfigCount][4];
                qp_info->maxQpPB    = api_param->reconf_hash[g_reconfigCount][5];

                g_reconfigCount++;
            }
            break;
        case XCODER_TEST_RECONF_LTR_INTERVAL:
            if (p_enc_ctx->frame_num ==
                api_param->reconf_hash[g_reconfigCount][0])
            {
                aux_data = ni_frame_new_aux_data(
                    frame, NI_FRAME_AUX_DATA_LTR_INTERVAL, sizeof(int32_t));
                if (!aux_data)
                {
                    ni_log(NI_LOG_ERROR, "Error %s(): no mem for reconf LTR interval "
                                   "aux_data\n",
                                   __func__);
                    return;
                }
                *((int32_t *)aux_data->data) =
                    api_param->reconf_hash[g_reconfigCount][1];
                ni_log(NI_LOG_DEBUG, "%s(): frame #%lu reconf LTR interval %d\n",
                               __func__, p_enc_ctx->frame_num,
                               *((int32_t *)aux_data->data));

                g_reconfigCount++;
            }
            break;
        case XCODER_TEST_INVALID_REF_FRAME:
            if (p_enc_ctx->frame_num ==
                api_param->reconf_hash[g_reconfigCount][0])
            {
                aux_data = ni_frame_new_aux_data(
                    frame, NI_FRAME_AUX_DATA_INVALID_REF_FRAME,
                    sizeof(int32_t));
                if (!aux_data)
                {
                    ni_log(NI_LOG_ERROR, "Error %s(): no mem for reconf invalid ref "
                                   "frame aux_data\n",
                                   __func__);
                    return;
                }
                *((int32_t *)aux_data->data) =
                    api_param->reconf_hash[g_reconfigCount][1];
                ni_log(NI_LOG_DEBUG, "%s(): frame #%lu reconf invalid ref frame %d\n",
                               __func__, p_enc_ctx->frame_num,
                               *((int32_t *)aux_data->data));

                g_reconfigCount++;
            }
            break;
        case XCODER_TEST_RECONF_FRAMERATE:
            if (p_enc_ctx->frame_num ==
                api_param->reconf_hash[g_reconfigCount][0])
            {
                aux_data = ni_frame_new_aux_data(
                    frame, NI_FRAME_AUX_DATA_FRAMERATE, sizeof(ni_framerate_t));
                if (!aux_data)
                {
                    ni_log(NI_LOG_ERROR, 
                        "Error %s(): no mem for reconf framerate aux_data\n",
                        __func__);
                    return;
                }
                ni_framerate_t *framerate = (ni_framerate_t *)aux_data->data;
                framerate->framerate_num =
                    (int32_t)api_param->reconf_hash[g_reconfigCount][1];
                framerate->framerate_denom =
                    (int32_t)api_param->reconf_hash[g_reconfigCount][2];

                ni_log(NI_LOG_DEBUG, 
                    "%s(): frame #%lu reconfig framerate (%d/%d) by frame aux data\n",
                    __func__, p_enc_ctx->frame_num, framerate->framerate_num,
                    framerate->framerate_denom);

                g_reconfigCount++;
            }
            break;
        case XCODER_TEST_RECONF_MAX_FRAME_SIZE:
            if (p_enc_ctx->frame_num ==
                api_param->reconf_hash[g_reconfigCount][0])
            {
                aux_data = ni_frame_new_aux_data(
                    frame, NI_FRAME_AUX_DATA_MAX_FRAME_SIZE, sizeof(int32_t));
                if (!aux_data)
                {
                    ni_log(NI_LOG_ERROR, 
                        "Error %s(): no mem for reconf maxFrameSize aux_data\n",
                        __func__);
                    return;
                }
                *((int32_t *)aux_data->data) =
                    api_param->reconf_hash[g_reconfigCount][1];
                ni_log(NI_LOG_DEBUG, "%s(): frame #%lu reconfig maxFrameSize %d by frame aux data\n",
                               __func__, p_enc_ctx->frame_num,
                               api_param->reconf_hash[g_reconfigCount][1]);

                g_reconfigCount++;
            }
            break;
        case XCODER_TEST_RECONF_CRF:
            if (p_enc_ctx->frame_num ==
                api_param->reconf_hash[g_reconfigCount][0])
            {
                aux_data = ni_frame_new_aux_data(
                    frame, NI_FRAME_AUX_DATA_CRF, sizeof(int32_t));
                if (!aux_data) {
                    ni_log(NI_LOG_ERROR, 
                        "Error %s(): no mem for reconf crf aux_data\n",
                        __func__);
                    return;
                }
                *((int32_t *)aux_data->data) =
                    api_param->reconf_hash[g_reconfigCount][1];
                ni_log(NI_LOG_DEBUG, "%s(): frame #%lu reconfig crf %d by frame aux data\n",
                               __func__, p_enc_ctx->frame_num,
                               api_param->reconf_hash[g_reconfigCount][1]);
                
                g_reconfigCount++;
            }
            break;
        case XCODER_TEST_RECONF_CRF_FLOAT:
            if (p_enc_ctx->frame_num ==
                api_param->reconf_hash[g_reconfigCount][0])
            {
                aux_data = ni_frame_new_aux_data(
                    frame, NI_FRAME_AUX_DATA_CRF_FLOAT, sizeof(float));
                if (!aux_data) {
                    ni_log(NI_LOG_ERROR,
                        "Error %s(): no mem for reconf crf aux_data\n",
                        __func__);
                    return;
                }
                float crf = (float)(api_param->reconf_hash[g_reconfigCount][1] +
                    (float)api_param->reconf_hash[g_reconfigCount][2] / 100.0);
                *((float *)aux_data->data) = crf;
                ni_log(NI_LOG_DEBUG,
                       "%s(): frame #%lu reconfig float type crf %f by frame "
                       "aux data\n", __func__, p_enc_ctx->frame_num, crf);
                g_reconfigCount++;
            }
            break;

        case XCODER_TEST_RECONF_VBV:
            if (p_enc_ctx->frame_num ==
                api_param->reconf_hash[g_reconfigCount][0])
            {
                aux_data = ni_frame_new_aux_data(
                    frame, NI_FRAME_AUX_DATA_VBV_MAX_RATE, sizeof(int32_t));
                if (!aux_data) {
                    ni_log(NI_LOG_ERROR, 
                        "Error %s(): no mem for reconf vbfMaxRate aux_data\n",
                        __func__);
                    return;
                }
                *((int32_t *)aux_data->data) =
                    api_param->reconf_hash[g_reconfigCount][1];
                aux_data = ni_frame_new_aux_data(
                    frame, NI_FRAME_AUX_DATA_VBV_BUFFER_SIZE, sizeof(int32_t));
                if (!aux_data) {
                    ni_log(NI_LOG_ERROR, 
                        "Error %s(): no mem for reconf vbvBufferSize aux_data\n",
                        __func__);
                    return;
                }
                *((int32_t *)aux_data->data) =
                    api_param->reconf_hash[g_reconfigCount][2];
                ni_log(NI_LOG_DEBUG,
                       "%s(): frame #%lu reconfig vbfMaxRate %d vbvBufferSize "
                       "%d by frame aux data\n",
                       __func__, p_enc_ctx->frame_num,
                       api_param->reconf_hash[g_reconfigCount][1],
                       api_param->reconf_hash[g_reconfigCount][2]);
                
                g_reconfigCount++;
            }
            break;
        case XCODER_TEST_RECONF_MAX_FRAME_SIZE_RATIO:
            if (p_enc_ctx->frame_num ==
                api_param->reconf_hash[g_reconfigCount][0]) {
                int maxFrameSizeRatio = api_param->reconf_hash[g_reconfigCount][1];
                if (maxFrameSizeRatio < 1) {
                    ni_log(NI_LOG_ERROR, "maxFrameSizeRatio %d cannot < 1\n",
                        maxFrameSizeRatio);
                    return;
                }
                aux_data = ni_frame_new_aux_data(
                    frame, NI_FRAME_AUX_DATA_MAX_FRAME_SIZE, sizeof(int32_t));
                if (!aux_data) {
                    ni_log(NI_LOG_ERROR, 
                        "Error %s(): no mem for reconf maxFrameSizeRatio aux_data\n",
                        __func__);
                    return;
                }

                int32_t bitrate, framerate_num, framerate_denom;
                uint32_t min_maxFrameSize, maxFrameSize;
                bitrate = (p_enc_ctx->target_bitrate > 0) ?  p_enc_ctx->target_bitrate : api_param->bitrate;      

                if ((p_enc_ctx->framerate.framerate_num > 0) && (p_enc_ctx->framerate.framerate_denom > 0))
                {
                    framerate_num = p_enc_ctx->framerate.framerate_num;
                    framerate_denom = p_enc_ctx->framerate.framerate_denom;
                }
                else
                {
                    framerate_num = (int32_t) api_param->fps_number;
                    framerate_denom = (int32_t) api_param->fps_denominator;
                }

                min_maxFrameSize = ((uint32_t)bitrate / framerate_num * framerate_denom) / 8;
                maxFrameSize = min_maxFrameSize * maxFrameSizeRatio > NI_MAX_FRAME_SIZE ?
                               NI_MAX_FRAME_SIZE : min_maxFrameSize * maxFrameSizeRatio;
                *((int32_t *)aux_data->data) = maxFrameSize;
                ni_log(NI_LOG_DEBUG,
                        "xcoder_send_frame: frame #%lu reconf "
                        "maxFrameSizeRatio %d maxFrameSize %d\n",
                        p_enc_ctx->frame_num, maxFrameSizeRatio, maxFrameSize);

                g_reconfigCount++;
            }
            break;
        case XCODER_TEST_RECONF_SLICE_ARG:
            if (p_enc_ctx->frame_num ==
                api_param->reconf_hash[g_reconfigCount][0]) {
                aux_data = ni_frame_new_aux_data(
                    frame, NI_FRAME_AUX_DATA_SLICE_ARG, sizeof(int16_t));
                if (!aux_data) {
                    ni_log(NI_LOG_ERROR, 
                        "Error %s(): no mem for reconf sliceArg aux_data\n",
                        __func__);
                    return;
                }
                *((int16_t *)aux_data->data) =
                    api_param->reconf_hash[g_reconfigCount][1];
                ni_log2(p_enc_ctx, NI_LOG_TRACE, 
                        "xcoder_send_frame: frame #%lu reconf "
                        "sliceArg %d\n",
                        p_enc_ctx->frame_num,
                        api_param->reconf_hash[g_reconfigCount][1]);

                g_reconfigCount++;
            }
            break;

        case XCODER_TEST_FORCE_IDR_FRAME:
            if (p_enc_ctx->frame_num ==
                api_param->reconf_hash[g_reconfigCount][0])
            {
                ni_force_idr_frame_type(p_enc_ctx);
                ni_log(NI_LOG_DEBUG, "%s(): frame #%lu force IDR frame\n", __func__,
                               p_enc_ctx->frame_num);
                g_reconfigCount++;
            }
            break;
        case XCODER_TEST_RECONF_BR_API:
            if (p_enc_ctx->frame_num ==
                api_param->reconf_hash[g_reconfigCount][0])
            {
                ni_reconfig_bitrate(p_enc_ctx,
                                    api_param->reconf_hash[g_reconfigCount][1]);
                ni_log(NI_LOG_DEBUG, "%s(): frame #%lu API reconfig BR %d\n",
                               __func__, p_enc_ctx->frame_num,
                               api_param->reconf_hash[g_reconfigCount][1]);
                g_reconfigCount++;
            }
            break;
        case XCODER_TEST_RECONF_INTRAPRD_API:
            if (p_enc_ctx->frame_num ==
                api_param->reconf_hash[g_reconfigCount][0]) {
                int32_t intraprd = 
                    api_param->reconf_hash[g_reconfigCount][1];
                ni_reconfig_intraprd(p_enc_ctx, intraprd);
                ni_log(NI_LOG_TRACE,
                        "xcoder_send_frame: frame #%lu API reconfig intraPeriod %d\n",
                        p_enc_ctx->frame_num,
                        intraprd);

                g_reconfigCount++;
            }
            break;
        case XCODER_TEST_RECONF_VUI_HRD_API:
            if (p_enc_ctx->frame_num ==
                api_param->reconf_hash[g_reconfigCount][0])
            {
                ni_vui_hrd_t vui;
                vui.colorDescPresent =
                    (uint8_t)api_param->reconf_hash[g_reconfigCount][1];
                vui.colorPrimaries =
                    (uint8_t)api_param->reconf_hash[g_reconfigCount][2];
                vui.colorTrc =
                    (uint8_t)api_param->reconf_hash[g_reconfigCount][3];
                vui.colorSpace =
                    (uint8_t)api_param->reconf_hash[g_reconfigCount][4];
                vui.aspectRatioWidth =
                    (uint8_t)api_param->reconf_hash[g_reconfigCount][5];
                vui.aspectRatioHeight =
                    (uint8_t)api_param->reconf_hash[g_reconfigCount][6];
                vui.videoFullRange =
                    (uint8_t)api_param->reconf_hash[g_reconfigCount][7];

                ni_reconfig_vui(p_enc_ctx, &vui);
                ni_log(NI_LOG_DEBUG, "%s(): frame #%lu API reconfig VUI HRD "
                    "colorDescPresent %d colorPrimaries %d "
                    "colorTrc %d colorSpace %d aspectRatioWidth %d "
                    "aspectRatioHeight %d videoFullRange %d\n",
                    __func__, p_enc_ctx->frame_num,
                    api_param->reconf_hash[g_reconfigCount][1],
                    api_param->reconf_hash[g_reconfigCount][2],
                    api_param->reconf_hash[g_reconfigCount][3],
                    api_param->reconf_hash[g_reconfigCount][4],
                    api_param->reconf_hash[g_reconfigCount][5],
                    api_param->reconf_hash[g_reconfigCount][6],
                    api_param->reconf_hash[g_reconfigCount][7]);
                g_reconfigCount++;
            }
            break;
        case XCODER_TEST_RECONF_LTR_API:
            if (p_enc_ctx->frame_num ==
                api_param->reconf_hash[g_reconfigCount][0])
            {
                ni_long_term_ref_t ltr;
                ltr.use_cur_src_as_long_term_pic =
                    (uint8_t)api_param->reconf_hash[g_reconfigCount][1];
                ltr.use_long_term_ref =
                    (uint8_t)api_param->reconf_hash[g_reconfigCount][2];

                ni_set_ltr(p_enc_ctx, &ltr);
                ni_log(NI_LOG_DEBUG, "%s(): frame #%lu API set LTR\n", __func__,
                               p_enc_ctx->frame_num);
                g_reconfigCount++;
            }
            break;
        case XCODER_TEST_RECONF_RC_MIN_MAX_QP_API:
        case XCODER_TEST_RECONF_RC_MIN_MAX_QP_API_REDUNDANT:
            if (p_enc_ctx->frame_num ==
                api_param->reconf_hash[g_reconfigCount][0])
            {
                ni_rc_min_max_qp qp_info;
                qp_info.minQpI = (int32_t)api_param->reconf_hash[g_reconfigCount][1];
                qp_info.maxQpI = (int32_t)api_param->reconf_hash[g_reconfigCount][2];
                qp_info.maxDeltaQp = (int32_t)api_param->reconf_hash[g_reconfigCount][3];
                qp_info.minQpPB = (int32_t)api_param->reconf_hash[g_reconfigCount][4];
                qp_info.maxQpPB = (int32_t)api_param->reconf_hash[g_reconfigCount][5];
                ni_reconfig_min_max_qp(p_enc_ctx, &qp_info);
                ni_log(NI_LOG_DEBUG,
                    "%s(): frame %llu minQpI %d maxQpI %d maxDeltaQp %d minQpPB %d maxQpPB %d\n",
                    __func__, p_enc_ctx->frame_num,
                    qp_info.minQpI, qp_info.maxQpI, qp_info.maxDeltaQp, qp_info.minQpPB, qp_info.maxQpPB);
                g_reconfigCount++;
            }
            break;
        case XCODER_TEST_RECONF_LTR_INTERVAL_API:
            if (p_enc_ctx->frame_num ==
                api_param->reconf_hash[g_reconfigCount][0])
            {
                ni_set_ltr_interval(p_enc_ctx,
                                    api_param->reconf_hash[g_reconfigCount][1]);
                ni_log(NI_LOG_DEBUG, "%s(): frame #%lu API set LTR interval %d\n",
                               __func__, p_enc_ctx->frame_num,
                               api_param->reconf_hash[g_reconfigCount][1]);
                g_reconfigCount++;
            }
            break;
        case XCODER_TEST_INVALID_REF_FRAME_API:
            if (p_enc_ctx->frame_num ==
                api_param->reconf_hash[g_reconfigCount][0])
            {
                ni_set_frame_ref_invalid(
                    p_enc_ctx, api_param->reconf_hash[g_reconfigCount][1]);
                ni_log(NI_LOG_DEBUG, "%s(): frame #%lu API set frame ref invalid "
                               "%d\n",
                               __func__, p_enc_ctx->frame_num,
                               api_param->reconf_hash[g_reconfigCount][1]);
                g_reconfigCount++;
            }
            break;
        case XCODER_TEST_RECONF_FRAMERATE_API:
            if (p_enc_ctx->frame_num ==
                api_param->reconf_hash[g_reconfigCount][0])
            {
                ni_framerate_t framerate;
                framerate.framerate_num =
                    (int32_t)api_param->reconf_hash[g_reconfigCount][1];
                framerate.framerate_denom =
                    (int32_t)api_param->reconf_hash[g_reconfigCount][2];
                ni_reconfig_framerate(p_enc_ctx, &framerate);
                ni_log(NI_LOG_DEBUG, 
                    "%s(): frame #%lu API reconfig framerate (%d/%d)\n",
                    __func__, p_enc_ctx->frame_num,
                    api_param->reconf_hash[g_reconfigCount][1],
                    api_param->reconf_hash[g_reconfigCount][2]);
                g_reconfigCount++;
            }
            break;
        case XCODER_TEST_RECONF_MAX_FRAME_SIZE_API:
            if (p_enc_ctx->frame_num ==
                api_param->reconf_hash[g_reconfigCount][0])
            {
                ni_reconfig_max_frame_size(p_enc_ctx,
                                    api_param->reconf_hash[g_reconfigCount][1]);
                ni_log(NI_LOG_DEBUG, "%s(): frame #%lu API reconfig maxFrameSize %d\n",
                               __func__, p_enc_ctx->frame_num,
                               api_param->reconf_hash[g_reconfigCount][1]);
                g_reconfigCount++;
            }
            break;          
        case XCODER_TEST_CRF_API:
            if (p_enc_ctx->frame_num ==
                api_param->reconf_hash[g_reconfigCount][0])
            {
                ni_reconfig_crf(p_enc_ctx,
                                    api_param->reconf_hash[g_reconfigCount][1]);
                ni_log(NI_LOG_DEBUG, "%s(): frame #%lu API reconfig crf %d\n",
                               __func__, p_enc_ctx->frame_num,
                               api_param->reconf_hash[g_reconfigCount][1]);
                g_reconfigCount++;
            }
            break;
        case XCODER_TEST_CRF_FLOAT_API:
            if (p_enc_ctx->frame_num ==
                api_param->reconf_hash[g_reconfigCount][0])
            {
                float crf = (float)(api_param->reconf_hash[g_reconfigCount][1] +
                    (float)api_param->reconf_hash[g_reconfigCount][2] / 100.0);
                ni_reconfig_crf2(p_enc_ctx, crf);
                ni_log(NI_LOG_DEBUG, "%s(): frame #%lu API reconfig crf %f\n",
                               __func__, p_enc_ctx->frame_num, crf);
                g_reconfigCount++;
            }
            break;
        case XCODER_TEST_RECONF_VBV_API:
            if (p_enc_ctx->frame_num ==
                api_param->reconf_hash[g_reconfigCount][0])
            {
                ni_reconfig_vbv_value(
                    p_enc_ctx, api_param->reconf_hash[g_reconfigCount][1],
                    api_param->reconf_hash[g_reconfigCount][2]);
                ni_log(NI_LOG_DEBUG, "%s(): frame #%lu API reconfig vbvMaxRate %d vbvBufferSize %d\n",
                       __func__, p_enc_ctx->frame_num,
                       api_param->reconf_hash[g_reconfigCount][1],
                       api_param->reconf_hash[g_reconfigCount][2]);
                g_reconfigCount++;
            }
            break;
        case XCODER_TEST_RECONF_MAX_FRAME_SIZE_RATIO_API:
            if (p_enc_ctx->frame_num ==
                api_param->reconf_hash[g_reconfigCount][0])
            {
                ni_reconfig_max_frame_size_ratio(
                    p_enc_ctx, api_param->reconf_hash[g_reconfigCount][1]);
                ni_log(NI_LOG_DEBUG,
                        "xcoder_send_frame: frame #%lu reconf maxFrameSizeRatio %d\n",
                        p_enc_ctx->frame_num, api_param->reconf_hash[g_reconfigCount][1]);

                g_reconfigCount++;
            }
            break;
        case XCODER_TEST_RECONF_SLICE_ARG_API:
            if (p_enc_ctx->frame_num ==
                api_param->reconf_hash[g_reconfigCount][0]) {
                ni_reconfig_slice_arg(
                    p_enc_ctx, api_param->reconf_hash[g_reconfigCount][1]);
                ni_log(NI_LOG_DEBUG,
                        "xcoder_send_frame: frame #%lu API reconfig sliceArg %d\n",
                        p_enc_ctx->frame_num,
                        api_param->reconf_hash[g_reconfigCount][1]);

                g_reconfigCount++;
            }
            break;
        case XCODER_TEST_RECONF_OFF:
        default:;
    }
}

/*!*****************************************************************************
 *  \brief Read from input file, upload to encoder, retrieve HW descriptor
 *
 *  \param  
 *
 *  \return
 ******************************************************************************/
int upload_send_data_get_desc(ni_session_context_t *p_upl_ctx,
                              ni_session_data_io_t *p_swin_data,   //intermediate for swf
                              ni_session_data_io_t *p_in_data,
                              int input_video_width, int input_video_height,
                              void *yuv_buf, unsigned long *bytes_sent)
{
    int retval, is_semiplanar;
    ni_frame_t *p_in_frame = &p_in_data->data.frame;       //hwframe
    ni_frame_t *p_swin_frame = &p_swin_data->data.frame;   //swframe
    niFrameSurface1_t *dst_surf = NULL;

    ni_log(NI_LOG_DEBUG, "===> upload_send_data <===\n");

    p_in_frame->start_of_stream = 0;
    p_in_frame->end_of_stream = yuv_buf == NULL;
    p_in_frame->force_key_frame = 0;
    p_in_frame->video_width = p_swin_frame->video_width = input_video_width;
    p_in_frame->video_height = p_swin_frame->video_height = input_video_height;
    // only metadata header for now
    p_in_frame->extra_data_len = NI_APP_ENC_FRAME_META_DATA_SIZE;
    p_swin_frame->extra_data_len = NI_APP_ENC_FRAME_META_DATA_SIZE;

    int dst_stride[NI_MAX_NUM_DATA_POINTERS] = {0};
    int dst_height_aligned[NI_MAX_NUM_DATA_POINTERS] = {0};
    ni_get_min_frame_dim(
            input_video_width, input_video_height,
            p_upl_ctx->pixel_format, dst_stride,
            dst_height_aligned);
    is_semiplanar = (
        get_pixel_planar(p_upl_ctx->pixel_format) == NI_PIXEL_PLANAR_FORMAT_SEMIPLANAR);
    ni_encoder_sw_frame_buffer_alloc(
            !is_semiplanar, p_swin_frame, input_video_width,
            dst_height_aligned[0], dst_stride, 0,
            (int)p_swin_frame->extra_data_len, false);
    if (!p_swin_frame->p_data[0])
    {
        ni_log(NI_LOG_ERROR, "Error: could not allocate YUV frame buffer!");
        return -1;
    }

    //can also be ni_frame_buffer_alloc()
    ni_frame_buffer_alloc_hwenc(p_in_frame, input_video_width,
                                input_video_height,
                                (int)p_in_frame->extra_data_len);
    if (!p_in_frame->p_data[3])
    {
        ni_log(NI_LOG_ERROR, "Error: could not allocate hw frame buffer!");
        return -1;
    }

    dst_surf = (niFrameSurface1_t *)p_in_frame->p_data[3];

    ni_log(NI_LOG_DEBUG, "p_dst alloc linesize = %d/%d/%d  src height=%d  "
                   "dst height aligned = %d/%d/%d\n",
                   dst_stride[0], dst_stride[1], dst_stride[2],
                   input_video_height, dst_height_aligned[0],
                   dst_height_aligned[1], dst_height_aligned[2]);

    if (p_in_frame->end_of_stream)
    {
        goto hwupload;
    }

    uint8_t *p_src[NI_MAX_NUM_DATA_POINTERS] = {NULL};
    int src_stride[NI_MAX_NUM_DATA_POINTERS] = {0};
    int src_height[NI_MAX_NUM_DATA_POINTERS] = {0};
    src_height[0] = input_video_height;
    src_height[1] = input_video_height / 2;
    src_height[2] = (is_semiplanar) ? 0 : (input_video_height / 2);
    uint32_t conf_win_right = 0;

    switch (p_upl_ctx->pixel_format)
    {
    case NI_PIX_FMT_RGBA:
    case NI_PIX_FMT_BGRA:
    case NI_PIX_FMT_ABGR:
    case NI_PIX_FMT_ARGB:
    case NI_PIX_FMT_BGR0:
        src_stride[0] = input_video_width * p_upl_ctx->bit_depth_factor;
        src_height[0] = input_video_height;
        src_height[1] = 0;
        src_height[2] = 0;
        p_src[0] = yuv_buf;
        break;
    case NI_PIX_FMT_NV12:
    case NI_PIX_FMT_P010LE:
    case NI_PIX_FMT_YUV420P:
    case NI_PIX_FMT_YUV420P10LE:
        src_stride[0] = input_video_width * p_upl_ctx->bit_depth_factor;
        src_stride[1] = is_semiplanar ? src_stride[0] : src_stride[0] / 2;
        src_stride[2] = is_semiplanar ? 0 : src_stride[0] / 2;

        p_src[0] = yuv_buf;
        p_src[1] = p_src[0] + src_stride[0] * src_height[0];
        p_src[2] = p_src[1] + src_stride[1] * src_height[1];
        if (input_video_width < NI_MIN_WIDTH)
        {
            conf_win_right += (NI_MIN_WIDTH - input_video_width) / 2 * 2;
        } else
        {
            conf_win_right += (NI_VPU_CEIL(input_video_width, 2) - input_video_width) / 2 * 2;
        }
        break;
    default:
        ni_log(NI_LOG_ERROR, "%s: Error Invalid pixel format %s\n", __func__,
                ni_pixel_format_name(p_upl_ctx->pixel_format));
        return -1;
    }

    ni_copy_frame_data(
        (uint8_t **)(p_swin_frame->p_data),
        p_src, input_video_width,
        input_video_height, p_upl_ctx->bit_depth_factor,
        p_upl_ctx->pixel_format, conf_win_right, dst_stride,
        dst_height_aligned, src_stride, src_height);

hwupload:
    retval = ni_device_session_hwup(p_upl_ctx, p_swin_data, dst_surf);
    if (retval < 0)
    {
        ni_log(NI_LOG_ERROR, "Error: ni_device_session_hwup():%d, frameNum %u\n",
                retval, number_of_frames);
        return -1;
    } else
    {
        *bytes_sent += p_swin_frame->data_len[0] + p_swin_frame->data_len[1] +
            p_swin_frame->data_len[2] + p_swin_frame->data_len[3];
        ni_log(NI_LOG_DEBUG, "upload_send_data: total sent data size=%lu\n",
               *bytes_sent);

        dst_surf->ui16width = input_video_width;
        dst_surf->ui16height = input_video_height;
        dst_surf->encoding_type = is_semiplanar ?
            NI_PIXEL_PLANAR_FORMAT_SEMIPLANAR :
            NI_PIXEL_PLANAR_FORMAT_PLANAR;

        number_of_frames++;

        ni_log(NI_LOG_DEBUG, "upload_send_data: FID = %d success, number:%u\n",
               dst_surf->ui16FrameIdx, number_of_frames);
    }

    return 0;
}

/*!*****************************************************************************
 *  \brief Read from input file, send to uploader for hw scaling,
 *         retrieve HW descriptor
 *
 *  \param
 *
 *  \return
 ******************************************************************************/
int upload_send_data_get_desc2(ni_session_context_t *p_upl_ctx,
                              ni_session_data_io_t *p_swin_data,   //intermediate for swf
                              ni_session_data_io_t *p_in_data,
                              int input_video_width, int input_video_height,
                              void *yuv_buf, unsigned long *bytes_sent)
{
    int retval, is_semiplanar;
    ni_frame_t *p_in_frame = &p_in_data->data.frame;       //hwframe
    ni_frame_t *p_swin_frame = &p_swin_data->data.frame;   //swframe
    niFrameSurface1_t *dst_surf = NULL;

    ni_log(NI_LOG_DEBUG, "===> upload_send_data <===\n");

    p_in_frame->start_of_stream = 0;
    p_in_frame->end_of_stream = yuv_buf == NULL;
    p_in_frame->force_key_frame = 0;
    p_in_frame->video_width = p_swin_frame->video_width = input_video_width;
    p_in_frame->video_height = p_swin_frame->video_height = input_video_height;
    // only metadata header for now
    p_in_frame->extra_data_len = NI_APP_ENC_FRAME_META_DATA_SIZE;
    p_swin_frame->extra_data_len = NI_APP_ENC_FRAME_META_DATA_SIZE;

    int dst_stride[NI_MAX_NUM_DATA_POINTERS] = {0};
    int dst_height_aligned[NI_MAX_NUM_DATA_POINTERS] = {0};
    is_semiplanar = (
        get_pixel_planar(p_upl_ctx->pixel_format) == NI_PIXEL_PLANAR_FORMAT_SEMIPLANAR);
    dst_height_aligned[0] = NI_VPU_CEIL(input_video_height, 2);
    dst_height_aligned[2] = dst_height_aligned[1] = dst_height_aligned[0] / 2;
    switch (p_upl_ctx->pixel_format)
    {
    case NI_PIX_FMT_RGBA:
    case NI_PIX_FMT_BGRA:
    case NI_PIX_FMT_ABGR:
    case NI_PIX_FMT_ARGB:
    case NI_PIX_FMT_BGR0:
        dst_stride[0] = NI_VPU_CEIL(input_video_width, 16) * p_upl_ctx->bit_depth_factor;
        dst_height_aligned[2] = dst_height_aligned[1] = 0;
        break;
    case NI_PIX_FMT_NV12:
        dst_stride[0] = NI_VPU_CEIL(input_video_width, 128);
        dst_stride[1] = dst_stride[0];
        dst_height_aligned[2] = 0;
        break;
    case NI_PIX_FMT_P010LE:
        dst_stride[0] = NI_VPU_CEIL(input_video_width * 2, 128);
        dst_stride[1] = dst_stride[0];
        dst_height_aligned[2] = 0;
        break;
    case NI_PIX_FMT_YUV420P:
        dst_stride[0] = NI_VPU_CEIL(input_video_width, 128);
        dst_stride[1] = NI_VPU_CEIL((input_video_width / 2), 128);
        dst_stride[2] = dst_stride[1];
        break;
    case NI_PIX_FMT_YUV420P10LE:
        dst_stride[0] = NI_VPU_CEIL(input_video_width * 2, 128);
        dst_stride[1] = NI_VPU_CEIL(input_video_width, 128);
        dst_stride[2] = dst_stride[1];
        break;
    default:
        ni_log(NI_LOG_ERROR, "%s: Error Invalid pixel format %s\n", __func__,
                ni_pixel_format_name(p_upl_ctx->pixel_format));
        return -1;
    }

    ni_frame_buffer_alloc_pixfmt(p_swin_frame, p_upl_ctx->pixel_format,
                                 input_video_width, input_video_height,
                                 dst_stride,
                                 p_upl_ctx->codec_format == NI_CODEC_FORMAT_H264,
                                 (int)p_swin_frame->extra_data_len);
    if (!p_swin_frame->p_data[0])
    {
        ni_log(NI_LOG_ERROR, "Error: could not allocate YUV frame buffer!");
        return -1;
    }

    // alloc dest avframe buff
    //can also be ni_frame_buffer_alloc()
    ni_frame_buffer_alloc_hwenc(p_in_frame, input_video_width,
                                input_video_height,
                                (int)p_in_frame->extra_data_len);
    if (!p_in_frame->p_data[3])
    {
        ni_log(NI_LOG_ERROR, "Error: could not allocate hw frame buffer!");
        return -1;
    }

    dst_surf = (niFrameSurface1_t *)p_in_frame->p_data[3];

    ni_log(NI_LOG_DEBUG, "p_dst alloc linesize = %d/%d/%d  src height=%d  "
                   "dst height aligned = %d/%d/%d\n",
                   dst_stride[0], dst_stride[1], dst_stride[2],
                   input_video_height, dst_height_aligned[0],
                   dst_height_aligned[1], dst_height_aligned[2]);

    if (p_in_frame->end_of_stream)
    {
        goto hwupload;
    }

    uint8_t *p_src[NI_MAX_NUM_DATA_POINTERS] = {NULL};
    int src_stride[NI_MAX_NUM_DATA_POINTERS] = {0};
    int src_height[NI_MAX_NUM_DATA_POINTERS] = {0};

    switch (p_upl_ctx->pixel_format)
    {
    case NI_PIX_FMT_RGBA:
    case NI_PIX_FMT_BGRA:
    case NI_PIX_FMT_ABGR:
    case NI_PIX_FMT_ARGB:
    case NI_PIX_FMT_BGR0:
        src_stride[0] = input_video_width * p_upl_ctx->bit_depth_factor;
        src_height[0] = input_video_height;
        p_src[0] = yuv_buf;
        break;
    case NI_PIX_FMT_NV12:
    case NI_PIX_FMT_P010LE:
    case NI_PIX_FMT_YUV420P:
    case NI_PIX_FMT_YUV420P10LE:
        src_stride[0] = input_video_width * p_upl_ctx->bit_depth_factor;
        src_stride[1] = is_semiplanar ? src_stride[0] : src_stride[0] / 2;
        src_stride[2] = is_semiplanar ? 0 : src_stride[0] / 2;

        src_height[0] = input_video_height;
        src_height[1] = src_height[0] / 2;
        src_height[2] = is_semiplanar ? 0 : src_height[1];

        p_src[0] = yuv_buf;
        p_src[1] = p_src[0] + src_stride[0] * src_height[0];
        p_src[2] = p_src[1] + src_stride[1] * src_height[1];
        break;
    default:
        ni_log(NI_LOG_ERROR, "%s: Error Invalid pixel format %s\n", __func__,
                ni_pixel_format_name(p_upl_ctx->pixel_format));
        return -1;
    }

    ni_copy_frame_data(
        (uint8_t **)(p_swin_frame->p_data),
        p_src, input_video_width,
        input_video_height, p_upl_ctx->bit_depth_factor,
        p_upl_ctx->pixel_format, 0, dst_stride,
        dst_height_aligned, src_stride, src_height);

hwupload:
    retval = ni_device_session_hwup(p_upl_ctx, p_swin_data, dst_surf);
    if (retval < 0)
    {
        ni_log(NI_LOG_ERROR, "Error: ni_device_session_hwup():%d, frameNum %u\n",
                retval, number_of_frames);
        return -1;
    } else
    {
        *bytes_sent += p_swin_frame->data_len[0] + p_swin_frame->data_len[1] +
            p_swin_frame->data_len[2] + p_swin_frame->data_len[3];
        ni_log(NI_LOG_DEBUG, "upload_send_data: total sent data size=%lu\n",
               *bytes_sent);

        dst_surf->ui16width = input_video_width;
        dst_surf->ui16height = input_video_height;
        dst_surf->encoding_type = is_semiplanar ?
            NI_PIXEL_PLANAR_FORMAT_SEMIPLANAR :
            NI_PIXEL_PLANAR_FORMAT_PLANAR;

        number_of_frames++;

        ni_log(NI_LOG_DEBUG, "upload_send_data: FID = %d success, number:%u\n",
               dst_surf->ui16FrameIdx, number_of_frames);
    }

    return 0;
}

/*!*****************************************************************************
 *  \brief  Send encoder input data, read from input file
 *
 *  Note: For optimal performance, yuv_buf should be 4k aligned
 *
 *  \param
 *
 *  \return
 ******************************************************************************/
int encoder_send_data(ni_session_context_t *p_enc_ctx,
                      ni_session_data_io_t *p_in_data, void *yuv_buf,
                      int input_video_width, int input_video_height,
                      unsigned long *bytes_sent, device_state_t *p_device_state,
                      int is_last_input, ni_rate_emu_t *p_rate_emu)
{
    int oneSent;
    ni_frame_t *p_in_frame = &p_in_data->data.frame;

    ni_log(NI_LOG_DEBUG, "===> encoder_send_data <===\n");

    if (p_enc_ctx->session_run_state == SESSION_RUN_STATE_SEQ_CHANGE_DRAINING)
    {
        ni_log(NI_LOG_DEBUG, "encoder_send_data: Sequence Change - waiting "
               "for previous session to end\n");
        return NI_TEST_RETCODE_SUCCESS;
    }

    if (p_device_state->enc_eos_sent == 1)
    {
        ni_log(NI_LOG_DEBUG, "encoder_send_data: ALL data (incl. eos) sent "
                       "already!\n");
        return NI_TEST_RETCODE_SUCCESS;
    }

    if (p_device_state->enc_resend)
    {
        goto send_frame;
    }

    p_in_frame->start_of_stream = 0;
    if (!p_device_state->enc_sos_sent ||
        p_enc_ctx->session_run_state == SESSION_RUN_STATE_SEQ_CHANGE_OPENING)
    {
        p_device_state->enc_sos_sent = 1;
        p_in_frame->start_of_stream = 1;
    }
    p_in_frame->end_of_stream = 0;
    p_in_frame->force_key_frame = 0;

    p_in_frame->video_width = input_video_width;
    p_in_frame->video_height = input_video_height;

    // reset encoder change data buffer
    memset(p_enc_ctx->enc_change_params, 0, sizeof(ni_encoder_change_params_t));

    // reset various aux data size
    p_in_frame->roi_len = 0;
    p_in_frame->reconf_len = 0;
    p_in_frame->sei_total_len = 0;

    // collect encode reconfig and demo info and save them as aux data in
    // the input frame struct.
    prep_reconf_demo_data(p_enc_ctx, p_in_frame);

    if (yuv_buf == NULL)
    {
        if (is_last_input)
        {
            p_in_frame->end_of_stream = 1;
            ni_log(NI_LOG_DEBUG, "encoder_send_data: read chunk size 0, eos!\n");
        }
        else
        {
            ni_log(NI_LOG_DEBUG, "encoder_send_data: exit to get next input\n");
            return NI_TEST_RETCODE_NEXT_INPUT;
        }
    }

send_frame:
    // simulate FFmpeg -re option, which controls process input rate to simualte real time environment
    if (p_rate_emu->rate_emu_framerate)
    {
        uint64_t abs_time_ns;
        abs_time_ns = ni_gettime_ns();
        if ((abs_time_ns - p_rate_emu->rate_emu_start) < (uint64_t)(1000000000LL / p_rate_emu->rate_emu_framerate * p_rate_emu->rate_emu_input_frames))
        {
           if (!p_device_state->enc_resend)
               p_device_state->enc_resend = 2;
           if (p_device_state->enc_resend == 2)
               return NI_TEST_RETCODE_SUCCESS;
        }
        else
        {
            p_rate_emu->rate_emu_input_frames++;
        }
    }
    
    oneSent = ni_enc_write_from_yuv_buffer(p_enc_ctx, p_in_frame, yuv_buf);
    if (oneSent < 0)
    {
        ni_log(NI_LOG_ERROR,
                "Error: failed ni_device_session_write() for encoder\n");
        p_device_state->enc_resend = 1;
        return NI_TEST_RETCODE_FAILURE;
    } else if (oneSent == 0 && !p_enc_ctx->ready_to_close)
    {
        p_device_state->enc_resend = 1;
        return NI_TEST_RETCODE_EAGAIN;
    } else
    {
        p_device_state->enc_resend = 0;
        number_of_frames++;

        *bytes_sent += p_in_frame->data_len[0] + p_in_frame->data_len[1] +
            p_in_frame->data_len[2] + p_in_frame->data_len[3];
        ni_log(NI_LOG_DEBUG, "encoder_send_data: total sent data size=%lu\n",
                       *bytes_sent);

        ni_log(NI_LOG_DEBUG, "encoder_send_data: success\n");
        
        if (p_enc_ctx->ready_to_close)
        {
            if (p_enc_ctx->session_run_state == SESSION_RUN_STATE_SEQ_CHANGE_DRAINING)
            {
                // The sequence change YUV frame will be read again next time.
                number_of_frames--;
            }
            else
            {
                p_device_state->enc_eos_sent = 1;
            }
        }

        if (p_enc_ctx->session_run_state == SESSION_RUN_STATE_SEQ_CHANGE_OPENING)
        {
            p_enc_ctx->session_run_state = SESSION_RUN_STATE_NORMAL;
            ni_log(NI_LOG_DEBUG,
                   "encoder_send_data: session_run_state change to %d\n",
                   p_enc_ctx->session_run_state);
        }
    }

    ni_frame_wipe_aux_data(p_in_frame);
    return NI_TEST_RETCODE_SUCCESS;
}

/*******************************************************************************
 *  @brief  Send encoder input data, directly after receiving from decoder
 *
 *  @param  p_enc_ctx encoder context
 *          p_dec_ctx decoder context
 *          p_dec_out_data frame returned by decoder
 *          p_enc_in_data  frame to be sent to encoder
 *
 *  @return
 ******************************************************************************/
int encoder_send_data2(ni_session_context_t *p_enc_ctx,
                       ni_session_data_io_t *p_dec_out_data,
                       ni_session_data_io_t *p_enc_in_data,
                       int input_video_width, int input_video_height,
                       unsigned long *bytes_sent, device_state_t *p_device_state)
{
    int oneSent;
    // pointer to data struct to be sent
    ni_session_data_io_t *p_to_send = NULL;
    // frame pointer to data frame struct to be sent
    ni_frame_t *p_in_frame = NULL;
    ni_xcoder_params_t *api_params =
        (ni_xcoder_params_t *)p_enc_ctx->p_session_config;
    int is_semiplanar = get_pixel_planar(p_enc_ctx->pixel_format) == NI_PIXEL_PLANAR_FORMAT_SEMIPLANAR;
    int is_hwframe = p_enc_ctx->hw_action != NI_CODEC_HW_NONE;

    ni_log(NI_LOG_DEBUG, "===> encoder_send_data2 <===\n");

    if (p_device_state->enc_eos_sent == 1)
    {
        ni_log(NI_LOG_DEBUG, "encoder_send_data2: ALL data (incl. eos) sent "
                       "already!\n");
        return 1;
    }

    // frame resend
    if (p_device_state->enc_resend)
    {
        p_to_send = p_enc_in_data;
        p_in_frame = &(p_to_send->data.frame);
        goto send_frame;
    }

    // if the source and target are of the same codec type, AND there is no
    // other aux data such as close caption, HDR10 etc, AND no padding required
    // (e.g. in the case of 32x32 transcoding that needs padding to 256x128),
    // then reuse the YUV frame data layout passed in because it's already in
    // the format required by VPU
    // Note: for now disable the reuse of decode frame !
    if (0/*p_enc_ctx->codec_format == dec_codec_format &&
        input_video_width >= NI_MIN_WIDTH &&
        input_video_height >= NI_MIN_HEIGHT &&
        !p_dec_out_data->data.frame.sei_hdr_content_light_level_info_len &&
        !p_dec_out_data->data.frame.sei_hdr_mastering_display_color_vol_len &&
        !p_dec_out_data->data.frame.sei_hdr_plus_len &&
        !p_dec_out_data->data.frame.sei_cc_len &&
        !p_dec_out_data->data.frame.sei_user_data_unreg_len &&
        !p_dec_out_data->data.frame.roi_len*/)
    {
        ni_log(NI_LOG_DEBUG, 
            "encoder_send_data2: encoding to the same codec "
            "format as the source: %u, NO SEI, reusing the frame struct!\n",
            p_enc_ctx->codec_format);
        p_to_send = p_dec_out_data;
        p_in_frame = &(p_to_send->data.frame);

        p_in_frame->force_key_frame = 0;

        p_in_frame->sei_total_len = p_in_frame->sei_cc_offset =
            p_in_frame->sei_cc_len =
                p_in_frame->sei_hdr_mastering_display_color_vol_offset =
                    p_in_frame->sei_hdr_mastering_display_color_vol_len =
                        p_in_frame->sei_hdr_content_light_level_info_offset =
                            p_in_frame->sei_hdr_content_light_level_info_len =
                                p_in_frame->sei_hdr_plus_offset =
                                    p_in_frame->sei_hdr_plus_len = 0;

        p_in_frame->roi_len = 0;
        p_in_frame->reconf_len = 0;
        p_in_frame->force_pic_qp = 0;
        p_in_frame->extra_data_len = NI_APP_ENC_FRAME_META_DATA_SIZE;
        p_in_frame->ni_pict_type = 0;
    } else
    {
        // otherwise have to pad/crop the source and copy to a new frame struct
        // and prep for the SEI aux data
        p_to_send = p_enc_in_data;
        p_in_frame = &(p_to_send->data.frame);
        p_in_frame->end_of_stream = p_dec_out_data->data.frame.end_of_stream;
        p_in_frame->ni_pict_type = 0;

        // reset encoder change data buffer
        memset(p_enc_ctx->enc_change_params, 0,
               sizeof(ni_encoder_change_params_t));

        // extra data starts with metadata header, and reset various aux data
        // size
        p_in_frame->extra_data_len = NI_APP_ENC_FRAME_META_DATA_SIZE;
        p_in_frame->roi_len = 0;
        p_in_frame->reconf_len = 0;
        p_in_frame->sei_total_len = 0;
        p_in_frame->force_pic_qp = 0;

        // collect encode reconfig and demo info and save them in the decode out
        // frame, to be used in the aux data prep and copy later
        prep_reconf_demo_data(p_enc_ctx, &(p_dec_out_data->data.frame));

        int dst_stride[NI_MAX_NUM_DATA_POINTERS] = {0};
        int dst_height_aligned[NI_MAX_NUM_DATA_POINTERS] = {0};
        bool alignment_2pass_wa = (
                       (api_params->cfg_enc_params.lookAheadDepth || 
                        api_params->cfg_enc_params.crfFloat >= 0) &&
                       (p_enc_ctx->codec_format == NI_CODEC_FORMAT_H265 ||
                        p_enc_ctx->codec_format == NI_CODEC_FORMAT_AV1));
        ni_get_hw_yuv420p_dim(input_video_width, input_video_height,
                              p_enc_ctx->bit_depth_factor, is_semiplanar,
                              dst_stride, dst_height_aligned);

	if (alignment_2pass_wa && !is_hwframe) {
            if (is_semiplanar) {
                // for 2-pass encode output mismatch WA, need to extend (and
                // pad) CbCr plane height, because 1st pass assume input 32
                // align
                dst_height_aligned[1] = (((dst_height_aligned[0] + 31) / 32) * 32) / 2;
            } else {
                // for 2-pass encode output mismatch WA, need to extend (and
                // pad) Cr plane height, because 1st pass assume input 32 align
                dst_height_aligned[2] = (((dst_height_aligned[0] + 31) / 32) * 32) / 2;
            }
        }

        // ROI demo mode takes higher priority over aux data
        // Note: when ROI demo modes enabled, supply ROI map for the specified
        //       range frames, and 0 map for others
        if (api_params->roi_demo_mode && api_params->cfg_enc_params.roi_enable)
        {
            if (p_enc_ctx->frame_num > 90 && p_enc_ctx->frame_num < 300)
            {
                p_in_frame->roi_len = p_enc_ctx->roi_len;
            } else
            {
                p_in_frame->roi_len = 0;
            }
            // when ROI enabled, always have a data buffer for ROI
            // Note: this is handled separately from ROI through side/aux data
            p_in_frame->extra_data_len += p_enc_ctx->roi_len;
        }

        int should_send_sei_with_frame = ni_should_send_sei_with_frame(
            p_enc_ctx, p_in_frame->ni_pict_type, api_params);

        // data buffer for various SEI: HDR mastering display color volume, HDR
        // content light level, close caption, User data unregistered, HDR10+
        // etc.
        uint8_t mdcv_data[NI_MAX_SEI_DATA];
        uint8_t cll_data[NI_MAX_SEI_DATA];
        uint8_t cc_data[NI_MAX_SEI_DATA];
        uint8_t udu_data[NI_MAX_SEI_DATA];
        uint8_t hdrp_data[NI_MAX_SEI_DATA];

        // prep for auxiliary data (various SEI, ROI) in p_in_frame, based on
        // the data returned in decoded frame and also reconfig and demo modes
        // collected in prep_reconf_demo_data
        ni_enc_prep_aux_data(
            p_enc_ctx, p_in_frame, &(p_dec_out_data->data.frame),
            p_enc_ctx->codec_format, should_send_sei_with_frame, mdcv_data,
            cll_data, cc_data, udu_data, hdrp_data);

        p_in_frame->extra_data_len += p_in_frame->sei_total_len;

        // data layout requirement: leave space for reconfig data if at least
        // one of reconfig, SEI or ROI is present
        // Note: ROI is present when enabled, so use encode config flag instead
        //       of frame's roi_len as it can be 0 indicating a 0'd ROI map
        //       setting !
        if (p_in_frame->reconf_len || p_in_frame->sei_total_len ||
            (api_params->roi_demo_mode &&
             api_params->cfg_enc_params.roi_enable))
        {
            p_in_frame->extra_data_len += sizeof(ni_encoder_change_params_t);
        }

        if (!is_hwframe)
        {
            ni_encoder_sw_frame_buffer_alloc(api_params->cfg_enc_params.planar,
                p_in_frame, input_video_width, input_video_height, dst_stride,
                p_enc_ctx->codec_format == NI_CODEC_FORMAT_H264,
                (int)(p_in_frame->extra_data_len), alignment_2pass_wa);
            if (!p_in_frame->p_data[0])
            {
                ni_log(NI_LOG_ERROR, "Error: cannot allocate YUV frame buffer!");
                return -1;
            }
        } else
        {
            ni_frame_buffer_alloc_hwenc(p_in_frame, input_video_width,
                                        input_video_height,
                                        (int)(p_in_frame->extra_data_len));
            if (!p_in_frame->p_data[3])
            {
                ni_log(NI_LOG_ERROR, "Error: cannot allocate YUV frame buffer!");
                return -1;
            }
        }

        ni_log(NI_LOG_DEBUG, 
            "p_dst alloc linesize = %d/%d/%d  src height=%d  "
            "dst height aligned = %d/%d/%d force_key_frame=%d, "
            "extra_data_len=%u"
            " sei_size=%u (hdr_content_light_level %u hdr_mastering_display_"
            "color_vol %u hdr10+ %u hrd %d) reconf_size=%u roi_size=%u "
            "force_pic_qp=%u udu_sei_size=%u "
            "use_cur_src_as_long_term_pic %u use_long_term_ref %u\n",
            dst_stride[0], dst_stride[1], dst_stride[2], input_video_height,
            dst_height_aligned[0], dst_height_aligned[1], dst_height_aligned[2],
            p_in_frame->force_key_frame, p_in_frame->extra_data_len,
            p_in_frame->sei_total_len,
            p_in_frame->sei_hdr_content_light_level_info_len,
            p_in_frame->sei_hdr_mastering_display_color_vol_len,
            p_in_frame->sei_hdr_plus_len, 0, /* hrd is 0 size for now */
            p_in_frame->reconf_len, p_in_frame->roi_len,
            p_in_frame->force_pic_qp, p_in_frame->sei_user_data_unreg_len,
            p_in_frame->use_cur_src_as_long_term_pic,
            p_in_frame->use_long_term_ref);

        uint8_t *p_src[NI_MAX_NUM_DATA_POINTERS];
        int src_stride[NI_MAX_NUM_DATA_POINTERS];
        int src_height[NI_MAX_NUM_DATA_POINTERS];

        src_height[0] = p_dec_out_data->data.frame.video_height;
        src_height[1] = src_height[2] = src_height[0] / 2;
        src_height[3] = 0;

        src_stride[0] =
            (int)(p_dec_out_data->data.frame.data_len[0]) / src_height[0];
        src_stride[1] =
            (int)(p_dec_out_data->data.frame.data_len[1]) / src_height[1];
        src_stride[2] = src_stride[1];
        if (is_semiplanar)
        {
            src_height[2] = 0;
            src_stride[2] = 0;
        }
        src_stride[3] = 0;

        p_src[0] = p_dec_out_data->data.frame.p_data[0];
        p_src[1] = p_dec_out_data->data.frame.p_data[1];
        p_src[2] = p_dec_out_data->data.frame.p_data[2];
        p_src[3] = p_dec_out_data->data.frame.p_data[3];

        if (!is_hwframe)
        {   // YUV part of the encoder input data layout
            ni_copy_hw_yuv420p(
                (uint8_t **)(p_in_frame->p_data), p_src, input_video_width,
                input_video_height, p_enc_ctx->bit_depth_factor, is_semiplanar,
                ((ni_xcoder_params_t *)p_enc_ctx->p_session_config)
                    ->cfg_enc_params.conf_win_right,
                dst_stride, dst_height_aligned, src_stride, src_height);
        } else
        {
            ni_copy_hw_descriptors((uint8_t **)(p_in_frame->p_data), p_src);
        }
        // auxiliary data part of the encoder input data layout
        ni_enc_copy_aux_data(p_enc_ctx, p_in_frame,
                             &(p_dec_out_data->data.frame),
                             p_enc_ctx->codec_format, mdcv_data, cll_data,
                             cc_data, udu_data, hdrp_data, is_hwframe,
                             is_semiplanar);
    }

    p_in_frame->video_width = input_video_width;
    p_in_frame->video_height = input_video_height;

    p_in_frame->start_of_stream = 0;
    if (!p_device_state->enc_sos_sent)
    {
        p_device_state->enc_sos_sent = 1;
        p_in_frame->start_of_stream = 1;
    }
    // p_in_frame->end_of_stream = 0;

send_frame:
    oneSent = (int)(p_in_frame->data_len[0] + p_in_frame->data_len[1] +
                    p_in_frame->data_len[2] + p_in_frame->data_len[3]);

    if (oneSent > 0 || p_in_frame->end_of_stream)
    {
        oneSent = ni_device_session_write(p_enc_ctx, p_to_send,
                                          NI_DEVICE_TYPE_ENCODER);
        p_in_frame->end_of_stream = 0;
    } else
    {
        goto end_encoder_send_data2;
    }

    if (oneSent < 0)
    {
        ni_log(NI_LOG_ERROR, "Error: encoder_send_data2\n");
        p_device_state->enc_resend = 1;
        return -1;
    } else if (oneSent == 0)
    {
        if (p_device_state->enc_eos_sent == 0 && p_enc_ctx->ready_to_close)
        {
            p_device_state->enc_resend = 0;
            p_device_state->enc_eos_sent = 1;
        } else
        {
            p_device_state->enc_resend = 1;
            return NI_TEST_RETCODE_EAGAIN;
        }
    } else
    {
        p_device_state->enc_resend = 0;

        if (p_enc_ctx->ready_to_close)
        {
            p_device_state->enc_eos_sent = 1;
        }
        ni_log(NI_LOG_DEBUG, "encoder_send_data2: success\n");
    }

end_encoder_send_data2:
    return 0;
}

/*!*****************************************************************************
 *  \brief  Send encoder input data, read from uploader instance hwframe
 *
 *  \param  
 *
 *  \return
 ******************************************************************************/
int encoder_send_data3(ni_session_context_t *p_enc_ctx,
                       ni_session_data_io_t *p_in_data,
                       int input_video_width, int input_video_height,
                       device_state_t *p_device_state, int eos)
{
    int oneSent;
    ni_frame_t *p_in_frame = &(p_in_data->data.frame);

    ni_log(NI_LOG_DEBUG, "===> encoder_send_data3 <===\n");

    if (p_device_state->enc_eos_sent == 1)
    {
        ni_log(NI_LOG_DEBUG, "encoder_send_data3: ALL data (incl. eos) sent "
                       "already!\n");
        return 0;
    }

    if (p_device_state->enc_resend)
    {
        goto send_frame;
    }

    p_in_frame->start_of_stream = 0;
    if (!p_device_state->enc_sos_sent)
    {
        p_device_state->enc_sos_sent = 1;
        p_in_frame->start_of_stream = 1;
    }
    p_in_frame->end_of_stream = eos;
    p_in_frame->force_key_frame = 0;
    p_in_frame->video_width = input_video_width;
    p_in_frame->video_height = input_video_height;
    if (eos)
    {
        ni_log(NI_LOG_DEBUG, "encoder_send_data3: read chunk size 0, eos!\n");
    }

    // only metadata header for now
    p_in_frame->extra_data_len = NI_APP_ENC_FRAME_META_DATA_SIZE;

send_frame:
    oneSent =
        ni_device_session_write(p_enc_ctx, p_in_data, NI_DEVICE_TYPE_ENCODER);
    if (oneSent < 0)
    {
        ni_log(NI_LOG_ERROR,
                "Error: failed ni_device_session_write() for encoder\n");
        p_device_state->enc_resend = 1;
        return -1;
    } else if (oneSent == 0 && !p_enc_ctx->ready_to_close)
    {
        p_device_state->enc_resend = 1;
        ni_log(NI_LOG_DEBUG, "NEEDED TO RESEND");
        return NI_TEST_RETCODE_EAGAIN;
    } else
    {
        p_device_state->enc_resend = 0;

        ni_log(NI_LOG_DEBUG, "encoder_send_data3: total sent data size=%u\n",
                       p_in_frame->data_len[3]);

        ni_log(NI_LOG_DEBUG, "encoder_send_data3: success\n");

        if (p_enc_ctx->ready_to_close)
        {
            p_device_state->enc_eos_sent = 1;
        }
    }

    return 0;
}

/*!*****************************************************************************
 *  \brief  Reopen or reconfig encoder upon sequence change
 *
 *  \param  
 *
 *  \return 0 - success got packet
 *          1 - received eos
 *          2 - got nothing, need retry
 *          -1 - failure
 ******************************************************************************/
int encoder_reinit_session(ni_session_context_t *p_enc_ctx,
                         ni_session_data_io_t *p_in_data,
                         ni_session_data_io_t *p_out_data)
{
    int ret = NI_TEST_RETCODE_SUCCESS;
    int new_stride, ori_stride;
    bool bIsSmallPicture = false;
    ni_frame_t *p_buffered_frame = &(p_in_data->data.frame);
    ni_xcoder_params_t *p_api_param = (ni_xcoder_params_t *)p_enc_ctx->p_session_config;
    int new_width, new_height;
    ni_pix_fmt_t new_pix_fmt;
    bool isnv12frame;
    bool isrgba;
    int src_stride[NI_MAX_NUM_DATA_POINTERS];

    new_width = p_buffered_frame->video_width;
    new_height = p_buffered_frame->video_height;
    new_pix_fmt = p_buffered_frame->pixel_format;

    // check if resolution is zero copy compatible and set linesize according to new resolution
    isnv12frame = (new_pix_fmt == NI_PIX_FMT_NV12 || new_pix_fmt == NI_PIX_FMT_P010LE);

    isrgba = (new_pix_fmt == NI_PIX_FMT_ABGR || new_pix_fmt == NI_PIX_FMT_ARGB
                 || new_pix_fmt == NI_PIX_FMT_RGBA || new_pix_fmt == NI_PIX_FMT_BGRA);

    // NOTE - FFmpeg / Gstreamer users should use linesize array in frame structure instead of src_stride in the following sample code
    src_stride[0] = new_width * p_enc_ctx->bit_depth_factor;
    if (isrgba)
    {
        src_stride[1] = 0;
        src_stride[2] = 0;
    }
    else
    {
        src_stride[1] = isnv12frame ? src_stride[0] : src_stride[0] / 2;
        src_stride[2] = isnv12frame ? 0 : src_stride[0] / 2;
    }

    if (ni_encoder_frame_zerocopy_check(p_enc_ctx,
        p_api_param, new_width, new_height,
        (const int *)src_stride, true) == NI_RETCODE_SUCCESS)
    {
        new_stride = p_api_param->luma_linesize; // new sequence is zero copy compatible
    }
    else
    {
        new_stride = NI_ALIGN(new_width * p_enc_ctx->bit_depth_factor, 128);
    }

    if (p_enc_ctx->ori_luma_linesize && p_enc_ctx->ori_chroma_linesize)
    {
        ori_stride = p_enc_ctx->ori_luma_linesize; // previous sequence was zero copy compatible
    }
    else
    {
        ori_stride = NI_ALIGN(p_enc_ctx->ori_width * p_enc_ctx->bit_depth_factor, 128);
    }

    if (p_api_param->cfg_enc_params.lookAheadDepth) {
        ni_log(NI_LOG_DEBUG, "xcoder_encode_reinit 2-pass "
             "lookaheadDepth %d\n",
             p_api_param->cfg_enc_params.lookAheadDepth);
        if ((new_width < NI_2PASS_ENCODE_MIN_WIDTH) ||
           (new_height < NI_2PASS_ENCODE_MIN_HEIGHT)) {
          bIsSmallPicture = true;
        }
    }
    else {
        if ((new_width < NI_MIN_WIDTH) ||
           (new_height < NI_MIN_HEIGHT)) {
          bIsSmallPicture = true;
        }
    }

    if (p_api_param->cfg_enc_params.multicoreJointMode) {
        ni_log(NI_LOG_DEBUG, "xcoder_encode_reinit multicore "
             "joint mode\n");
        if ((new_width < NI_MULTICORE_ENCODE_MIN_WIDTH) ||
           (new_height < NI_MULTICORE_ENCODE_MIN_HEIGHT)) {
          bIsSmallPicture = true;
        }
    }

    if (p_api_param->cfg_enc_params.crop_width || p_api_param->cfg_enc_params.crop_height) {
        ni_log(NI_LOG_DEBUG, "xcoder_encode_reinit needs to close and re-open "
             "due to crop width x height\n");
        bIsSmallPicture = true;
    }    

    ni_log(NI_LOG_DEBUG, "xcoder_encode_reinit resolution: %dx%d->%dx%d "
         "pix fmt: %d->%d bIsSmallPicture %d codec %d\n",
         ori_stride, p_enc_ctx->ori_height, new_stride, new_height,
         p_enc_ctx->ori_pix_fmt, new_pix_fmt, bIsSmallPicture,
         p_enc_ctx->codec_format);

    // fast sequence change without close / open only if new resolution < original resolution
    if (ori_stride*p_enc_ctx->ori_height < new_stride*new_height ||
        p_enc_ctx->ori_pix_fmt != new_pix_fmt ||
        bIsSmallPicture ||
        p_enc_ctx->codec_format == NI_CODEC_FORMAT_JPEG)
    {
        ni_log(NI_LOG_INFO, "XCoder encode sequence change by close / re-open session\n");
        encoder_close_session(p_enc_ctx, p_in_data, p_out_data);
        ret = encoder_init_session(p_enc_ctx, p_in_data, p_out_data, new_width,
                                   new_height, new_pix_fmt);
        // clear crop parameters upon sequence change because cropping values may not be compatible to new resolution
        p_api_param->cfg_enc_params.crop_width = p_api_param->cfg_enc_params.crop_height = 0;
        p_api_param->cfg_enc_params.hor_offset = p_api_param->cfg_enc_params.ver_offset = 0;
    }
    else
    {
      if (p_enc_ctx->codec_format == NI_CODEC_FORMAT_AV1) {
          // AV1 8x8 alignment HW limitation is now worked around by FW cropping input resolution
          if (new_width % NI_PARAM_AV1_ALIGN_WIDTH_HEIGHT)
              ni_log(NI_LOG_ERROR,
                     "resolution change: AV1 Picture Width not aligned to %d - picture will be cropped\n",
                     NI_PARAM_AV1_ALIGN_WIDTH_HEIGHT);
   
          if (new_height % NI_PARAM_AV1_ALIGN_WIDTH_HEIGHT)
              ni_log(NI_LOG_ERROR,
                     "resolution change: AV1 Picture Height not aligned to %d - picture will be cropped\n",
                     NI_PARAM_AV1_ALIGN_WIDTH_HEIGHT);
      }
      ni_log(NI_LOG_INFO, "XCoder encode sequence change by re-config session (fast path)\n");
      ret = encoder_sequence_change(p_enc_ctx, p_in_data, p_out_data, new_width, new_height, new_pix_fmt);
    }

    // this state is referenced when sending first frame after sequence change
    p_enc_ctx->session_run_state = SESSION_RUN_STATE_SEQ_CHANGE_OPENING;

    ni_log(NI_LOG_DEBUG, "%s: session_run_state change to %d \n", __func__,
           p_enc_ctx->session_run_state);

    return ret;
}

static void write_av1_ivf_header(uint32_t width, uint32_t height,
                                 uint32_t frame_num, uint32_t frame_denom,
                                 FILE *p_file)
{
    static int global_ivf_header = 1;

    // write the global ivf start header
    if (global_ivf_header && p_file != NULL && !av1_output_obu)
    {
        uint8_t start_header[32] = {
            0x44, 0x4b, 0x49, 0x46, /* signature: 'DKIF' */
            0x00, 0x00,             /* version: 0 */
            0x20, 0x00,             /* length of header in bytes: 32 */
            0x41, 0x56, 0x30, 0x31, /* codec FourCC: AV01 */
            0x00, 0x07, /* width in pixels(little endian), default 1280 */
            0xd0, 0x02, /* height in pixels(little endian), default 720 */
            0x1e, 0x00, 0x00, 0x00, /* time base numerator, default 30 */
            0x01, 0x00, 0x00, 0x00, /* time base denominator, default 1 */
            0x00, 0x00, 0x00, 0x00, /* number of frames in file */
            0x00, 0x00, 0x00, 0x00  /* reserved */
        };

        if (width && height)
        {
            start_header[12] = width & 0xff;
            start_header[13] = ((width >> 8) & 0xff);
            start_header[14] = height & 0xff;
            start_header[15] = ((height >> 8) & 0xff);
        }

        if (frame_num && frame_denom)
        {
            start_header[16] = frame_num & 0xff;
            start_header[17] = ((frame_num >> 8) & 0xff);
            start_header[18] = ((frame_num >> 16) & 0xff);
            start_header[19] = ((frame_num >> 24) & 0xff);
            start_header[20] = frame_denom & 0xff;
            start_header[21] = ((frame_denom >> 8) & 0xff);
            start_header[22] = ((frame_denom >> 16) & 0xff);
            start_header[23] = ((frame_denom >> 24) & 0xff);
        }

        if (fwrite(start_header, sizeof(start_header), 1, p_file) != 1)
        {
            ni_log(NI_LOG_ERROR, "Error: writing ivf start header fail!\n");
            ni_log(NI_LOG_ERROR, "Error: ferror rc = %d\n", ferror(p_file));
        }

        global_ivf_header = 0;
    }
}

static void write_av1_ivf_packet(ni_packet_t *p_out_pkt, uint32_t meta_size,
                                 FILE *p_file)
{
    int i;

    if (!p_file)
    {
        return;
    }

    // write ivf frame header
    if (!av1_output_obu)
    {
        uint32_t pts = muxed_number_of_packets;
        uint32_t pkt_size = p_out_pkt->data_len - meta_size;

        if (av1_seq_header_len > 0)
        {
            pkt_size += av1_seq_header_len - meta_size;
        }
        for (i = 0; i < p_out_pkt->av1_buffer_index; i++)
        {
            pkt_size += p_out_pkt->av1_data_len[i] - meta_size;
        }

        // ivf frame header
        // bytes 0-3: size of frame in bytes(not including the 12-byte header
        // byte 4-11: 64-bit pts (here pts=num_of_packets(32-bit), thus here only saves 32-bit
        uint8_t ivf_frame_header[12] = {((pkt_size & 0xff)),
                                        ((pkt_size >> 8) & 0xff),
                                        ((pkt_size >> 16) & 0xff),
                                        ((pkt_size >> 24) & 0xff),
                                        ((pts & 0xff)),
                                        ((pts >> 8) & 0xff),
                                        ((pts >> 16) & 0xff),
                                        ((pts >> 24) & 0xff),
                                        0x00, 0x00, 0x00, 0x00};
        if (fwrite(ivf_frame_header, 12, 1, p_file) != 1)
        {
            ni_log(NI_LOG_ERROR, "Error: writing ivf frame header fail!\n");
            ni_log(NI_LOG_ERROR, "Error: ferror rc = %d\n", ferror(p_file));
        }
    }

    // write the leftover sequence header if there is any
    if (av1_seq_header_len > 0)
    {
        if (fwrite(p_av1_seq_header + meta_size,
                   av1_seq_header_len - meta_size, 1, p_file) != 1)
        {
            ni_log(NI_LOG_ERROR, "Error: writing av1 sequence header fail!\n");
            ni_log(NI_LOG_ERROR, "Error: ferror rc = %d\n", ferror(p_file));
        }
        ni_aligned_free(p_av1_seq_header);
        av1_seq_header_len = 0;
    }

    // write the leftover av1 packets
    for (i = 0; i < p_out_pkt->av1_buffer_index; i++)
    {
        if (fwrite((uint8_t *)p_out_pkt->av1_p_data[i] + meta_size,
                   p_out_pkt->av1_data_len[i] - meta_size, 1, p_file) != 1)
        {
            ni_log(NI_LOG_ERROR, "Error: writing av1 packets fail!\n");
            ni_log(NI_LOG_ERROR, "Error: ferror rc = %d\n", ferror(p_file));
        }
    }

    // write the current packet
    if (fwrite((uint8_t *)p_out_pkt->p_data + meta_size,
               p_out_pkt->data_len - meta_size, 1, p_file) != 1)
    {
        ni_log(NI_LOG_ERROR, "Error: writing av1 packets fail!\n");
        ni_log(NI_LOG_ERROR, "Error: ferror rc = %d\n", ferror(p_file));
    }

    muxed_number_of_packets++;
}

static int write_av1_ivf_trailer(ni_packet_t *p_out_pkt, uint32_t meta_size,
                                 FILE *p_file)
{
    if (p_file)
    {
        // write the leftover packets
        if (p_out_pkt->av1_buffer_index > 0)
        {
            write_av1_ivf_packet(p_out_pkt, meta_size, p_file);
        }

        // update frame_count in ivf start header
        if (muxed_number_of_packets && !av1_output_obu)
        {
            uint8_t frame_cnt[4] = {
                (muxed_number_of_packets & 0xff),
                ((muxed_number_of_packets >> 8) & 0xff),
                ((muxed_number_of_packets >> 16) & 0xff),
                ((muxed_number_of_packets >> 24) & 0xff)};
            fseek(p_file, 24, SEEK_SET);
            if (fwrite(frame_cnt, 4, 1, p_file) != 1)
            {
                ni_log(NI_LOG_ERROR, "Error: failed to update frame_cnt in ivf "
                        "header!\n");
                ni_log(NI_LOG_ERROR, "Error: ferror rc = %d\n", ferror(p_file));
                return -1;
            }
        }
    }

    return 0;
}

/*!*****************************************************************************
 *  \brief  Receive output data from encoder
 *
 *  \param  p_in_data is passed in to specify new frame resolution upon sequence
 *          change
 *
 *  \return 0 - success got packet
 *          1 - received eos
 *          2 - got nothing, need retry
 *          -1 - failure
 ******************************************************************************/
int encoder_receive_data(
    ni_session_context_t *p_enc_ctx, ni_session_data_io_t *p_out_data,
    int output_video_width, int output_video_height, FILE *p_file,
    unsigned long long *total_bytes_received, int print_time,
    volatile unsigned int *nb_pkts_received,
    ni_session_data_io_t * p_in_data)
{
    int packet_size = NI_MAX_TX_SZ;
    int rc = 0;
    int end_flag = 0;
    int rx_size = 0;
    ni_packet_t *p_out_pkt = &(p_out_data->data.packet);
    int meta_size = p_enc_ctx->meta_size;
    ni_xcoder_params_t *p_api_param = (ni_xcoder_params_t *)p_enc_ctx->p_session_config;

    ni_log(NI_LOG_DEBUG, "===> encoder_receive_data <===\n");
    if (NI_INVALID_SESSION_ID == p_enc_ctx->session_id)
    {
        // keep-alive-thread timeout will set session_id to invalid, should exit
        ni_log(NI_LOG_ERROR,
               "encode session id invalid, the session should be closed\n");
        return NI_TEST_RETCODE_FAILURE;
    }

receive_data:
    rc = ni_packet_buffer_alloc(p_out_pkt, packet_size);
    if (rc != NI_RETCODE_SUCCESS)
    {
        ni_log(NI_LOG_ERROR, "Error: malloc packet failed, ret = %d!\n", rc);
        return NI_TEST_RETCODE_FAILURE;
    }

    rc = ni_device_session_read(p_enc_ctx, p_out_data, NI_DEVICE_TYPE_ENCODER);

    end_flag = p_out_pkt->end_of_stream;
    rx_size = rc;

    ni_log(NI_LOG_DEBUG, "encoder_receive_data: received data size=%d\n", rx_size);

    if (rx_size > meta_size)
    {
        if (p_enc_ctx->codec_format == NI_CODEC_FORMAT_AV1)
        {
            if (p_enc_ctx->pkt_num == 0)
            {
                write_av1_ivf_header(output_video_width, output_video_height,
                                     p_api_param->fps_number,
                                     p_api_param->fps_denominator, p_file);
                // store the sequence header for next packet writing
                p_av1_seq_header = (uint8_t *)p_out_pkt->p_data;
                av1_seq_header_len = p_out_pkt->data_len;
                p_out_pkt->p_buffer = NULL;
                p_out_pkt->p_data = NULL;
                p_out_pkt->buffer_size = 0;
                p_out_pkt->data_len = 0;
            } else
            {
                // store the av1 unshown frames for next packet writing
                if (!p_out_pkt->av1_show_frame)
                {
                    p_out_pkt->av1_p_buffer[p_out_pkt->av1_buffer_index] =
                        p_out_pkt->p_buffer;
                    p_out_pkt->av1_p_data[p_out_pkt->av1_buffer_index] =
                        p_out_pkt->p_data;
                    p_out_pkt->av1_buffer_size[p_out_pkt->av1_buffer_index] =
                        p_out_pkt->buffer_size;
                    p_out_pkt->av1_data_len[p_out_pkt->av1_buffer_index] =
                        p_out_pkt->data_len;
                    p_out_pkt->av1_buffer_index++;
                    p_out_pkt->p_buffer = NULL;
                    p_out_pkt->p_data = NULL;
                    p_out_pkt->buffer_size = 0;
                    p_out_pkt->data_len = 0;
                    if (p_out_pkt->av1_buffer_index >= MAX_AV1_ENCODER_GOP_NUM)
                    {
                        ni_log(NI_LOG_ERROR, "Error: recv AV1 not shown frame "
                               "number %d >= %d\n", p_out_pkt->av1_buffer_index,
                               MAX_AV1_ENCODER_GOP_NUM);
                        return NI_TEST_RETCODE_FAILURE;
                    }
                } else
                {
                    ni_log(NI_LOG_DEBUG, "AV1 output packet "
                           "pts %lld  dts %lld\n", p_out_pkt->pts, p_out_pkt->dts);
                    write_av1_ivf_packet(p_out_pkt, p_enc_ctx->meta_size, p_file);
                    ni_packet_buffer_free_av1(p_out_pkt);
                }

                // recycle hw frame before next read
                if (p_enc_ctx->hw_action)
                {
                    // encoder only returns valid recycle index
                    // when there's something to recycle.
                    // This range is suitable for all memory bins
                    if (p_out_pkt->recycle_index > 0 &&
                        p_out_pkt->recycle_index <
                        NI_GET_MAX_HWDESC_FRAME_INDEX(p_enc_ctx->ddr_config))
                    {
                        ni_hw_frame_unref(p_out_pkt->recycle_index);
                        p_out_pkt->recycle_index = 0; //clear to not double count
                    }
                }
            }
        } else
        {
            if (p_file &&
                (fwrite((uint8_t *)p_out_pkt->p_data + meta_size,
                        p_out_pkt->data_len - meta_size, 1, p_file) != 1))
            {
                ni_log(NI_LOG_ERROR, "Error: writing data %u bytes error!\n",
                        p_out_pkt->data_len - meta_size);
                ni_log(NI_LOG_ERROR, "Error: ferror rc = %d\n", ferror(p_file));
            }
        }

        *total_bytes_received += rx_size - meta_size;

        if (0 == p_enc_ctx->pkt_num)
        {
            p_enc_ctx->pkt_num = 1;
            ni_log(NI_LOG_DEBUG, "got encoded stream header, keep reading ..\n");
            goto receive_data;
        }
        (*nb_pkts_received)++;

        ni_log(NI_LOG_DEBUG, "Got:   Packets= %u\n", *nb_pkts_received);
    } else if (rx_size != 0)
    {
        ni_log(NI_LOG_ERROR, "Error: received %d bytes, <= metadata size %d!\n",
                rx_size, meta_size);
        return NI_TEST_RETCODE_FAILURE;
    } else if (!end_flag && p_api_param->low_delay_mode)
    {
        ni_log(NI_LOG_DEBUG, "low delay mode and NO pkt, keep reading ..\n");
        ni_usleep(200);
        goto receive_data;
    } else
    {
        if (end_flag)
        {
            if (SESSION_RUN_STATE_SEQ_CHANGE_DRAINING ==
                p_enc_ctx->session_run_state)
            {
              // after sequence change completes, reset codec state
              ni_log(NI_LOG_INFO, "encoder_receive_data: sequence "
                     "change completed, return SEQ_CHANGE_DONE and will reopen "
                     "or reconfig codec!\n");

              rc = encoder_reinit_session(p_enc_ctx, p_in_data, p_out_data);
              ni_log(NI_LOG_TRACE, "encoder_receive_data: encoder_reinit_session ret %d\n", rc);
              if (rc == NI_RETCODE_SUCCESS)
              {
                return NI_TEST_RETCODE_SEQ_CHANGE_DONE;
              }
              else
              {
                return NI_TEST_RETCODE_FAILURE;
              }
            }

            if (p_enc_ctx->codec_format == NI_CODEC_FORMAT_AV1)
            {
                rc = write_av1_ivf_trailer(p_out_pkt, p_enc_ctx->meta_size, p_file);
                ni_packet_buffer_free_av1(p_out_pkt);
                if (rc < 0)
                {
                    return NI_TEST_RETCODE_FAILURE;
                }
            }
        }
    }

    if (print_time)
    {
        uint32_t time_diff = (uint32_t)(current_time.tv_sec - start_time.tv_sec);
        if (time_diff == 0)
        {
            time_diff = 1;
        }
        ni_log(NI_LOG_ERROR, "[R] Got:%d   Packets= %u fps=%f  Total bytes %llu\n", rx_size,
               *nb_pkts_received, (float)p_enc_ctx->frame_num / (float)time_diff,
               *total_bytes_received);
    }

    if (end_flag)
    {
        ni_log(NI_LOG_INFO, "Encoder Receiving done.\n");
        return NI_TEST_RETCODE_END_OF_STREAM;
    } else if (0 == rx_size)
    {
        return NI_TEST_RETCODE_EAGAIN;
    }
    ni_log(NI_LOG_DEBUG, "encoder_receive_data: success\n");

    return NI_TEST_RETCODE_SUCCESS;
}

/*!*****************************************************************************
 *  \brief  Set up hard coded demo ROI map
 *
 *  \param  
 *
 *  \return none
 ******************************************************************************/
void set_demo_roi_map(ni_session_context_t *p_enc_ctx)
{
    ni_xcoder_params_t *p_param =
        (ni_xcoder_params_t *)(p_enc_ctx->p_session_config);
    uint32_t i, j, sumQp = 0;
    uint32_t mbWidth, mbHeight, numMbs;
    // mode 1: Set QP for center 1/3 of picture to highest - lowest quality
    // the rest to lowest - highest quality;
    // mode non-1: reverse of mode 1
    int importanceLevelCentre = p_param->roi_demo_mode == 1 ? 40 : 10;
    int importanceLevelRest = p_param->roi_demo_mode == 1 ? 10 : 40;
    int32_t width, height;

    if (!p_enc_ctx->roi_map)
    {
        p_enc_ctx->roi_map =
            (ni_enc_quad_roi_custom_map *)calloc(1, p_enc_ctx->roi_len);
    }
    if (!p_enc_ctx->roi_map)
    {
        return;
    }
    uint32_t roiMapBlockUnitSize = 64;   // HEVC
    uint32_t max_cu_size = 64;           // HEVC
    if (NI_CODEC_FORMAT_H264 == p_enc_ctx->codec_format)
    {
        max_cu_size = 16;
        roiMapBlockUnitSize = 16;
    }

    width = p_param->source_width;
    height = p_param->source_height;
    // AV1 non-8x8-aligned resolution is implicitly cropped due to Quadra HW limitation
    if (NI_CODEC_FORMAT_AV1 == p_enc_ctx->codec_format)
    {
      width = (width / 8) * 8;
      height = (height / 8) * 8;
    }
    
    mbWidth =
        ((width + max_cu_size - 1) & (~(max_cu_size - 1))) /
        roiMapBlockUnitSize;
    mbHeight =
        ((height + max_cu_size - 1) & (~(max_cu_size - 1))) /
        roiMapBlockUnitSize;
    numMbs = mbWidth * mbHeight;

    // copy roi MBs QPs into custom map
    bool bIsCenter;
    // number of qp info (8x8) per mb or ctb
    uint32_t entryPerMb = (roiMapBlockUnitSize / 8) * (roiMapBlockUnitSize / 8);

    for (i = 0; i < numMbs; i++)
    {
        if ((i % mbWidth > mbWidth / 3) && (i % mbWidth < mbWidth * 2 / 3))
            bIsCenter = 1;
        else
            bIsCenter = 0;

        for (j = 0; j < entryPerMb; j++)
        {
            /*
              g_quad_roi_map[i*4+j].field.skip_flag = 0; // don't force
              skip mode g_quad_roi_map[i*4+j].field.roiAbsQp_flag = 1; //
              absolute QP g_quad_roi_map[i*4+j].field.qp_info = bIsCenter
              ? importanceLevelCentre : importanceLevelRest;
            */
            p_enc_ctx->roi_map[i * entryPerMb + j].field.ipcm_flag =
                0;   // don't force skip mode
            p_enc_ctx->roi_map[i * entryPerMb + j].field.roiAbsQp_flag =
                1;   // absolute QP
            p_enc_ctx->roi_map[i * entryPerMb + j].field.qp_info =
                bIsCenter ? importanceLevelCentre : importanceLevelRest;
        }
        sumQp += p_enc_ctx->roi_map[i * entryPerMb].field.qp_info;
    }
    p_enc_ctx->roi_avg_qp =
        // NOLINTNEXTLINE(clang-analyzer-core.DivideZero)
        (sumQp + (numMbs >> 1)) / numMbs;   // round off
}

/*!*****************************************************************************
 *  \brief  Encoder session open
 *
 *  \param  
 *
 *  \return 0 if successful, < 0 otherwise
 ******************************************************************************/
int encoder_open_session(ni_session_context_t *p_enc_ctx, int dst_codec_format,
                         int iXcoderGUID, ni_xcoder_params_t *p_enc_params,
                         int width, int height,
                         ni_color_primaries_t color_primaries,
                         ni_color_transfer_characteristic_t color_trc,
                         ni_color_space_t color_space, int video_full_range_flag,
                         int sar_num, int sar_den, ni_pix_fmt_t pix_fmt,
                         ni_rate_emu_t *p_rate_emu,
                         bool check_zerocopy)
{
    int ret = 0;
    bool isrgba = false;

    // p_rate_emu is NULL for sequence change - do not reset rate emu start time upon sequence change
    if (p_rate_emu)
    {
        p_rate_emu->rate_emu_start = ni_gettime_ns();
        p_rate_emu->rate_emu_input_frames = 0;
    }

    if (video_full_range_flag < 0)
    {
        ni_log(NI_LOG_ERROR, "ERROR %s: The video full range flag is %d should "
               "be indicated excplicitly as 0 or 1!\n", __func__);
        return -1;
    }

    p_enc_ctx->p_session_config = p_enc_params;
    p_enc_ctx->session_id = NI_INVALID_SESSION_ID;
    p_enc_ctx->codec_format = dst_codec_format;

    // assign the card GUID in the encoder context and let session open
    // take care of the rest
    p_enc_ctx->device_handle = NI_INVALID_DEVICE_HANDLE;
    p_enc_ctx->blk_io_handle = NI_INVALID_DEVICE_HANDLE;
    p_enc_ctx->hw_id = iXcoderGUID;

    // default: little endian
    p_enc_ctx->src_endian = NI_FRAME_LITTLE_ENDIAN;

    switch (pix_fmt)
    {
        case NI_PIX_FMT_YUV420P:
        case NI_PIX_FMT_NV12:
            p_enc_ctx->src_bit_depth = 8;
            p_enc_ctx->bit_depth_factor = 1;
            break;
        case NI_PIX_FMT_YUV420P10LE:
        case NI_PIX_FMT_P010LE:
            p_enc_ctx->src_bit_depth = 10;
            p_enc_ctx->bit_depth_factor = 2;
            break;
        case NI_PIX_FMT_ABGR:
        case NI_PIX_FMT_ARGB:
        case NI_PIX_FMT_RGBA:
        case NI_PIX_FMT_BGRA:
            p_enc_ctx->src_bit_depth = 8;
            p_enc_ctx->bit_depth_factor = 4;
            isrgba = true;
            break;
        default:
            p_enc_ctx->src_bit_depth = 8;
            p_enc_ctx->bit_depth_factor = 1;
            pix_fmt = NI_PIX_FMT_YUV420P;
            break;
            //ni_log(NI_LOG_ERROR, "%s: Invalid pixel format %s\n", __func__,
            //        ni_pixel_format_name(pix_fmt));
            //return NI_RETCODE_INVALID_PARAM;
    }

    // original resolution this stream started with, this is used by encoder sequence change
    p_enc_ctx->ori_width = width;
    p_enc_ctx->ori_height = height;
    p_enc_ctx->ori_bit_depth_factor = p_enc_ctx->bit_depth_factor;
    p_enc_ctx->ori_pix_fmt = pix_fmt;
    p_enc_ctx->pixel_format = pix_fmt;

    int linesize_aligned = width;
    if (!isrgba)
    {
        if (linesize_aligned < NI_MIN_WIDTH)
        {
            p_enc_params->cfg_enc_params.conf_win_right +=
                (NI_MIN_WIDTH - width) / 2 * 2;
            linesize_aligned = NI_MIN_WIDTH;
        } else
        {
            linesize_aligned = ((width + 1) / 2) * 2;
            p_enc_params->cfg_enc_params.conf_win_right +=
                (linesize_aligned - width) / 2 * 2;
        }
    }
    p_enc_params->source_width = linesize_aligned;

    int height_aligned = height;
    if (!isrgba)
    {
        if (height_aligned < NI_MIN_HEIGHT)
        {
            p_enc_params->cfg_enc_params.conf_win_bottom +=
                (NI_MIN_HEIGHT - height) / 2 * 2;
            height_aligned = NI_MIN_HEIGHT;
        } else
        {
            height_aligned = ((height + 1) / 2) * 2;
            p_enc_params->cfg_enc_params.conf_win_bottom +=
                (height_aligned - height) / 2 * 2;
        }
    }
    p_enc_params->source_height = height_aligned;

    // VUI setting including color setting is done by specifying them in the
    // encoder config
    p_enc_params->cfg_enc_params.colorDescPresent = 0;

    if ((color_primaries != NI_COL_PRI_UNSPECIFIED) ||
        (color_space != NI_COL_SPC_UNSPECIFIED) ||
        (color_trc != NI_COL_TRC_UNSPECIFIED))
    {
        p_enc_params->cfg_enc_params.colorDescPresent = 1;
    }
    p_enc_params->cfg_enc_params.colorPrimaries = color_primaries;
    p_enc_params->cfg_enc_params.colorTrc = color_trc;
    p_enc_params->cfg_enc_params.colorSpace = color_space;
    p_enc_params->cfg_enc_params.videoFullRange = video_full_range_flag;
    p_enc_params->cfg_enc_params.aspectRatioWidth = sar_num;
    p_enc_params->cfg_enc_params.aspectRatioHeight = sar_den;

    // default planar encoder input data
    p_enc_params->cfg_enc_params.planar = get_pixel_planar(pix_fmt);

    p_enc_params->video_full_range_flag = video_full_range_flag;

    if (check_zerocopy)
    {
        
        // config linesize for zero copy (if input resolution is zero copy compatible)
        int src_stride[NI_MAX_NUM_DATA_POINTERS];     

        // NOTE - FFmpeg / Gstreamer users should use linesize array in frame structure instead of src_stride in the following sample code
        src_stride[0] = width * p_enc_ctx->bit_depth_factor;

        if (isrgba)
            src_stride[1] = src_stride[2] = 0;
        else
        {
            bool isnv12frame = (p_enc_params->cfg_enc_params.planar == NI_PIXEL_PLANAR_FORMAT_SEMIPLANAR) ? true : false;
            src_stride[1] = isnv12frame ? src_stride[0] : src_stride[0] / 2;
            src_stride[2] = isnv12frame ? 0 : src_stride[0] / 2;
        }
        
        ni_encoder_frame_zerocopy_check(p_enc_ctx,
            p_enc_params, width, height,
            (const int *)src_stride, true);
    }

    ret = ni_device_session_open(p_enc_ctx, NI_DEVICE_TYPE_ENCODER);
    if (ret != NI_RETCODE_SUCCESS)
    {
        ni_log(NI_LOG_ERROR, "Error: %s failure!\n", __func__);
    } else
    {
        ni_log(NI_LOG_INFO, "Encoder device %d session open successful.\n", iXcoderGUID);
    }

    // set up ROI QP map for ROI demo modes if enabled
    if (p_enc_params->cfg_enc_params.roi_enable &&
        (1 == p_enc_params->roi_demo_mode || 2 == p_enc_params->roi_demo_mode))
    {
        set_demo_roi_map(p_enc_ctx);
    }

    return ret;
}

/*!*****************************************************************************
 *  \brief  encoder session close
 *
 *  \param  
 *
 *  \return 0 if successful, < 0 otherwise
 ******************************************************************************/
int encoder_close_session(ni_session_context_t *p_enc_ctx,
                         ni_session_data_io_t *p_in_data,
                         ni_session_data_io_t *p_out_data)
{
    int ret = 0;
    ni_device_session_close(p_enc_ctx, 1, NI_DEVICE_TYPE_ENCODER);
    
    ni_log(NI_LOG_DEBUG, "encoder_close_session - close encoder blk_io_handle %d device_handle %d\n", p_enc_ctx->blk_io_handle, p_enc_ctx->device_handle);
#ifdef _WIN32
    ni_device_close(p_enc_ctx->device_handle);
#elif __linux__
    ni_device_close(p_enc_ctx->device_handle);
    ni_device_close(p_enc_ctx->blk_io_handle);
#endif

    if (p_enc_ctx->codec_format == NI_CODEC_FORMAT_AV1 &&
        p_out_data->data.packet.av1_buffer_index)
    {
        ni_packet_buffer_free_av1(&(p_out_data->data.packet));
    }
    ni_frame_buffer_free(&(p_in_data->data.frame));
    ni_packet_buffer_free(&(p_out_data->data.packet));
    return ret;    
}

/*!*****************************************************************************
 *  \brief  encoder session init
 *
 *  \param  
 *
 *  \return 0 if successful, < 0 otherwise
 ******************************************************************************/
int encoder_init_session(ni_session_context_t *p_enc_ctx,
                         ni_session_data_io_t *p_in_data,
                         ni_session_data_io_t *p_out_data,
                         int arg_width, int arg_height,
                         ni_pix_fmt_t pix_fmt)
{
    int ret = 0;
    ni_xcoder_params_t *p_api_param = (ni_xcoder_params_t *)p_enc_ctx->p_session_config;

    ni_log(NI_LOG_DEBUG, "XCoder encode sequence change (re-open): session_run_state %d\n", p_enc_ctx->session_run_state);
    
      // for encode from YUV, use all the parameters specified by user
    ret = encoder_open_session(p_enc_ctx, p_enc_ctx->codec_format,
                               p_enc_ctx->hw_id, p_api_param, arg_width,
                               arg_height, p_api_param->color_primaries,
                               p_api_param->color_transfer_characteristic,
                               p_api_param->color_space,
                               p_api_param->video_full_range_flag,
                               p_api_param->sar_num, p_api_param->sar_denom,
                               pix_fmt,
                               NULL, true);
    if (NI_RETCODE_SUCCESS != ret)
    {
      ni_log(NI_LOG_ERROR, "Failed to Re-open Encoder Session upon Sequence Change (status = %d)\n", ret);
      return ret;
    }
    
    p_out_data->data.packet.end_of_stream = 0;
    p_in_data->data.frame.start_of_stream = 1;
    return ret;
}

int encoder_sequence_change(ni_session_context_t *p_enc_ctx,
                         ni_session_data_io_t *p_in_data,    
                         ni_session_data_io_t *p_out_data,
                         int width, int height, ni_pix_fmt_t pix_fmt)
{
  ni_retcode_t ret = 0;
  int bit_depth;
  int bit_depth_factor;

  ni_log(NI_LOG_DEBUG, "XCoder encode sequence change (reconfig): session_run_state %d\n", p_enc_ctx->session_run_state);

  switch (pix_fmt)
  {
    case NI_PIX_FMT_YUV420P:
    case NI_PIX_FMT_NV12:
      bit_depth = 8;
      bit_depth_factor = 1;
      break;
    case NI_PIX_FMT_YUV420P10LE:
    case NI_PIX_FMT_P010LE:
      bit_depth = 10;
      bit_depth_factor = 2;
      break;
    default:
      bit_depth = 8;
      bit_depth_factor = 1;
      break;
  }

  ret = ni_device_session_sequence_change(p_enc_ctx, width, height, bit_depth_factor, NI_DEVICE_TYPE_ENCODER);
  if (NI_RETCODE_SUCCESS != ret)
  {
    ni_log(NI_LOG_ERROR, "Failed to send Sequence Change to Encoder Session (status = %d)\n", ret);
    return ret;
  }

  // update session context
  p_enc_ctx->bit_depth_factor = bit_depth_factor;
  p_enc_ctx->src_bit_depth = bit_depth;
  // xcoder demo only support little endian (for 10-bit pixel format)
  p_enc_ctx->src_endian = NI_FRAME_LITTLE_ENDIAN;
  p_enc_ctx->ready_to_close = 0;
  p_enc_ctx->frame_num = 0; // need to reset frame_num because pkt_num is set to 1 when header received after sequnce change, and low delay mode compares frame_num and pkt_num
  p_enc_ctx->pkt_num = 0; // also need to reset pkt_num because before header received, pkt_num > frame_num will also cause low delay mode stuck
  p_enc_ctx->pixel_format = p_in_data->data.frame.pixel_format;
  p_out_data->data.packet.end_of_stream = 0;
  p_in_data->data.frame.start_of_stream = 1;
  return ret;
}

/*!*****************************************************************************
 *  \brief  decoder session open
 *
 *  \param  
 *
 *  \return 0 if successful, < 0 otherwise
 ******************************************************************************/
int decoder_open_session(ni_session_context_t *p_dec_ctx, int iXcoderGUID,
                         ni_xcoder_params_t *p_dec_params)
{
    int ret = 0;

    p_dec_ctx->p_session_config = p_dec_params;
    p_dec_ctx->session_id = NI_INVALID_SESSION_ID;

    // assign the card GUID in the encoder context and let session open
    // take care of the rest
    p_dec_ctx->device_handle = NI_INVALID_DEVICE_HANDLE;
    p_dec_ctx->blk_io_handle = NI_INVALID_DEVICE_HANDLE;
    p_dec_ctx->hw_id = iXcoderGUID;

    if (p_dec_params->dec_input_params.hwframes)
    {
        p_dec_ctx->hw_action = NI_CODEC_HW_ENABLE;
    } else
    {
        p_dec_ctx->hw_action = NI_CODEC_HW_NONE;
    }

    ret = ni_device_session_open(p_dec_ctx, NI_DEVICE_TYPE_DECODER);

    if (ret != NI_RETCODE_SUCCESS)
    {
        ni_log(NI_LOG_ERROR, "Error: ni_decoder_session_open() failure!\n");
        return -1;
    } else
    {
        ni_log(NI_LOG_INFO, "Decoder device %d session open successful.\n", iXcoderGUID);
        return 0;
    }
}

/*!*****************************************************************************
 *  \brief  Encoder session open
 *
 *  \param  
 *
 *  \return 0 if successful, < 0 otherwise
 ******************************************************************************/
int uploader_open_session(ni_session_context_t *p_upl_ctx, int iXcoderGUID,
                          int width, int height, ni_pix_fmt_t pix_fmt,
                          int is_p2p, int pool_size)
{
    int ret = 0;
    p_upl_ctx->session_id = NI_INVALID_SESSION_ID;

    // assign the card GUID in the encoder context and let session open
    // take care of the rest
    p_upl_ctx->device_handle = NI_INVALID_DEVICE_HANDLE;
    p_upl_ctx->blk_io_handle = NI_INVALID_DEVICE_HANDLE;
    p_upl_ctx->hw_id = iXcoderGUID;

    // Set the input frame format of the upload session
    ret = ni_uploader_set_frame_format(p_upl_ctx, width, height, pix_fmt, is_p2p);
    if(ret != NI_RETCODE_SUCCESS)
    {
        ni_log(NI_LOG_ERROR, "Error: %s failure. Failed to set uploader format!\n", __func__);
        return ret;
    }

    ret = ni_device_session_open(p_upl_ctx, NI_DEVICE_TYPE_UPLOAD);
    if (ret != NI_RETCODE_SUCCESS)
    {
        ni_log(NI_LOG_ERROR, "Error: %s failure!\n", __func__);
        return ret;
    } else
    {
        ni_log(NI_LOG_INFO, "Uploader device %d session open successful.\n", iXcoderGUID);
    }

    ret = ni_device_session_init_framepool(p_upl_ctx, pool_size, 0);
    if (ret < 0)
    {
        ni_log(NI_LOG_ERROR, "Error: %s failure!\n", __func__);
    } else
    {
        ni_log(NI_LOG_INFO, "Uploader device %d configured successful.\n", iXcoderGUID);
    }

    return ret;
}

void print_usage(void)
{
    printf(
        "Video decoder/encoder/transcoder application directly using Netint "
        "Libxcoder release v%s\n"
        "Usage: xcoder [options]\n"
        "\n"
        "options:\n"
        "-h | --help                    Show help.\n"
        "-v | --version                 Print version info.\n"
        "-t | --multi-thread            Enable multi-threaded sending and "
        "receiving"
        " in all modes.\n"
        "-l | --loglevel                Set loglevel of libxcoder API.\n"
        "                               [none, fatal, error, info, debug, "
        "trace]\n"
        "                               (Default: info)\n"
        "-c | --card                    Set card index to use.\n"
        "                               See `ni_rsrc_mon` for cards on "
        "system.\n"
        "                               (Default: 0)\n"
        "-i | --input                   Input file path.\n"
        "-r | --repeat                  (Positive integer) to Repeat input X \n"
        "                               times for performance test.(Default: "
        "1)\n"
        "-e | --readframerate           Read input at specified frame rate.\n"
        "                               Currently only available for ENCODE "
        "modes (y2a, y2h, y2x, y2o)\n"
        "-p | --pix_fmt                 Indicate the pixel format of the "
        "input.\n"
        "                               (Default: yuv420p)\n"
        "                               Read raw video and do encode: [yuv420p, yuv420p10le, nv12, p010le, "
        "rgba, gbra, argb, abgr, bgr0, yuv444p]\n"
        "                               Upload and do encode: [yuv420p, yuv420p10le, nv12, p010le, "
        "rgba, gbra, argb, abgr, bgr0]\n"
        "-s | --size                    Resolution of input file in format "
        "WIDTHxHEIGHT.\n"
        "                               (eg. '1920x1080')\n"
        "-m | --mode                    Input to output codec processing mode "
        "in "
        "format:\n"
        "                               InType2OutType. [a2y, h2y, y2a, u2a, "
        "y2h, "
        "u2h, a2a, "
        "a2h, h2a, h2h, y2x, y2o, u2x, v2y, v2a, v2h, v2x, h2x, a2x, a2r]\n"
        "                               Type notation: y=YUV420P a=AVC, "
        "h=HEVC, "
        "v=VP9, x=AV1 IVF, o=AV1 OBU, "
        "u=hwupload yuv, "
        "r=scaler p2p demo\n"
        "-b | --bitdepth                Input and output bit depth. [8, 10]\n"
        "                               (Default: 8)\n"
        "-u | --user_data_sei_passthru  Enable user data unregistered SEI "
        "passthrough.\n"
        "-x | --xcoder-params           Encoding params. See \"Encoding "
        "Parameters\" chapter in\n"
        "                               "
        "QuadraIntegration&ProgrammingGuide*.pdf for help.\n"
        "                               "
        "Can be specified multiple (max MAX_OUTPUT_FILES) times \n"
        "                               "
        "for HWUpload + Encoding for non-AV1 codec in single \n"
        "                               "
        "thread mode. Must match the number of -o specified.\n"
        "                               (Default: \"\")\n"
        "-g | --xcoder-gop              Custom GOP for encoding. See \"Custom "
        " Gop Structure\" chapter in\n"
        "                               "
        "QuadraIntegration&ProgrammingGuide*.pdf for help.\n"
        "                               (Default: \"\")\n"
        "                               "
        "gopPresetIdx must be set to 0 to be in effect.\n"
        "-d | --decoder-params          Decoding params. See \"Decoding "
        "Parameters\" chapter in\n"
        "                               "
        "QuadraIntegration&ProgrammingGuide*.pdf for help.\n"
        "                               (Default: \"\")\n"
        "-o | --output                  Output file path.\n"
        "                               "
        "Can be specified multiple (max MAX_OUTPUT_FILES) times \n"
        "                               "
        "for HWUpload + Encoding for non-AV1 codec in single \n"
        "                               "
        "thread mode. Must match the number of -x specified.\n"
        "-f | --vf                      Scaling params. Example: "
        "ni_width=1280:ni_height=720:format=0:p2p=1.\n",
        NI_XCODER_REVISION);
}

void *decoder_send_thread(void *args)
{
    dec_send_param_t *p_dec_send_param = args;
    ni_session_context_t *p_dec_ctx = p_dec_send_param->p_dec_ctx;
    int retval = 0;

    ni_log(NI_LOG_INFO, "decoder_send_thread start: decoder_low_delay %d\n",
           p_dec_ctx->decoder_low_delay);
    while (!g_end_of_all_threads)
    {
        retval = decoder_send_data(
            p_dec_ctx, p_dec_send_param->p_in_pkt,
            p_dec_send_param->input_video_width,
            p_dec_send_param->input_video_height, p_dec_send_param->pkt_size,
            p_dec_send_param->p_total_bytes_sent,
            p_dec_send_param->print_time, p_dec_send_param->p_xcoder_state,
            p_dec_send_param->p_stream_info);
        if (retval < 0)   // Error
        {
            ni_log(NI_LOG_ERROR, "Error: decoder_send_thread break!\n");
            break;
        } else if (p_dec_send_param->p_xcoder_state->dec_eos_sent)   //eos
        {
            ni_log(NI_LOG_INFO, "decoder_send_thread reach eos\n");
            break;
        }
    }

    // Broadcast all codec threads to quit on exception such as NVMe IO.
    if (retval < 0)
    {
        g_end_of_all_threads = 1;
    }

    ni_log(NI_LOG_TRACE, "decoder_send_thread exit\n");
    return (void *)(long)retval;
}

void *decoder_receive_thread(void *args)
{
    dec_recv_param_t *p_dec_recv_param = args;
    ni_test_frame_list_t *frame_list = p_dec_recv_param->frame_list;
    ni_session_data_io_t *p_out_frame = NULL;
    ni_frame_t *p_ni_frame = NULL;
    int retval = 0;
    int print_time;
    struct timeval previous_time_l = previous_time;
    int rx_size = 0;

    ni_log(NI_LOG_INFO, "decoder_receive_thread start\n");

    for (;;)
    {
        if (p_dec_recv_param->mode == XCODER_APP_DECODE)
        {
            (void)ni_gettimeofday(&current_time, NULL);
            print_time = need_time_print(&current_time, &previous_time);
            p_ni_frame = &p_dec_recv_param->p_out_frame->data.frame;
            retval = decoder_receive_data(
                p_dec_recv_param->p_dec_ctx, p_dec_recv_param->p_out_frame,
                p_dec_recv_param->output_video_width,
                p_dec_recv_param->output_video_height, p_dec_recv_param->p_file,
                p_dec_recv_param->p_total_bytes_received, print_time,
                1,   // save to file
                p_dec_recv_param->p_xcoder_state,
                &rx_size);

            if (p_dec_recv_param->p_dec_ctx->hw_action == NI_CODEC_HW_ENABLE)
            {
                niFrameSurface1_t *p_hwframe = (niFrameSurface1_t *)p_dec_recv_param->p_out_frame->data.frame.p_data[3];
                ni_log(NI_LOG_DEBUG, "decoder_receive_thread HW decode-only. recycle HW frame idx %u\n", p_hwframe->ui16FrameIdx);
                ni_hwframe_buffer_recycle2(p_hwframe);
                ni_frame_buffer_free(p_ni_frame);
            } else
            {
                ni_decoder_frame_buffer_free(p_ni_frame);
            }

            if (retval < 0)   // Error
            {
                ni_log(
                    NI_LOG_ERROR,
                    "Error: decoder_receive_thread break in decode mode!\n");
                break;
            } else if (p_ni_frame->end_of_stream)   //eos
            {
                ni_log(NI_LOG_INFO, "decoder_receive_thread reach eos\n");
                retval = 0;
                break;
            } else if (retval == NI_TEST_RETCODE_EAGAIN)
            {
                ni_usleep(100);
            }
            if (print_time)
            {
                previous_time = current_time;
            }
        } else
        {
            while (frame_list_is_full(frame_list) && !g_end_of_all_threads)
            {
                ni_usleep(100);
            }

            if (g_end_of_all_threads)
            {
                break;
            }

            print_time = need_time_print(&current_time, &previous_time_l);
            p_out_frame = &frame_list->frames[frame_list->tail];
            p_ni_frame = &p_out_frame->data.frame;
            retval = decoder_receive_data(
                p_dec_recv_param->p_dec_ctx, p_out_frame,
                p_dec_recv_param->output_video_width,
                p_dec_recv_param->output_video_height, p_dec_recv_param->p_file,
                p_dec_recv_param->p_total_bytes_received, print_time,
                0,   // do not save to file
                p_dec_recv_param->p_xcoder_state,
                &rx_size);
            if (retval < 0)   // Error
            {
                if (!p_dec_recv_param->p_dec_ctx->hw_action)
                {
                    ni_decoder_frame_buffer_free(p_ni_frame);
                } else
                {
                    ni_frame_buffer_free(p_ni_frame);
                }
                ni_log(
                    NI_LOG_ERROR,
                    "Error: decoder_receive_thread break in transcode mode!\n");
                break;
            } else if (p_ni_frame->end_of_stream)
            {
                frame_list_enqueue(frame_list);
                ni_log(NI_LOG_INFO, "decoder_receive_thread reach eos\n");
                retval = 0;
                break;
            } else if (retval == NI_TEST_RETCODE_EAGAIN)
            {
                if (!p_dec_recv_param->p_dec_ctx->hw_action)
                {
                    ni_decoder_frame_buffer_free(p_ni_frame);
                } else
                {
                    ni_frame_buffer_free(p_ni_frame);
                }
                ni_usleep(100);
            } else
            {
                if (p_dec_recv_param->p_dec_ctx->hw_action)
                {
                    uint16_t current_hwframe_index =
                        ((niFrameSurface1_t *)((uint8_t *)
                                                   p_ni_frame->p_data[3]))
                            ->ui16FrameIdx;
                    ni_log(NI_LOG_DEBUG, "decoder recv:%d, tail:%d\n",
                                   current_hwframe_index, frame_list->tail);
                }
                frame_list_enqueue(frame_list);
            }
            if (print_time)
            {
                // current_time will be updated in transcode mode
                previous_time_l = current_time;
            }
        }
    }

    // Broadcast all codec threads to quit on exception such as NVMe IO.
    if (retval < 0)
    {
        g_end_of_all_threads = 1;
    }

    ni_log(NI_LOG_TRACE, "decoder_receive_thread exit\n");
    return (void *)(long)retval;
}

void *encoder_send_thread(void *args)
{
    enc_send_param_t *p_enc_send_param = args;
    ni_session_context_t *p_enc_ctx_list = p_enc_send_param->p_enc_ctx;
    ni_test_frame_list_t *frame_list = p_enc_send_param->frame_list;
    ni_session_data_io_t *p_dec_frame = NULL;
    ni_session_data_io_t *p_upl_frame = NULL;
    ni_session_data_io_t sw_pix_frame[2];
    ni_frame_t *p_ni_frame = NULL;
    niFrameSurface1_t *p_surface;
    int i, ret = 0;
    int input_index = 0;
    int chunk_size;
    int eos = 0;

    ni_log(NI_LOG_INFO, "%s start\n", __func__);

    memset(sw_pix_frame, 0, 2 * sizeof(ni_session_data_io_t));

    for (;;)
    {
        if (p_enc_send_param->mode == XCODER_APP_ENCODE)
        {
            chunk_size = read_yuv_from_file(
                                p_enc_send_param->input_arg_pfs[input_index],
                                p_enc_send_param->yuv_buf,
                                p_enc_send_param->input_arg_width[input_index],
                                p_enc_send_param->input_arg_height[input_index],
                                p_enc_ctx_list[0].pixel_format,
                                p_enc_send_param->sw_pix_fmt, &eos,
                                p_enc_ctx_list[0].session_run_state);
            if (chunk_size < 0)
            {
                break;
            }

            // YUV444P conversion
            if (p_enc_send_param->sw_pix_fmt != NI_SW_PIX_FMT_NONE)
            {
                ret = convert_yuv_444p_to_420p(&sw_pix_frame[0],
                                eos ? NULL : p_enc_send_param->yuv_buf,
                                p_enc_send_param->input_arg_width[input_index],
                                p_enc_send_param->input_arg_height[input_index],
                                p_enc_send_param->sw_pix_fmt, 0,
                                p_enc_send_param->dec_codec_format);
                if (ret < 0)
                {
                    break;
                }
            }

            for (i = 0; i < p_enc_send_param->output_total; i++)
            {
                if (p_enc_send_param->sw_pix_fmt != NI_SW_PIX_FMT_NONE)
                {
                    ret = encoder_send_data3(&p_enc_ctx_list[i],
                                &sw_pix_frame[i],
                                p_enc_send_param->input_arg_width[input_index],
                                p_enc_send_param->input_arg_height[input_index],
                                &p_enc_send_param->p_xcoder_state[i], eos);
                } else
                {
                    ret = encoder_send_data(&p_enc_ctx_list[i],
                              p_enc_send_param->p_in_frame,
                              eos ? NULL : p_enc_send_param->yuv_buf,
                              p_enc_send_param->input_arg_width[input_index],
                              p_enc_send_param->input_arg_height[input_index],
                              p_enc_send_param->p_total_bytes_sent,
                              &p_enc_send_param->p_xcoder_state[i],
                              input_index == p_enc_send_param->input_total - 1,
                              &p_enc_send_param->p_rate_emu[i]);
                }

                if (ret < 0)   // Error
                {
                    ni_log(NI_LOG_ERROR,"Error: %s break in encode mode\n",__func__);
                    LRETURN;
                }
            }

            // All frames has been sent successfully. Just check the one of the
            // statuses.
            if (ret == NI_TEST_RETCODE_NEXT_INPUT) // next input (will trigger sequence change)
            {
                input_index++;
                get_total_file_size(p_enc_send_param->input_arg_pfs[input_index]);
            } else if (p_enc_ctx_list[0].session_run_state ==
                       SESSION_RUN_STATE_SEQ_CHANGE_DRAINING) // sequene change
            {
                ni_usleep(100);
            } else if (p_enc_send_param->p_xcoder_state[0].enc_eos_sent)   //eos
            {
                ni_log(NI_LOG_DEBUG, "%s reach eos in encode mode\n", __func__);
                break;
            }
        } else if (p_enc_send_param->mode == XCODER_APP_TRANSCODE)
        {
            while (frame_list_is_empty(frame_list) && !g_end_of_all_threads)
            {
                ni_usleep(100);
            }

            if (g_end_of_all_threads)
            {
                LRETURN;
            }

            p_dec_frame = &frame_list->frames[frame_list->head];
            p_ni_frame = &p_dec_frame->data.frame;

            for (i = 0; i < p_enc_send_param->output_total; i++)
            {
                ret = encoder_send_data2(&p_enc_ctx_list[i], p_dec_frame,
                                         p_enc_send_param->p_in_frame,
                                         p_enc_send_param->input_video_width,
                                         p_enc_send_param->input_video_height,
                                         p_enc_send_param->p_total_bytes_sent,
                                         p_enc_send_param->p_xcoder_state);
                if (ret < 0)   //Error
                {
                    if (p_enc_ctx_list[i].hw_action)
                    {
                        //pre close cleanup will clear it out
                        p_surface = (niFrameSurface1_t *)p_ni_frame->p_data[3];
                        ni_hw_frame_ref(p_surface);
                    } else
                    {
                        ni_decoder_frame_buffer_free(p_ni_frame);
                    }
                    frame_list_drain(frame_list);
                    ni_log(NI_LOG_ERROR, "Error: %s break in transcode mode!\n",
                            __func__);
                    LRETURN;
                }
            }

            // All frames has been sent successfully. Just check the one of the
            // statuses.
            if (p_enc_ctx_list[0].hw_action)
            {
                // skip copy the frame when eos received.
                if (!p_enc_send_param->p_xcoder_state[0].enc_eos_sent)
                {
                    //track in array with unique index, free when enc read
                    //finds this must be implemented in application space
                    //for complete tracking of hwframes. If encoder write
                    //had no buffer avail space the next time we update the
                    //tracker will be redundant.
                    p_surface = (niFrameSurface1_t *)p_ni_frame->p_data[3];
                    ni_hw_frame_ref(p_surface);
                }
                ni_frame_wipe_aux_data(p_ni_frame);   //reusebuff
            } else
            {
                ni_decoder_frame_buffer_free(p_ni_frame);
            }
            frame_list_drain(frame_list);

            if (p_enc_send_param->p_xcoder_state[0].enc_eos_sent)   // eos
            {
                ni_log(NI_LOG_INFO, "%s reach eos in transcode mode\n", __func__);
                break;
            }
        } else
        {
            while (frame_list_is_empty(frame_list) && !g_end_of_all_threads)
            {
                ni_usleep(100);
            }

            if (g_end_of_all_threads)
            {
                LRETURN;
            }

            p_upl_frame = &frame_list->frames[frame_list->head];
            eos = p_upl_frame->data.frame.end_of_stream;

            for (i = 0; i < p_enc_send_param->output_total; i++)
            {
                ret = encoder_send_data3(&p_enc_ctx_list[i], p_upl_frame,
                                         p_enc_send_param->input_video_width,
                                         p_enc_send_param->input_video_height,
                                         &p_enc_send_param->p_xcoder_state[i],
                                         eos);
                if (ret < 0)   //Error
                {
                    ni_log(NI_LOG_ERROR, "Error: %s break in upload mode\n",
                            __func__);
                    LRETURN;
                }
            }

            // All frames has been sent successfully. Just check the one of the
            // statuses.
            if (!p_enc_send_param->p_xcoder_state[0].enc_resend)
            {
                p_ni_frame = &p_upl_frame->data.frame;
                p_surface = (niFrameSurface1_t *)p_ni_frame->p_data[3];
                ni_hw_frame_ref(p_surface);
                ni_log(NI_LOG_DEBUG, "send frame index:%d, head:%d\n",
                       p_surface->ui16FrameIdx, frame_list->head);
                frame_list_drain(frame_list);
            }

            if (p_enc_send_param->p_xcoder_state[0].enc_eos_sent)
            {
                ni_log(NI_LOG_INFO, "%s reach eos in upload mode\n", __func__);
                break;
            }
        }
    }

END:
    // Broadcast all codec threads to quit on exception such as NVMe IO.
    if (ret < 0)
    {
        g_end_of_all_threads = 1;
    }

    ni_log(NI_LOG_TRACE, "%s exit\n", __func__);
    return (void *)(long)ret;
}

void *encoder_receive_thread(void *args)
{
    enc_recv_param_t *p_enc_recv_param = args;
    int i, ret = 0;
    int print_time;
    int end_of_all_streams = 0;

    ni_log(NI_LOG_INFO, "encoder_receive_thread start\n");

    while (!end_of_all_streams && ret >= 0 && !g_end_of_all_threads)
    {
        (void)ni_gettimeofday(&current_time, NULL);
        print_time = need_time_print(&current_time, &previous_time);

        ret = encoder_receive(p_enc_recv_param->p_enc_ctx,
                              p_enc_recv_param->p_buffered_frame,
                              p_enc_recv_param->p_out_packet,
                              p_enc_recv_param->output_video_width,
                              p_enc_recv_param->output_video_height,
                              p_enc_recv_param->p_number_of_packets,
                              p_enc_recv_param->output_total,
                              p_enc_recv_param->p_file,
                              p_enc_recv_param->p_total_bytes_received,
                              print_time, p_enc_recv_param->p_xcoder_state);
        for (i = 0; ret >= 0 && i < p_enc_recv_param->output_total; i++)
        {
            if (!p_enc_recv_param->p_xcoder_state[i].enc_eos_received)
            {
                ni_log(NI_LOG_DEBUG, "enc %d continues to read!\n", i);
                end_of_all_streams = 0;
                break;
            } else
            {
                ni_log(NI_LOG_DEBUG, "enc %d eos !\n", i);
                end_of_all_streams = 1;
            }
        }

        if (print_time)
        {
            previous_time = current_time;
        }
    }

    // Broadcast all codec threads to quit on exception such as NVMe IO.
    if (ret < 0)
    {
        g_end_of_all_threads = 1;
    }

    ni_log(NI_LOG_TRACE, "encoder_receive_thread exit\n");
    return (void *)(long)ret;
}

void *uploader_thread(void *args)
{
    uploader_param_t *p_upl_param = args;
    ni_session_context_t *p_upl_ctx = p_upl_param->p_upl_ctx;
    ni_test_frame_list_t *frame_list = p_upl_param->frame_list;
    ni_session_data_io_t *p_out_frame;
    niFrameSurface1_t *p_hwframe = NULL;
    int eos = 0;
    p_upl_ctx->async_mode = 1;   // async mode (multi-threaded mode)

    ni_log(NI_LOG_INFO, "uploader_thread start\n");
    for (;;)
    {
        while (uploader_frame_list_full(frame_list, p_upl_ctx->pixel_format) &&
             !g_end_of_all_threads)
        {
            ni_usleep(100);
        }

        if (g_end_of_all_threads)
        {
            break;
        }

        int tail = frame_list->tail;
        p_out_frame = &frame_list->frames[tail];
        p_hwframe = hwupload_frame(p_upl_ctx, p_upl_param->p_sca_ctx,
                                   p_upl_param->p_swin_frame, p_out_frame,
                                   p_upl_param->p_scale_frame,
                                   p_upl_ctx->pixel_format,
                                   p_upl_param->input_video_width,
                                   p_upl_param->input_video_height,
                                   p_upl_param->pfs, p_upl_param->yuv_buf,
                                   p_upl_param->p_total_bytes_sent, &eos);
        if (!p_hwframe)
        {
            if (p_upl_ctx->status == NI_RETCODE_NVME_SC_WRITE_BUFFER_FULL)
            {
                ni_usleep(100);
                continue;
            } else
            {
                ni_log(NI_LOG_ERROR, "Error: uploader_thread break!\n");
                // Broadcast all codec threads to quit on exception such as
                // NVMe IO.
                g_end_of_all_threads = 1;
                break;
            }
        } else if (p_out_frame->data.frame.end_of_stream)   // eos
        {
            frame_list_enqueue(frame_list);
            ni_log(NI_LOG_INFO, "uploader_thread reach eos\n");
            break;
        }

        ni_log(NI_LOG_DEBUG, "uploader:%d, tail:%d\n", p_hwframe->ui16FrameIdx,
               tail);

        if (!is_ni_enc_pix_fmt(p_upl_ctx->pixel_format))
        {
            ni_frame_buffer_free(&frame_list->frames[tail].data.frame);
            memcpy(&frame_list->frames[tail],
                   p_upl_param->p_scale_frame, sizeof(ni_session_data_io_t));
        }
        frame_list_enqueue(frame_list);
    }

    ni_log(NI_LOG_TRACE, "uploader_thread exit\n");
    return NULL;
}

static int ni_scaler_params_set_value(ni_filter_params_t *params,
                                      const char *name, const char *value)
{
    if (!params)
    {
        ni_log(NI_LOG_ERROR, "Error: Null pointer parameters passed\n");
        return -1;
    }

    if (!name)
    {
        ni_log(NI_LOG_ERROR, "Error: Null name pointer parameters passed\n");
        return -1;
    }

    if (!strcmp("scale_width", name))
    {
        params->scale_width = atoi(value);
    } else if (!strcmp("scale_height", name))
    {
        params->scale_height = atoi(value);
    } else if (!strcmp("p2p", name))
    {
        int is_p2p = atoi(value);
        params->p2p = is_p2p;
    } else if (!strcmp("format", name))
    {
        int format = atoi(value);
        if (format == 0)
        {
            params->format = GC620_I420;
        } else if (format == 1)
        {
            params->format = GC620_RGBA8888;
        } else if (format == 2)
        {
            params->format = GC620_RGB888_PLANAR;
        } else
        {
            ni_log(NI_LOG_ERROR, "Error: invallid scaler parameters\n");
            return -1;
        }
    } else
    {
        ni_log(NI_LOG_ERROR, "Error: invallid scaler parameters\n");
        return -1;
    }

    return 0;
}

static int retrieve_filter_params(char xcoderParams[],
                                  ni_filter_params_t *params)
{
    char key[64], value[64];
    char *curr = xcoderParams, *colon_pos;
    int ret = 0;

    while (*curr)
    {
        colon_pos = strchr(curr, ':');

        if (colon_pos)
        {
            *colon_pos = '\0';
        }

        if (strlen(curr) > sizeof(key) + sizeof(value) - 1 ||
            ni_param_get_key_value(curr, key, value))
        {
            ni_log(NI_LOG_ERROR,
                    "Error: scaler-params key/value not "
                    "retrieved: %s\n",
                    curr);
            ret = -1;
            break;
        }
        ret = ni_scaler_params_set_value(params, key, value);
        if (ret != 0)
        {
            ni_log(NI_LOG_ERROR, "Error: failed to parse the input scaler parameters\n");
            ret = -1;
            break;
        }

        if (colon_pos)
        {
            curr = colon_pos + 1;
        } else
        {
            curr += strlen(curr);
        }
    }
    return ret;
}

/*!*****************************************************************************
 *  \brief  Init scaler params here - both user setting params and fixed params. 
 *
 *  \param  
 *
 *  \return 0 if successful, < 0 otherwise
 ******************************************************************************/
void init_scaler_params(ni_scaler_input_params_t *p_scaler_params,
                        ni_scaler_opcode_t op, int in_rec_width,
                        int in_rec_height, int in_rec_x, int in_rec_y,
                        int out_rec_x, int out_rec_y)
{
    p_scaler_params->op = op;
    // input_format/width/height, output_format/width/height should be assigned by users for all ops
    if (op == NI_SCALER_OPCODE_CROP)
    {
        // fixed numbers
        p_scaler_params->out_rec_width = 0;
        p_scaler_params->out_rec_height = 0;
        p_scaler_params->out_rec_x = 0;
        p_scaler_params->out_rec_y = 0;
        p_scaler_params->rgba_color = 0;

        // params set by user
        p_scaler_params->in_rec_width = in_rec_width;
        p_scaler_params->in_rec_height = in_rec_height;
        p_scaler_params->in_rec_x = in_rec_x;
        p_scaler_params->in_rec_y = in_rec_y;
    } else if (op == NI_SCALER_OPCODE_SCALE)
    {
        // fixed params
        p_scaler_params->in_rec_width = 0;
        p_scaler_params->in_rec_height = 0;
        p_scaler_params->in_rec_x = 0;
        p_scaler_params->in_rec_y = 0;

        p_scaler_params->out_rec_width = 0;
        p_scaler_params->out_rec_height = 0;
        p_scaler_params->out_rec_x = 0;
        p_scaler_params->out_rec_y = 0;

        p_scaler_params->rgba_color = 0;
    } else if (op == NI_SCALER_OPCODE_PAD)
    {
        // fixed params
        p_scaler_params->in_rec_width = p_scaler_params->input_width;
        p_scaler_params->in_rec_height = p_scaler_params->input_height;
        p_scaler_params->in_rec_x = 0;
        p_scaler_params->in_rec_y = 0;

        p_scaler_params->out_rec_width = p_scaler_params->input_width;
        p_scaler_params->out_rec_height = p_scaler_params->input_height;

        /* 
            Scaler uses BGRA color, or ARGB in little-endian 
            ui32RgbaColor = (s->rgba_color[3] << 24) | (s->rgba_color[0] << 16) |
                            (s->rgba_color[1] << 8) | s->rgba_color[2];
            here p_scaler_params->rgba_color = ui32RgbaColor;
        */
        p_scaler_params->rgba_color =
            4278190080;   // now padding color is black

        // params set by user
        p_scaler_params->out_rec_x = out_rec_x;
        p_scaler_params->out_rec_y = out_rec_y;
    } else if (op == NI_SCALER_OPCODE_OVERLAY)
    {
        // fixed params
        // set the in_rec params to the w/h of overlay(the upper) frames
        p_scaler_params->in_rec_width = p_scaler_params->input_width;
        p_scaler_params->in_rec_height = p_scaler_params->input_height;

        // the output w/h is the main frame's w/h (main frame is the lower/background frame)
        p_scaler_params->out_rec_width = p_scaler_params->output_width;
        p_scaler_params->out_rec_height = p_scaler_params->output_height;
        p_scaler_params->out_rec_x = 0;
        p_scaler_params->out_rec_y = 0;
        p_scaler_params->rgba_color = 0;

        // params set by user
        p_scaler_params->in_rec_x = in_rec_x;
        p_scaler_params->in_rec_y = in_rec_x;
    }
}

/*!*****************************************************************************
 *  \brief  open scaler session
 *
 *  \param  
 *
 *  \return 0 if successful, < 0 otherwise
 ******************************************************************************/
static int scaler_session_open(ni_session_context_t *p_scaler_ctx,
                               int iXcoderGUID, ni_scaler_opcode_t op)
{
    int ret = 0;

    p_scaler_ctx->session_id = NI_INVALID_SESSION_ID;

    p_scaler_ctx->device_handle = NI_INVALID_DEVICE_HANDLE;
    p_scaler_ctx->blk_io_handle = NI_INVALID_DEVICE_HANDLE;
    p_scaler_ctx->hw_id = iXcoderGUID;
    p_scaler_ctx->device_type = NI_DEVICE_TYPE_SCALER;
    p_scaler_ctx->scaler_operation = op;
    p_scaler_ctx->keep_alive_timeout = NI_DEFAULT_KEEP_ALIVE_TIMEOUT;

    ret = ni_device_session_open(p_scaler_ctx, NI_DEVICE_TYPE_SCALER);

    if (ret != NI_RETCODE_SUCCESS)
    {
        ni_log(NI_LOG_ERROR, "Error: ni_scaler_session_open() failure!\n");
        return -1;
    } else
    {
#ifdef _WIN32
        ni_log(NI_LOG_INFO, "Scaler session open: device_handle %p, session_id %u.\n",
               p_scaler_ctx->device_handle, p_scaler_ctx->session_id);
#else
        ni_log(NI_LOG_INFO, "Scaler session open: device_handle %d, session_id %u.\n",
               p_scaler_ctx->device_handle, p_scaler_ctx->session_id);
#endif
        return 0;
    }
}

/*!*****************************************************************************
 *  \brief  Launch scaler operation and get the result hw frame
 *
 *  \param  
 *
 *  \return 0 if successful, < 0 otherwise
 ******************************************************************************/
static int launch_scaler_operation(ni_session_context_t *p_ctx, int iXcoderGUID,
                                   ni_frame_t *p_frame_in_up,
                                   ni_frame_t *p_frame_in_bg,
                                   ni_session_data_io_t *p_data_out,
                                   ni_scaler_input_params_t scaler_params)
{
    int ret = 0;
    niFrameSurface1_t *frame_surface_up;
    niFrameSurface1_t *frame_surface_bg;
    niFrameSurface1_t *frame_surface_output;

    if (p_ctx->session_id == NI_INVALID_SESSION_ID)
    {
        // Open scaler session
        if (0 != scaler_session_open(p_ctx, iXcoderGUID, scaler_params.op))
        {
            ni_log(NI_LOG_ERROR, "Error: scaler open session error\n");
            return -1;
        }

        // init scaler hwframe pool
        if (0 != ni_scaler_frame_pool_alloc(p_ctx, scaler_params))
        {
            ni_log(NI_LOG_ERROR, "Error: init filter hwframe pool\n");
            return -1;
        }
    }

    // allocate a ni_frame_t structure on the host PC
    ret = ni_frame_buffer_alloc_hwenc(&p_data_out->data.frame,
                                      scaler_params.output_width,
                                      scaler_params.output_height, 0);
    if (ret != 0)
    {
        return -1;
    }

    // out_frame retrieved from decoder
    frame_surface_up = (niFrameSurface1_t *)(p_frame_in_up->p_data[3]);
    frame_surface_bg = (niFrameSurface1_t *)(p_frame_in_bg->p_data[3]);

    // Allocate scaler input frame
    ret = ni_scaler_input_frame_alloc(p_ctx, scaler_params, frame_surface_up);
    if (ret != 0)
    {
        return -1;
    }

    // Allocate scaler destination frame.
    ret = ni_scaler_dest_frame_alloc(p_ctx, scaler_params, frame_surface_bg);
    if (ret != 0)
    {
        return -1;
    }

    // Retrieve hardware frame info from 2D engine and put it in the ni_frame_t structure.
    ret = ni_device_session_read_hwdesc(p_ctx, p_data_out, NI_DEVICE_TYPE_SCALER);
    frame_surface_output = (niFrameSurface1_t *)(p_data_out->data.frame.p_data[3]);
    ni_log(NI_LOG_DEBUG, "%s: output FID %u \n", __func__,
           frame_surface_output->ui16FrameIdx);
    if (ret < 0)
    {
        ni_frame_buffer_free(p_frame_in_up);
        ni_frame_buffer_free(p_frame_in_bg);
        ni_frame_buffer_free(&p_data_out->data.frame);
    }

    return ret;
}

/*!*****************************************************************************
 *  \brief  Use crop->pad->overlay to simulate a drawbox filter. 
 *
 *  \param  
 *
 *  \return 0 if successful, < 0 otherwise
 ******************************************************************************/
int drawbox_filter(ni_session_context_t *p_crop_ctx,
                   ni_session_context_t *p_pad_ctx,
                   ni_session_context_t *p_overlay_ctx,
                   ni_session_context_t *p_fmt_ctx,
                   ni_frame_t *p_frame_in, ni_session_data_io_t *p_data_out,
                   box_params_t *p_box_params,
                   int iXcoderGUID, int input_format, int output_format)
{
    // assume drawing box at the same position (can be set by box_params)
    // set default box params
    int box_width = 80;
    int box_height = 60;
    int box_x = 160;
    int box_y = 100;
    int line_width = 4;
    if (p_box_params)
    {
        box_width = ((int)((p_box_params->box_w + 1) / 2) * 2);
        box_height = ((int)((p_box_params->box_h + 1) / 2) * 2);
        box_x = p_box_params->box_x;
        box_y = p_box_params->box_y;
    }

    /*
        To simulate a drawbox filter, we need to
        1. Crop at the box set position
        2. Padding the crop output frame for fixed fixels (depends on the line_width of the box)
           Recycle the crop hwFrame
        3. Overlay the padding result on the original frame
           Recycle the padding hwFrame
        4. if format-change is needed, launch a scale operation and do format change
           Recycle the overlay hwFrame
    */

    int ret = 0;
    niFrameSurface1_t *p_surface_in;
    p_surface_in = (niFrameSurface1_t *)(p_frame_in->p_data[3]);

    ni_scaler_input_params_t crop_params = {0};
    crop_params.input_format = input_format;
    crop_params.input_width = p_surface_in->ui16width;
    crop_params.input_height = p_surface_in->ui16height;
    crop_params.output_format = GC620_I420;
    crop_params.output_width = box_width;
    crop_params.output_height = box_height;
    init_scaler_params(&crop_params, NI_SCALER_OPCODE_CROP, box_width,
                       box_height, box_x, box_y, 0, 0);
    ni_session_data_io_t crop_data = {0};
    ret = launch_scaler_operation(p_crop_ctx, iXcoderGUID, p_frame_in,
                                  p_frame_in, &crop_data, crop_params);
    if (ret != 0)
    {
        ni_log(NI_LOG_ERROR, "Failed to lauch scaler operation %d\n", crop_params.op);
        return -1;
    }
    niFrameSurface1_t *crop_frame_surface =
        (niFrameSurface1_t *)(crop_data.data.frame.p_data[3]);
    ni_hw_frame_ref(crop_frame_surface);

    ni_scaler_input_params_t pad_params = {0};
    pad_params.input_format = input_format;
    pad_params.input_width = crop_params.output_width;
    pad_params.input_height = crop_params.output_height;
    pad_params.output_format = GC620_I420;
    pad_params.output_width = crop_params.output_width + line_width * 2;
    pad_params.output_height = crop_params.output_height + line_width * 2;
    init_scaler_params(&pad_params, NI_SCALER_OPCODE_PAD, 0, 0, 0, 0,
                       line_width, line_width);
    ni_session_data_io_t pad_data = {0};
    ret = launch_scaler_operation(p_pad_ctx, iXcoderGUID, &crop_data.data.frame,
                                  &crop_data.data.frame, &pad_data, pad_params);
    // recycle HwFrameIdx first, then free the frame
    ni_hw_frame_unref(crop_frame_surface->ui16FrameIdx);
    ni_frame_buffer_free(&(crop_data.data.frame));
    if (ret != 0)
    {
        ni_log(NI_LOG_ERROR, "Failed to lauch scaler operation %d\n", pad_params.op);
        return -1;
    }
    niFrameSurface1_t *pad_frame_surface =
        (niFrameSurface1_t *)(pad_data.data.frame.p_data[3]);
    ni_hw_frame_ref(pad_frame_surface);

    ni_scaler_input_params_t overlay_params = {0};
    overlay_params.input_format = input_format;
    overlay_params.input_width = pad_params.output_width;
    overlay_params.input_height = pad_params.output_height;
    overlay_params.output_format = GC620_I420;
    overlay_params.output_width = p_surface_in->ui16width;
    overlay_params.output_height = p_surface_in->ui16height;
    int overlay_x = (box_width > line_width) ? (box_width - line_width) : 0;
    int overlay_y = (box_height > line_width) ? (box_height - line_width) : 0;
    init_scaler_params(&overlay_params, NI_SCALER_OPCODE_OVERLAY, 0, 0,
                       overlay_x, overlay_y, 0, 0);
    ni_session_data_io_t ovly_data = {0};
    if (output_format == GC620_I420)
        ret = launch_scaler_operation(p_overlay_ctx, iXcoderGUID,
                                      &pad_data.data.frame, p_frame_in,
                                      p_data_out, overlay_params);
    else
        ret = launch_scaler_operation(p_overlay_ctx, iXcoderGUID,
                                      &pad_data.data.frame, p_frame_in,
                                      &ovly_data, overlay_params);
    // recycle HwFrameIdx first, then free the frame
    ni_hw_frame_unref(pad_frame_surface->ui16FrameIdx);
    ni_frame_buffer_free(&(pad_data.data.frame));
    if (ret != 0)
    {
        ni_log(NI_LOG_ERROR, "Failed to lauch scaler operation %d\n", overlay_params.op);
        return -1;
    }

    if (output_format != GC620_I420)   // use scale filter to do format change
    {
        niFrameSurface1_t *ovly_frame_surface =
            (niFrameSurface1_t *)(ovly_data.data.frame.p_data[3]);
        ni_hw_frame_ref(ovly_frame_surface);
        ovly_frame_surface->ui16width = overlay_params.output_width;
        ovly_frame_surface->ui16height = overlay_params.output_height;
        ret = scale_filter(p_fmt_ctx, &(ovly_data.data.frame), p_data_out,
                           iXcoderGUID, overlay_params.output_width,
                           overlay_params.output_height, GC620_I420,
                           output_format);
        ni_hw_frame_unref(ovly_frame_surface->ui16FrameIdx);
        ni_frame_buffer_free(&ovly_data.data.frame);
        if (ret != 0)
        {
            ni_log(NI_LOG_ERROR, "Failed to lauch scaler operation 0\n");
            return -1;
        }
    }

    return 0;
}

/*!*****************************************************************************
 *  \brief  Do a scale and/or format-change operation. 
 *
 *  \param  
 *
 *  \return 0 if successful, < 0 otherwise
 ******************************************************************************/
int scale_filter(ni_session_context_t *p_ctx, ni_frame_t *p_frame_in,
                 ni_session_data_io_t *p_data_out, int iXcoderGUID,
                 int scale_width, int scale_height, int in_format, int out_format)
{
    int ret;
    niFrameSurface1_t *p_surface_in;
    ni_scaler_input_params_t scale_params;

    p_surface_in = (niFrameSurface1_t *)p_frame_in->p_data[3];
    scale_params.output_format = out_format;   // rgba or bgrp or yuv420p;
    scale_params.output_width = scale_width;
    scale_params.output_height = scale_height;
    scale_params.input_format = in_format;
    scale_params.input_width = p_surface_in ? p_surface_in->ui16width : 0;
    scale_params.input_height = p_surface_in ? p_surface_in->ui16height : 0;
    init_scaler_params(&scale_params, NI_SCALER_OPCODE_SCALE, 0, 0, 0, 0, 0, 0);

    ret = launch_scaler_operation(p_ctx, iXcoderGUID, p_frame_in, p_frame_in,
                                  p_data_out, scale_params);
    if (ret != 0)
    {
        ni_log(NI_LOG_ERROR, "Failed to lauch scaler operation %d\n", scale_params.op);
        return -1;
    }

    return ret;
}

/*!*****************************************************************************
 *  \brief  Write scaler p2p or hwdl output.
 *
 *  \param  
 *
 *  \return 0 if successful, < 0 otherwise
 ******************************************************************************/
int scaler_output_write(ni_session_context_t *p_ctx,
                        ni_session_data_io_t *p_data_out, int out_width,
                        int out_height, int out_format, FILE *fp,
                        disp_buffer_t *disp)
{
    int ret;
    niFrameSurface1_t *p_surface;

    if (!fp)
        return 0;

    p_surface = (niFrameSurface1_t *)p_data_out->data.frame.p_data[3];
    p_surface->ui16width = out_width;
    p_surface->ui16height = out_height;
    p_surface->bit_depth = 1;
    p_surface->encoding_type = NI_PIXEL_PLANAR_FORMAT_PLANAR;

    if (p_ctx->isP2P)
    {
#ifndef _WIN32
        ret = read_from_dmabuf(p_ctx, p_surface, disp, out_format, fp);
        if (ret != 0)
        {
            ni_hw_frame_unref(p_surface->ui16FrameIdx);
            ni_frame_buffer_free(&(p_data_out->data.frame));
            return -1;
        }
#endif
    } else
    {
        // use hwdownload to download hw frame, recycle hwframe buffer
        ni_session_data_io_t hwdl_session_data = {0};
        ret = hwdl_frame(p_ctx, &hwdl_session_data, &p_data_out->data.frame,
                         out_format);
        if (ret <= 0)
        {
            ni_hw_frame_unref(p_surface->ui16FrameIdx);
            ni_frame_buffer_free(&(p_data_out->data.frame));
            return -1;
        }

        // write hwdl frame to output_file
        write_rawvideo_data(fp, out_width, out_height, out_format,
                            &hwdl_session_data.data.frame);
        ni_frame_buffer_free(&hwdl_session_data.data.frame);
    }

    ni_hw_frame_unref(p_surface->ui16FrameIdx);

    return 0;
}

// final scan clean up of ref counted HW frame pool, return number of recycled
// HW frames.
static int scan_and_clean_hwdescriptors(void)
{
    int i;
    int recycled = 0;

    for (i = 0; i < NI_MAX_DR_HWDESC_FRAME_INDEX; i++)
    {
        if (g_hwframe_pool[i].ref_cnt &&
            g_hwframe_pool[i].surface.ui16FrameIdx)
        {
            ni_log(NI_LOG_DEBUG, "clean/recycle frame idx %u ref_cnt %d\n,",
                   g_hwframe_pool[i].surface.ui16FrameIdx,
                   g_hwframe_pool[i].ref_cnt);
            ni_hwframe_buffer_recycle2(&g_hwframe_pool[i].surface);
            g_hwframe_pool[i].ref_cnt = 0;
            recycled++;
        }
    }

    return recycled;
}

// reference HW frame
static void ni_hw_frame_ref(const niFrameSurface1_t *p_surface)
{
    uint16_t hwframe_index;

    if (!p_surface)
    {
        return;
    }

    hwframe_index = p_surface->ui16FrameIdx;
    g_hwframe_pool[hwframe_index].ref_cnt++;
    if (1 == g_hwframe_pool[hwframe_index].ref_cnt)
    {
        memcpy(&g_hwframe_pool[hwframe_index].surface, p_surface,
               sizeof(niFrameSurface1_t));
    }
    ni_log(NI_LOG_TRACE, "%s frame idx %u ref_cnt %d ..\n", __func__,
           hwframe_index, g_hwframe_pool[hwframe_index].ref_cnt);
}

// unref HW frame
static void ni_hw_frame_unref(uint16_t hwframe_index)
{
    if (g_hwframe_pool[hwframe_index].ref_cnt > 0)
    {
        g_hwframe_pool[hwframe_index].ref_cnt--;
        if (0 == g_hwframe_pool[hwframe_index].ref_cnt &&
            g_hwframe_pool[hwframe_index].surface.ui16FrameIdx)
        {
            ni_log(NI_LOG_TRACE, "%s frame idx recycing frame idx %u\n", __func__,
                   g_hwframe_pool[hwframe_index].surface.ui16FrameIdx);

            ni_hwframe_buffer_recycle2(&g_hwframe_pool[hwframe_index].surface);
        }
        ni_log(NI_LOG_TRACE, "%s frame idx %u ref_cnt now: %d\n", __func__,
               hwframe_index, g_hwframe_pool[hwframe_index].ref_cnt);
    } else
    {
        ni_log(NI_LOG_ERROR, "%s error frame idx %u ref_cnt %d <= 0\n",
               __func__, hwframe_index,
               g_hwframe_pool[hwframe_index].ref_cnt);
    }
}

static niFrameSurface1_t *hwupload_frame(ni_session_context_t *p_upl_ctx,
                                         ni_session_context_t *p_sca_ctx,
                                         ni_session_data_io_t *p_sw_data,
                                         ni_session_data_io_t *p_hw_data,
                                         ni_session_data_io_t *p_scale_data,
                                         ni_pix_fmt_t pix_fmt, int width,
                                         int height, int pfs, void *yuv_buf,
                                         unsigned long *bytes_sent, int *eos)
{
    int ret, chunk_size;
    uint64_t offset;
    niFrameSurface1_t *p_hwframe = NULL;

    chunk_size = read_yuv_from_file(pfs, yuv_buf, width, height, pix_fmt,
                                    NI_SW_PIX_FMT_NONE, eos,
                                    p_upl_ctx->session_run_state);
    if (chunk_size < 0)
    {
        ni_log(NI_LOG_ERROR, "Error: read yuv file error\n");
        return NULL;
    }

    // need to have the hwframe before open encoder
    ret = upload_send_data_get_desc(p_upl_ctx, p_sw_data, p_hw_data, width,
                                    height, *eos ? NULL : yuv_buf, bytes_sent);
    if (p_upl_ctx->status == NI_RETCODE_NVME_SC_WRITE_BUFFER_FULL)
    {
        ni_log(NI_LOG_DEBUG, "No space to write to, try to read a packet\n");
        //file was read so reset read pointer and try again
        data_left_size += chunk_size;
        if (number_of_frames_in_file &&
            (number_of_frames > number_of_frames_in_file))
        {
            offset = chunk_size * (number_of_frames % number_of_frames_in_file);
        } else
        {
            offset = chunk_size * number_of_frames;
        }
        lseek(pfs, offset, SEEK_SET);
        return NULL;
    } else if (ret)
    {
        ni_log(NI_LOG_ERROR, "Error: upload frame error\n");
        return NULL;
    }

    //to determine if from same device and buffer dimensions in memory
    //needs to be done where input frame is available to check
    p_hwframe = (niFrameSurface1_t *)p_hw_data->data.frame.p_data[3];
    if (p_hw_data->data.frame.end_of_stream)
    {
        // reach eos
        return p_hwframe;
    }

    // need to convert into pixel format for NI encoding
    if (!is_ni_enc_pix_fmt(pix_fmt))
    {
        ni_hw_frame_ref(p_hwframe);
        ret = scale_filter(p_sca_ctx, &p_hw_data->data.frame, p_scale_data,
                           p_upl_ctx->hw_id, width, height,
                           ni_to_gc620_pix_fmt(pix_fmt), GC620_I420);
        ni_hw_frame_unref(p_hwframe->ui16FrameIdx);
        if (ret)
        {
            ni_log(NI_LOG_ERROR, "Error: upload frame error\n");
            return NULL;
        }
        p_hwframe = (niFrameSurface1_t *)p_scale_data->data.frame.p_data[3];
    }

    return p_hwframe;
}

static int encoder_open(ni_session_context_t *enc_ctx_list,
                        ni_xcoder_params_t *p_api_param_list,
                        int output_total, char p_enc_conf_params[][2048],
                        char p_enc_conf_gop[][2048],
                        ni_frame_t *p_ni_frame, int width, int height,
                        int fps_num, int fps_den, int bitrate,
                        int codec_format, ni_pix_fmt_t pix_fmt,
                        int aspect_ratio_idc, int xcoder_guid,
                        niFrameSurface1_t *p_surface, int multi_thread,
                        ni_rate_emu_t *p_enc_rate_emu_list,
                        bool check_zerocopy)
{
    int i, ret = 0;
    int color_prim = NI_COL_PRI_UNSPECIFIED;
    int color_trc = NI_COL_TRC_UNSPECIFIED;
    int color_space = NI_COL_SPC_UNSPECIFIED;
    int sar_num = 0;
    int sar_den = 0;
    int video_full_range_flag = 0;

    if (p_ni_frame != NULL)
    {
        // open the encode session when the first frame arrives and the session
        // is not opened yet, with the source stream and user-configured encode
        // info both considered when constructing VUI in the stream headers
        color_prim = p_ni_frame->color_primaries;
        color_trc = p_ni_frame->color_trc;
        color_space = p_ni_frame->color_space;
        sar_num = p_ni_frame->sar_width;
        sar_den = p_ni_frame->sar_height;
        video_full_range_flag = p_ni_frame->video_full_range_flag;

        // calculate the source fps and set it as the default target fps, based
        // on the timing_info passed in from the decoded frame
        if (p_ni_frame->vui_num_units_in_tick && p_ni_frame->vui_time_scale)
        {
            if (NI_CODEC_FORMAT_H264 == p_ni_frame->src_codec)
            {
                if (0 == p_ni_frame->vui_time_scale % 2)
                {
                    fps_num = (int)(p_ni_frame->vui_time_scale / 2);
                    fps_den = (int)(p_ni_frame->vui_num_units_in_tick);
                } else
                {
                    fps_num = (int)(p_ni_frame->vui_time_scale);
                    fps_den = (int)(2 * p_ni_frame->vui_num_units_in_tick);
                }
            } else if (NI_CODEC_FORMAT_H265 == p_ni_frame->src_codec)
            {
                fps_num = p_ni_frame->vui_time_scale;
                fps_den = p_ni_frame->vui_num_units_in_tick;
            }
        }
    }

    for (i = 0; i < output_total; i++)
    {
        // set up encoder p_config, using some info from source
        ret = ni_encoder_init_default_params(&p_api_param_list[i], fps_num,
                                             fps_den, bitrate, width, height,
                                             enc_ctx_list[i].codec_format);
        if (ret < 0)
        {
            ni_log(NI_LOG_ERROR, "Error encoder[%d] init default set up error\n", i);
            return -1;
        }

        // check and set ni_encoder_params from --xcoder-params
        // Note: the parameter setting has to be in this order so that user
        //       configured values can overwrite the source/default ones if
        //       desired.
        if (ni_retrieve_xcoder_params(p_enc_conf_params[i],
                                      &p_api_param_list[i], &enc_ctx_list[i]))
        {
            ni_log(NI_LOG_ERROR, "Error: encoder[%d] p_config parsing error\n", i);
            return -1;
        }

        if (ni_retrieve_xcoder_gop(p_enc_conf_gop[i],
                                   &p_api_param_list[i], &enc_ctx_list[i]))
        {
            ni_log(NI_LOG_ERROR, "Error: encoder[%d] p_config_gop parsing error\n", i);
            return -1;
        }

        // set async mode in enc_ctx if encoding is multi-threaded
        if (multi_thread)
        {
            ni_log(NI_LOG_INFO, "Encoder[%d] is multi-threaded, set async mode "
                   "in the session context!\n", i);
            enc_ctx_list[i].async_mode = 1;
        }

        // check color primaries configuration
        if (color_prim != p_api_param_list[i].color_primaries &&
            NI_COL_PRI_UNSPECIFIED != p_api_param_list[i].color_primaries)
        {
            ni_log(NI_LOG_DEBUG, "Encoder[%d] user-configured color primaries "
                   "%d to overwrite source %d\n",
                   i, p_api_param_list[i].color_primaries, color_prim);
            color_prim = p_api_param_list[i].color_primaries;
        }

        // check color transfer characteristic configuration
        if (color_trc != p_api_param_list[i].color_transfer_characteristic &&
            NI_COL_TRC_UNSPECIFIED != p_api_param_list[i].color_transfer_characteristic)
        {
            ni_log(NI_LOG_DEBUG, "Encoder[%d] user-configured color trc %d to "
                   "overwrite source %d\n", i,
                   p_api_param_list[i].color_transfer_characteristic, color_trc);
            color_trc = p_api_param_list[i].color_transfer_characteristic;
        }

        // check color space configuration
        if (color_space != p_api_param_list[i].color_space &&
            NI_COL_SPC_UNSPECIFIED != p_api_param_list[i].color_space)
        {
            ni_log(NI_LOG_DEBUG, "Encoder[%d] user-configured color space %d "
                   "to overwrite source %d\n",
                   i, p_api_param_list[i].color_space, color_space);
            color_space = p_api_param_list[i].color_space;
        }

        // check video full range flag configuration
        if (p_api_param_list[i].video_full_range_flag >= 0)
        {
            ni_log(NI_LOG_DEBUG, "Encoder[%d] user-configured video full range "
                   "flag %d\n", i, p_api_param_list[i].video_full_range_flag);
            video_full_range_flag = p_api_param_list[i].video_full_range_flag;
        }

        // check aspect ratio indicator configuration
        if (aspect_ratio_idc > 0 && aspect_ratio_idc < NI_NUM_PIXEL_ASPECT_RATIO)
        {
            sar_num = ni_h264_pixel_aspect_list[aspect_ratio_idc].num;
            sar_den = ni_h264_pixel_aspect_list[aspect_ratio_idc].den;
        } else if (p_api_param_list[i].sar_denom)
        {
            sar_num = p_api_param_list[i].sar_num;
            sar_den = p_api_param_list[i].sar_denom;
        }

        // check hwframe configuration
        if (p_surface != NULL)
        {
            //Items in this else condition could be improved by being handled in libxcoder
            enc_ctx_list[i].hw_action = NI_CODEC_HW_ENABLE;
            p_api_param_list[i].hwframes = 1;
            enc_ctx_list[i].sender_handle =
                (ni_device_handle_t)(int64_t)p_surface->device_handle;
            p_api_param_list[i].rootBufId = p_surface->ui16FrameIdx;
        }

        ret = encoder_open_session(&enc_ctx_list[i], codec_format, xcoder_guid,
                                   &p_api_param_list[i], width, height,
                                   color_prim, color_trc, color_space,
                                   video_full_range_flag, sar_num, sar_den,
                                   pix_fmt, p_enc_rate_emu_list,
                                   check_zerocopy);
        if (ret != 0)
        {
            ni_log(NI_LOG_ERROR, "Error encoder[%d] open session failed!\n", i);
            return -1;
        }
    }

    return ret;
}

static int encoder_receive(ni_session_context_t *enc_ctx_list,
                           ni_session_data_io_t *in_frame,
                           ni_session_data_io_t *pkt, int width, int height,
                           uint32_t *number_of_packets_list,
                           int output_total, FILE **pfs_list,
                           unsigned long long *total_bytes_received_list,
                           int print_time, device_state_t *xcoder_state)
{
    int i, recycle_index;
    int recv_fin_flag = NI_TEST_RETCODE_SUCCESS;
    uint32_t prev_num_pkt[MAX_OUTPUT_FILES] = {0};

    for (i = 0; i < output_total; i++)
    {
        pkt->data.packet.end_of_stream = 0;
        prev_num_pkt[i] = number_of_packets_list[i];

        recv_fin_flag = encoder_receive_data(&enc_ctx_list[i], pkt, width,
                                             height, pfs_list[i],
                                             &total_bytes_received_list[i],
                                             print_time,
                                             &number_of_packets_list[i],
                                             in_frame);

        recycle_index = pkt->data.packet.recycle_index;
        if (prev_num_pkt[i] < number_of_packets_list[i] &&
            enc_ctx_list[i].hw_action && recycle_index > 0 &&
            recycle_index < NI_GET_MAX_HWDESC_FRAME_INDEX(enc_ctx_list[i].ddr_config))
        {
            //encoder only returns valid recycle index
            //when there's something to recycle.
            //This range is suitable for all memory bins
            ni_hw_frame_unref(recycle_index);
        } else
        {
            ni_log(NI_LOG_DEBUG, "enc %d recv, prev_num_pkt %u "
                   "number_of_packets_list %u recycle_index %u\n", i,
                   prev_num_pkt[i], number_of_packets_list[i], recycle_index);
        }

        if (prev_num_pkt[i] < number_of_packets_list[i] &&
            enc_ctx_list[i].codec_format == NI_CODEC_FORMAT_AV1)
        {
            // For low delay mode encoding, only one packet is received for one
            // frame sent. For non low delay mode, there will be multiple
            // packets received for one frame sent. So we need to read out all
            // the packets encoded.
            ni_xcoder_params_t *p_api_param =
                (ni_xcoder_params_t *)enc_ctx_list[i].p_session_config;
            if (!p_api_param->low_delay_mode)
            {
                (void)ni_gettimeofday(&current_time, NULL);
                print_time = need_time_print(&current_time, &previous_time);
                i--;
                continue;
            }
        }

        xcoder_state[i].enc_eos_received = pkt->data.packet.end_of_stream;

        if (recv_fin_flag < 0)
        {
            ni_log(NI_LOG_DEBUG, "enc %d error, quit !\n", i);
            break;
        } else if (recv_fin_flag == NI_TEST_RETCODE_EAGAIN)
        {
            ni_usleep(100);
        }
    }

    return recv_fin_flag;
}

static void decoder_stat_report_and_close(ni_session_context_t *p_dec_ctx,
                                 unsigned long long total_bytes_received)
{
    uint32_t time_diff;

    time_diff = (uint32_t)(current_time.tv_sec - start_time.tv_sec);
    if (time_diff == 0)
        time_diff = 1;

    ni_log(NI_LOG_ERROR, "[R] Got:  Frames= %u  fps=%f  Total bytes %llu\n",
           number_of_frames, (float)number_of_frames / (float)time_diff,
           total_bytes_received);

    ni_device_session_close(p_dec_ctx, 1, NI_DEVICE_TYPE_DECODER);
}

static void encoder_stat_report_and_close(ni_session_context_t *p_enc_ctx_list,
                                  int output_total,
                                  uint32_t *number_of_packets_list,
                                  unsigned long long *total_bytes_received_list)
{
    int i;
    int nb_recycled;
    uint32_t time_diff;

    time_diff = (uint32_t)(current_time.tv_sec - start_time.tv_sec);
    if (time_diff == 0)
        time_diff = 1;

    nb_recycled = scan_and_clean_hwdescriptors();

    for (i = 0; i < output_total; i++)
    {
        ni_log(NI_LOG_ERROR, "[R] Got:  enc %d Packets= %u fps=%f Total bytes %llu\n",
               (int)i, number_of_packets_list[i],
               (float)p_enc_ctx_list[i].frame_num / (float)time_diff,
               total_bytes_received_list[i]);

        ni_device_session_close(&p_enc_ctx_list[i], 1, NI_DEVICE_TYPE_ENCODER);
    }

    ni_log(NI_LOG_DEBUG, "Cleanup recycled %d internal buffers\n",
           nb_recycled);
}

int main(int argc, char *argv[])
{
    tx_data_t sdPara = {0};
    rx_data_t rcPara = {0};
    device_state_t xcoder_state = {0};
    // those variables with MAX_OUTPUT_FILES entries are for the support of
    // HW frame ladder encoding only.
    device_state_t xcoder_state_list[MAX_OUTPUT_FILES] = {0};
    int pfs = 0;
    char xcoderGUID[32];
    int iXcoderGUID = 0;
    unsigned long total_bytes_sent;
    uint32_t number_of_packets_list[MAX_OUTPUT_FILES] = {0};
    unsigned long long total_bytes_received;
    unsigned long long total_bytes_received_list[MAX_OUTPUT_FILES];
    FILE *p_file_list[MAX_OUTPUT_FILES];
    char outFileNameList[MAX_OUTPUT_FILES * FILE_NAME_LEN];
    char *n;   // used for parsing width and height from --size
    char mode_description[128];
    int end_of_all_streams = 0;
    int input_video_width;
    int input_video_height;
    int arg_width;
    int arg_height;
    int fps_num = 30;
    int fps_den = 1;
    int bitrate = 200000;
    int mode = -1;
    int multi_thread = 0;
    size_t i;
    int pkt_size;
    ni_xcoder_params_t *p_dec_api_param;
    ni_xcoder_params_t *p_enc_api_param_list = NULL;
    niFrameSurface1_t *p_hwframe = NULL;
    ni_test_frame_list_t frame_list = {0};
    char encConfXcoderParamsList[MAX_OUTPUT_FILES][2048] = {0};
    char encConfXcoderGopList[MAX_OUTPUT_FILES][2048] = {0};
    char decConfXcoderParams[2048] = {0};
    char scaConfXcoderParams[2048] = {0};
    int ret = 0;
    ni_log_level_t log_level = NI_LOG_INFO;
    int print_time = 0;
    ni_rate_emu_t rate_emu = {0};
    int src_codec_format = 0, dst_codec_format = 0;
    int bit_depth = 8;
    void *yuv_buf;
    int input_arg_width[MAX_INPUT_FILES] = {0};
    int input_arg_height[MAX_INPUT_FILES] = {0};
    int input_bit_depth[MAX_INPUT_FILES];
    int input_arg_pfs[MAX_INPUT_FILES] = {0};
    ni_h264_sps_t SPS = {0};   // input header SPS
    ni_h265_sps_t HEVC_SPS = {0};
    ni_vp9_header_info_t VP9_INFO = {0};
    int input_index = 0, output_o_index = 0, output_x_index = 0, output_g_index = -1;
    int input_total, output_total = 0;
    int size_index = 0;
    int bit_depth_index = 0;
    int user_data_sei_passthru = 0;
    ni_pix_fmt_t pix_fmt = NI_PIX_FMT_YUV420P;
    ni_sw_pix_fmt_t sw_pix_fmt = NI_SW_PIX_FMT_NONE;
    int rx_size;
    int chunk_size;
    int eos = 0;
    int send_fin_flag = 0;
    int receive_fin_flag = 0;
    // Input arg handling
    int opt;
    int opt_index;
    const char *opt_string = "hvtl:c:i:s:m:b:x:g:d:f:o:r:e:p:u:";
    static struct option long_options[] = {
        {"help", no_argument, NULL, 'h'},
        {"version", no_argument, NULL, 'v'},
        {"multi-thread", no_argument, NULL, 't'},
        {"loglevel", required_argument, NULL, 'l'},
        {"card", required_argument, NULL, 'c'},
        {"input", required_argument, NULL, 'i'},
        {"size", required_argument, NULL, 's'},
        {"mode", required_argument, NULL, 'm'},
        {"bitdepth", required_argument, NULL, 'b'},
        {"xcoder-params", required_argument, NULL, 'x'},
        {"xcoder-gop", required_argument, NULL, 'g'},
        {"decoder-params", required_argument, NULL, 'd'},
        {"scaler-params", required_argument, NULL, 'f'},
        {"output", required_argument, NULL, 'o'},
        {"repeat", required_argument, NULL, 'r'},
        {"readframerate", required_argument, NULL, 'e'},
        {"pix_fmt", required_argument, NULL, 'p'},
        {"user_data_sei_passthru", required_argument, NULL, 'u'},
        {NULL, 0, NULL, 0},
    };

    while ((opt = getopt_long(argc, argv, opt_string, long_options,
                              &opt_index)) != -1)
    {
        switch (opt)
        {
            case 'h':
                print_usage();
                exit(0);
            case 'v':
               printf( "Release ver: %s\n"
                       "API ver:     %s\n"
                       "Date:        %s\n"
                       "ID:          %s\n",
                       NI_XCODER_REVISION, LIBXCODER_API_VERSION,
                       NI_SW_RELEASE_TIME, NI_SW_RELEASE_ID);
                exit(0);
            case 't':
                multi_thread = 1;
                break;
            case 'l':
                log_level = arg_to_ni_log_level(optarg);
                if (log_level != NI_LOG_INVALID)
                {
                    ni_log_set_level(log_level);
                } else {
                    arg_error_exit("-l | --loglevel", optarg);
                }
                break;
            case 'c':
                strcpy(xcoderGUID, optarg);
                iXcoderGUID = (int)strtol(optarg, &n, 10);
                if (n ==
                    xcoderGUID)   // no numeric characters found in left side of optarg
                    arg_error_exit("-c | --card", optarg);
                break;
            case 'i':
                if (input_index == MAX_INPUT_FILES)
                {
                    ni_log(NI_LOG_ERROR, "Error: number of input files cannot exceed %d\n", MAX_INPUT_FILES);
                    exit(-1);
                }
                strcpy(sdPara.fileName[input_index], optarg);
                input_index++;
                break;
            case 's':
                if (size_index == MAX_INPUT_FILES)
                {
                    ni_log(NI_LOG_ERROR, "Error: number of input resolutions cannot exceed %d\n", MAX_INPUT_FILES);
                    exit(-1);
                }
                input_arg_width[size_index] = (int)strtol(optarg, &n, 10);
                input_arg_height[size_index] = atoi(n + 1);
                if ((*n != 'x') || (!input_arg_width[size_index] || !input_arg_height[size_index]))
                    arg_error_exit("-s | --size", optarg);
                size_index++;
                break;
            case 'm':
                if (!(strlen(optarg) == 3))
                    arg_error_exit("-, | --mode", optarg);
                // convert to lower case for processing
                for (i = 0; i < strlen(optarg); i++)
                    optarg[i] = (char)tolower((unsigned char)optarg[i]);

                if (strcmp(optarg, "y2a") != 0 && strcmp(optarg, "y2h") != 0 &&
                    strcmp(optarg, "a2y") != 0 && strcmp(optarg, "a2a") != 0 &&
                    strcmp(optarg, "a2h") != 0 && strcmp(optarg, "h2y") != 0 &&
                    strcmp(optarg, "h2a") != 0 && strcmp(optarg, "h2h") != 0 &&
                    strcmp(optarg, "u2a") != 0 && strcmp(optarg, "u2h") != 0 &&
                    strcmp(optarg, "a2r") != 0 && strcmp(optarg, "h2r") != 0 &&
                    strcmp(optarg, "u2x") != 0 && strcmp(optarg, "y2x") != 0 &&
                    strcmp(optarg, "v2y") != 0 && strcmp(optarg, "y2o") != 0 &&
                    strcmp(optarg, "v2a") != 0 && strcmp(optarg, "v2h") != 0 &&
                    strcmp(optarg, "v2x") != 0 && strcmp(optarg, "a2x") != 0 &&
                    strcmp(optarg, "h2x") != 0)
                    arg_error_exit("-, | --mode", optarg);

                // determine dec/enc/xcod mode to use
                if (optarg[0] == 'y')
                {
                    sprintf(mode_description, "Encoding");
                    mode = XCODER_APP_ENCODE;
                } else if (optarg[2] == 'y')
                {
                    sprintf(mode_description, "Decoding");
                    mode = XCODER_APP_DECODE;
                } else if ((optarg[0] == 'y') && (optarg[2] == 'y'))
                {
                    arg_error_exit("-, | --mode", optarg);
                } else if (optarg[0] == 'u')
                {
                    sprintf(mode_description, "HWUpload + Encoding");
                    mode = XCODER_APP_HWUP_ENCODE;
                } else if (optarg[2] == 'r')
                {
                    sprintf(mode_description, "Decoding + filtering");
                    mode = XCODER_APP_FILTER;
                } else
                {
                    sprintf(mode_description, "Transcoding");
                    mode = XCODER_APP_TRANSCODE;
                }

                // determine codecs to use
                if (optarg[0] == 'a')
                {
                    src_codec_format = NI_CODEC_FORMAT_H264;
                    strcat(mode_description, " from AVC");
                }
                if (optarg[0] == 'h')
                {
                    src_codec_format = NI_CODEC_FORMAT_H265;
                    strcat(mode_description, " from HEVC");
                }
                if (optarg[0] == 'v')
                {
                    src_codec_format = NI_CODEC_FORMAT_VP9;
                    strcat(mode_description, " from VP9");
                }
                if (optarg[2] == 'a')
                {
                    dst_codec_format = NI_CODEC_FORMAT_H264;
                    strcat(mode_description, " to AVC");
                }
                if (optarg[2] == 'h')
                {
                    dst_codec_format = NI_CODEC_FORMAT_H265;
                    strcat(mode_description, " to HEVC");
                }
                if (optarg[2] == 'x')
                {
                    dst_codec_format = NI_CODEC_FORMAT_AV1;
                    strcat(mode_description, " to AV1");
                }
                if (optarg[2] == 'o')
                {
                    dst_codec_format = NI_CODEC_FORMAT_AV1;
                    av1_output_obu = 1;
                    strcat(mode_description, " to AV1 OBU");
                }                
                break;
            case 'b':
                if (bit_depth_index == MAX_INPUT_FILES)
                {
                    ni_log(NI_LOG_ERROR, "Error: cannot input more than %d bitdepth\n",
                           MAX_INPUT_FILES);
                    exit(-1);
                }
                if (!(atoi(optarg) == 8 || atoi(optarg) == 10))
                    arg_error_exit("-b | --bitdepth", optarg);
                input_bit_depth[bit_depth_index] = atoi(optarg);
                bit_depth_index++;
                break;
            case 'x':
                if (output_x_index == MAX_OUTPUT_FILES)
                {
                    ni_log(NI_LOG_ERROR, "Error: number of encoder config cannot exceed %d\n",
                           MAX_OUTPUT_FILES);
                    exit(-1);
                }

                // Set the index of corresponding xcoder-gop the same as the current index of xcoder-params
                output_g_index = output_x_index;
                strcpy(encConfXcoderParamsList[output_x_index], optarg);
                output_x_index++;
                break;
            case 'g':
                if(output_g_index != output_x_index - 1)
                {
                    ni_log(NI_LOG_ERROR, "Error: xcoder-gop must follow xcoder-params, not the other way around\n");
                    exit(-1);
                }

                strcpy(encConfXcoderGopList[output_g_index], optarg);
                break;
            case 'd':
                strcpy(decConfXcoderParams, optarg);
                break;
            case 'f':
                strcpy(scaConfXcoderParams, optarg);
                break;
            case 'o':
                strcpy(rcPara.fileName, optarg);
                if (output_o_index == MAX_OUTPUT_FILES)
                {
                    ni_log(NI_LOG_ERROR, "Error: number of output files cannot exceed %d\n",
                           MAX_OUTPUT_FILES);
                    exit(-1);
                }

                for (i = 0; i < output_o_index; i++)
                {
                    if (0 == strcmp(&outFileNameList[i * FILE_NAME_LEN],
                                    optarg))
                    {
                        ni_log(NI_LOG_ERROR, "Error: output file names must be unique: %s\n",
                               optarg);
                        exit(-1);
                    }
                }

                strcpy(&outFileNameList[output_o_index * FILE_NAME_LEN],
                       optarg);
                output_o_index++;
                break;
            case 'r':
                if (!(atoi(optarg) >= 1))
                    arg_error_exit("-r | --repeat", optarg);
                g_repeat = atoi(optarg);
                break;
            case 'p':
                pix_fmt = ni_pixel_format_search(optarg);
                if (pix_fmt == NI_PIX_FMT_NONE)
                {
                    if (!strcmp(optarg, "yuv444p"))
                    {
                        sw_pix_fmt = NI_SW_PIX_FMT_YUV444P;
                    }
                    else
                    {
                        arg_error_exit("-p | --pix_fmt", optarg);
                    }
                }
                break;
            case 'u':
                user_data_sei_passthru = atoi(optarg) >= 1;
                break;
            case 'e':
                if (atoi(optarg) < 0)
                    arg_error_exit("-e | --readframerate", optarg);
                rate_emu.rate_emu_framerate = atoi(optarg);
                break;
            default:
                print_usage();
                exit(1);
        }
    }

    // readframerate parameter check
    if (rate_emu.rate_emu_framerate != 0)
    {
        if (mode != XCODER_APP_ENCODE)
        {
            ni_log(NI_LOG_ERROR, "Error: readframerate %d setting only available for ENCODE mode - y2a, y2h, y2x, y2o\n", rate_emu.rate_emu_framerate);
            exit(-1);
        }
    }

    // sequence change input parameter check
    if (input_index != size_index)
    {
        if (mode == XCODER_APP_ENCODE)
        {
            ni_log(NI_LOG_ERROR, "Error: number of input files %d does not match with number of input resolutions %d\n", input_index, size_index);
            exit(-1);
        }
    }

    input_total = input_index;      

    if (input_total > 1)
    {
        if (mode != XCODER_APP_ENCODE)
        {
            ni_log(NI_LOG_ERROR, "Error: multiple input files not supported for mode %d\n", mode);
            exit(-1);
        }

        if (g_repeat > 1)
        {
            ni_log(NI_LOG_ERROR, "Error: multiple input files not supported when repeat %d greater than one\n", g_repeat);
            exit(-1);
        }
    }

    // checking for HW frame ladder encode support
    if (output_x_index > 1 && output_o_index != output_x_index)
    {
        ni_log(NI_LOG_ERROR, "Error: number of output files: %d does not match number of "
               "encode config: %d\n",
               output_o_index, output_x_index);
        exit(-1);
    }

    if ((sw_pix_fmt == NI_SW_PIX_FMT_YUV444P ||
        sw_pix_fmt == NI_SW_PIX_FMT_YUV444P10LE) && output_o_index != 2)
    {
        ni_log(NI_LOG_ERROR, "Error: Must indicate 2 output files for yuv444p encoding\n");
        exit(-1);
    }

    if (output_o_index > 1 && NI_CODEC_FORMAT_AV1 == dst_codec_format)
    {
        ni_log(NI_LOG_ERROR, "Error: multi-encoding is only supported for non-AV1 codec\n");
        exit(-1);
    }

    output_total = output_o_index;

    for ( ; bit_depth_index < MAX_INPUT_FILES; bit_depth_index++)
    {
        input_bit_depth[bit_depth_index] = 8;
    }

    arg_width = input_arg_width[0];
    arg_height = input_arg_height[0];
    bit_depth = input_bit_depth[0];

    // Check required args are present
    if (!sdPara.fileName[0][0])
    {
        ni_log(NI_LOG_ERROR, "Error: missing argument for -i | --input\n");
        exit(-1);
    }

    if (ni_posix_memalign(&yuv_buf, sysconf(_SC_PAGESIZE), MAX_YUV_FRAME_SIZE))
    {
        ni_log(NI_LOG_ERROR, "Error: failed to allocate YUV data buffer");
        exit(-1);
    }

    p_dec_api_param = malloc(sizeof(*p_dec_api_param));
    if (!p_dec_api_param)
    {
        ni_log(NI_LOG_ERROR, "Error: failed to allocate p_dec_api_param\n");
        exit(-1);
    }

    for (input_index = 0; input_index < input_total; input_index++)
    {
        if (!sdPara.fileName[input_index][0])
        {
            ni_log(NI_LOG_ERROR, "Error: incorrect input file %d\n", input_index);
            exit(-1);
        }
    
#ifdef _WIN32
        input_arg_pfs[input_index] = open(sdPara.fileName[input_index], O_RDONLY | O_BINARY);
#else
        input_arg_pfs[input_index] = open(sdPara.fileName[input_index], O_RDONLY);
#endif

        if (input_arg_pfs[input_index] < 0)
        {
            ni_log(NI_LOG_ERROR, "Error: cannot open %s\n", sdPara.fileName[input_index]);
            ni_log(NI_LOG_ERROR, "Error: input file read failure\n");
            goto end;
        }
        ni_log(NI_LOG_INFO, "SUCCESS: Opened input file: %s with file id = %d\n",
               sdPara.fileName[input_index], input_arg_pfs[input_index]);
    }

    input_index = 0;
    pfs = input_arg_pfs[0];

    if ((mode != XCODER_APP_TRANSCODE) && (mode != XCODER_APP_DECODE) &&
        (mode != XCODER_APP_ENCODE) && (mode != XCODER_APP_HWUP_ENCODE) &&
        (mode != XCODER_APP_FILTER))
    {
        ni_log(NI_LOG_ERROR, "Error: missing argument for -m | --mode\n");
        exit(-1);
    }
    if (!rcPara.fileName[0])
    {
        ni_log(NI_LOG_ERROR, "Error: missing argument for -o | --output\n");
        exit(-1);
    }

    sdPara.mode = mode;
    rcPara.mode = mode;

    // Print high-level description of processing to occur and codecs involved
    ni_log(NI_LOG_INFO, "%s...\n", mode_description);

    pkt_size = 131040;   // hardcoded input data chunk size (for H.265 1080p)

    get_total_file_size(pfs);
    if (mode == XCODER_APP_ENCODE || mode == XCODER_APP_HWUP_ENCODE)
    {
        ni_log(NI_LOG_DEBUG, "YUV input read will repeat %d times\n", g_repeat);
        ni_log(NI_LOG_DEBUG, "YUV input, do not read all to cache\n");
    } else
    {
        uint32_t read_chunk = 4096;
        uint64_t tmpFileSize = total_file_size;

        //try to allocate memory for input file buffer, quit if failure
        if (total_file_size > 0 && !(g_file_cache = malloc(total_file_size)))
        {
            ni_log(NI_LOG_ERROR,
                    "Error: input file size %llu exceeding max malloc, quit\n",
                    (unsigned long long)total_file_size);
            goto end;
        }
        g_curr_cache_pos = g_file_cache;
        ni_log(NI_LOG_INFO, "Reading %llu bytes in total ..\n", (unsigned long long)total_file_size);

        while (tmpFileSize)
        {
            if (read_chunk > tmpFileSize)
            {
                read_chunk = tmpFileSize;
            }
            int one_read_size = read(pfs, g_curr_cache_pos, read_chunk);
            if (one_read_size == -1)
            {
                ni_log(NI_LOG_ERROR, "Error: reading file, quit! left-to-read %llu\n",
                        (unsigned long long)tmpFileSize);
                ni_log(NI_LOG_ERROR, "Error: input file read error\n");
                goto end;
            } else
            {
                tmpFileSize -= one_read_size;
                g_curr_cache_pos += one_read_size;
            }
        }
        ni_log(NI_LOG_INFO, "read %llu bytes from input file into memory\n", (unsigned long long)total_file_size);
    }
    g_curr_cache_pos = g_file_cache;

    // HWUP + ladder encode single-thread uses multi output setup
    for (i = 0; i < output_total; i++)
    {
        if (strcmp(&outFileNameList[i * FILE_NAME_LEN], "null") != 0 &&
            strcmp(&outFileNameList[i * FILE_NAME_LEN], "/dev/null") != 0)
        {
            p_file_list[i] =
                fopen(&outFileNameList[i * FILE_NAME_LEN], "wb");
            if (p_file_list[i] == NULL)
            {
                ni_log(NI_LOG_ERROR, "Error: cannot open %s\n",
                        &outFileNameList[i * FILE_NAME_LEN]);
                goto end;
            }
            ni_log(NI_LOG_INFO, "SUCCESS: Opened output file: %s\n",
                   &outFileNameList[i * FILE_NAME_LEN]);
        } else
        {
            p_file_list[i] = NULL;
            ni_log(NI_LOG_INFO, "SUCCESS: output %d file %s implies dropping output\n",
                   (int)i + 1, &outFileNameList[i * FILE_NAME_LEN]);
        }
    }

    p_enc_api_param_list = calloc(output_total, sizeof(ni_xcoder_params_t));
    if (!p_enc_api_param_list)
    {
        ni_log(NI_LOG_ERROR, "Error: calloc for enc ladder parameters.\n");
        goto end;
    }

    // for H.264, probe the source and use the probed source info as defaults
    if (NI_CODEC_FORMAT_H264 == src_codec_format &&
        (mode == XCODER_APP_TRANSCODE || mode == XCODER_APP_DECODE ||
         mode == XCODER_APP_FILTER))
    {
        if (probe_h264_stream_info(&SPS))
        {
            ni_log(NI_LOG_ERROR,
                    "ERROR: H.264 file probing complete, source file format "
                    "not supported !\n");
            goto end;
        }

        ni_log(NI_LOG_DEBUG, "Using probed H.264 source info: %d bits "
                       "resolution %dx%d\n",
                       SPS.bit_depth_luma, SPS.width, SPS.height);
        bit_depth = SPS.bit_depth_luma;
        arg_width = SPS.width;
        arg_height = SPS.height;
    } else if (NI_CODEC_FORMAT_H265 == src_codec_format &&
               (mode == XCODER_APP_DECODE || mode == XCODER_APP_TRANSCODE))
    {
        if (probe_h265_stream_info(&HEVC_SPS))
        {
            ni_log(NI_LOG_ERROR,
                    "ERROR: H.265 file probing complete, source file format "
                    "not supported !\n");
            goto end;
        }
        bit_depth = HEVC_SPS.bit_depth_chroma;
        arg_width = (int)(HEVC_SPS.width -
                          (HEVC_SPS.pic_conf_win.left_offset +
                           HEVC_SPS.pic_conf_win.right_offset));
        arg_height = (int)(HEVC_SPS.height -
                           (HEVC_SPS.pic_conf_win.top_offset +
                            HEVC_SPS.pic_conf_win.bottom_offset));
        ni_log(NI_LOG_INFO,
               "Using probed H.265 source info: %d bits "
               "resolution %dx%d\n",
               HEVC_SPS.bit_depth_chroma, arg_width, arg_height);
    } else if (NI_CODEC_FORMAT_VP9 == src_codec_format &&
               (mode == XCODER_APP_DECODE || mode == XCODER_APP_TRANSCODE))
    {
        if (probe_vp9_stream_info(&VP9_INFO))
        {
            ni_log(NI_LOG_ERROR,
                    "ERROR: VP9 file probing complete, source file format "
                    "not supported !\n");
            goto end;
        }
        ni_log(NI_LOG_INFO,
               "Using probed VP9 source info: %d profile "
               "resolution %ux%u, timebase %u/%u\n",
               VP9_INFO.profile, VP9_INFO.width, VP9_INFO.height,
               VP9_INFO.timebase.den, VP9_INFO.timebase.num);
        bit_depth = VP9_INFO.profile ? 10 : 8;
        arg_width = VP9_INFO.width;
        arg_height = VP9_INFO.height;
    }

    sdPara.arg_width = arg_width;
    sdPara.arg_height = arg_height;
    rcPara.arg_width = arg_width;
    rcPara.arg_height = arg_height;

    // set up decoder p_config with some hard coded numbers
    if (ni_decoder_init_default_params(p_dec_api_param, 25, 1, 200000, arg_width,
                                       arg_height) < 0)
    {
        ni_log(NI_LOG_ERROR, "Error: decoder p_config set up error\n");
        return -1;
    }

    send_fin_flag = 0;
    receive_fin_flag = 0;

    ni_session_context_t dec_ctx = {0};
    ni_session_context_t enc_ctx_list[MAX_OUTPUT_FILES] = {0};
    ni_session_context_t upl_ctx = {0};
    ni_session_context_t sca_ctx = {0};
    ni_session_context_t crop_ctx = {0};
    ni_session_context_t pad_ctx = {0};
    ni_session_context_t ovly_ctx = {0};
    ni_session_context_t fmt_ctx = {0};
    ni_rate_emu_t enc_rate_emu_list[MAX_OUTPUT_FILES] = {0};

    if (ni_device_session_context_init(&dec_ctx) < 0)
    {
        ni_log(NI_LOG_ERROR, "Error: init decoder context error\n");
        return -1;
    }
    for (i = 0; i < output_total; i++)
    {
        if (ni_device_session_context_init(&enc_ctx_list[i]) < 0)
        {
            ni_log(NI_LOG_ERROR, "Error: init encoder ladder %d context error\n",
                    (int)i);
            return -1;
        }
    }

    if (ni_device_session_context_init(&upl_ctx) < 0)
    {
        ni_log(NI_LOG_ERROR, "Error: init decoder context error\n");
        return -1;
    }

    if (ni_device_session_context_init(&sca_ctx) < 0)
    {
        ni_log(NI_LOG_ERROR, "Error: init decoder context error\n");
        return -1;
    }

    if (ni_device_session_context_init(&crop_ctx) < 0)
    {
        ni_log(NI_LOG_ERROR, "Error: init decoder context error\n");
        return -1;
    }

    if (ni_device_session_context_init(&pad_ctx) < 0)
    {
        ni_log(NI_LOG_ERROR, "Error: init decoder context error\n");
        return -1;
    }

    if (ni_device_session_context_init(&ovly_ctx) < 0)
    {
        ni_log(NI_LOG_ERROR, "Error: init decoder context error\n");
        return -1;
    }

    if (ni_device_session_context_init(&fmt_ctx) < 0)
    {
        ni_log(NI_LOG_ERROR, "Error: init decoder context error\n");
        return -1;
    }

    sdPara.p_dec_ctx = (void *)&dec_ctx;
    sdPara.p_enc_ctx = (void *)&enc_ctx_list[0];
    sdPara.p_upl_ctx = (void *)&upl_ctx;
    sdPara.p_sca_ctx = (void *)&sca_ctx;
    sdPara.p_crop_ctx = (void *)&crop_ctx;
    sdPara.p_pad_ctx = (void *)&pad_ctx;
    sdPara.p_ovly_ctx = (void *)&ovly_ctx;
    sdPara.p_fmt_ctx = (void *)&fmt_ctx;
    rcPara.p_dec_ctx = (void *)&dec_ctx;
    rcPara.p_enc_ctx = (void *)&enc_ctx_list[0];
    rcPara.p_upl_ctx = (void *)&upl_ctx;
    rcPara.p_sca_ctx = (void *)&sca_ctx;
    rcPara.p_crop_ctx = (void *)&crop_ctx;
    rcPara.p_pad_ctx = (void *)&pad_ctx;
    rcPara.p_ovly_ctx = (void *)&ovly_ctx;
    rcPara.p_fmt_ctx = (void *)&fmt_ctx;

    for (i = 0; i < output_total; i++)
    {
        enc_ctx_list[i].nb_rois = 0;
        enc_ctx_list[i].roi_side_data_size = 0;
        enc_ctx_list[i].nb_rois = 0;
        enc_ctx_list[i].roi_side_data_size = 0;
        enc_ctx_list[i].av_rois = NULL;
        enc_ctx_list[i].codec_format = dst_codec_format;
        enc_rate_emu_list[i].rate_emu_framerate = rate_emu.rate_emu_framerate;
    }

    if (mode == XCODER_APP_TRANSCODE || mode == XCODER_APP_DECODE ||
        mode == XCODER_APP_FILTER)
    {
        dec_ctx.p_session_config = NULL;
        dec_ctx.session_id = NI_INVALID_SESSION_ID;
        dec_ctx.codec_format = src_codec_format;

        // user_data_sei_passthru disabled by default
        if ((NI_CODEC_FORMAT_H264 == src_codec_format) || (NI_CODEC_FORMAT_H265 == src_codec_format))
            dec_ctx.enable_user_data_sei_passthru = user_data_sei_passthru;

        // no need to directly allocate resource context
        rcPara.p_dec_rsrc_ctx = sdPara.p_dec_rsrc_ctx = NULL;

        // assign the card GUID in the decoder context and let session open
        // take care of the rest
        dec_ctx.device_handle = dec_ctx.blk_io_handle =
            NI_INVALID_DEVICE_HANDLE;
        dec_ctx.hw_id = iXcoderGUID;

        dec_ctx.p_session_config = p_dec_api_param;
        // default: little endian
        dec_ctx.src_bit_depth = bit_depth;
        dec_ctx.src_endian = NI_FRAME_LITTLE_ENDIAN;
        dec_ctx.bit_depth_factor = 1;
        if (10 == dec_ctx.src_bit_depth)
        {
            dec_ctx.bit_depth_factor = 2;
        }

        // check and set ni_decoder_params from --xcoder-params
        if (ni_retrieve_decoder_params(decConfXcoderParams, p_dec_api_param,
                                       &dec_ctx))
        {
            ni_log(NI_LOG_ERROR, "Error: encoder p_config parsing error\n");
            return -1;
        }
        if (p_dec_api_param->dec_input_params.hwframes &&
            mode == XCODER_APP_DECODE &&
            strcmp(rcPara.fileName, "null") != 0 &&
            strcmp(rcPara.fileName, "/dev/null") != 0)
        {
            ni_log(NI_LOG_ERROR, "Error: decode only with hwframes supports null "
                    "output only\n");
            goto end;
        }

        // set async mode in dec_ctx if multi-threaded
        if ((mode == XCODER_APP_DECODE || mode == XCODER_APP_TRANSCODE) &&
            multi_thread)
        {
            ni_log(NI_LOG_INFO, "Decoding is multi-threaded, set async mode in "
                   "session context !\n");
            dec_ctx.async_mode = 1;
        }

        // Decode, use all the parameters specified by user
        ret = decoder_open_session(&dec_ctx, iXcoderGUID, p_dec_api_param);
        if (ret != 0)
        {
            goto end;
        }
    }

    if (mode == XCODER_APP_TRANSCODE || mode == XCODER_APP_ENCODE ||
        mode == XCODER_APP_HWUP_ENCODE || mode == XCODER_APP_FILTER)
    {
        // no need to directly allocate resource context for encoder
        rcPara.p_enc_rsrc_ctx = sdPara.p_enc_rsrc_ctx = NULL;
        rcPara.p_upl_rsrc_ctx = sdPara.p_upl_rsrc_ctx = NULL;
        rcPara.p_sca_rsrc_ctx = sdPara.p_sca_rsrc_ctx = NULL;
        rcPara.p_crop_rsrc_ctx = sdPara.p_crop_rsrc_ctx = NULL;
        rcPara.p_pad_rsrc_ctx = sdPara.p_pad_rsrc_ctx = NULL;
        rcPara.p_ovly_rsrc_ctx = sdPara.p_ovly_rsrc_ctx = NULL;
        rcPara.p_fmt_rsrc_ctx = sdPara.p_fmt_rsrc_ctx = NULL;
    }

    // encoder session open, if needed, will be at the first frame arrival as it
    // carries source stream info that may be useful in encoding config

    total_bytes_received = 0;
    total_bytes_sent = 0;

    ni_log(NI_LOG_INFO, "user video resolution: %dx%d\n", arg_width, arg_height);
    if (arg_width == 0 || arg_height == 0)
    {
        input_video_width = 1280;
        input_video_height = 720;
    } else
    {
        input_video_width = arg_width;
        input_video_height = arg_height;
    }
    int output_video_width = input_video_width;
    int output_video_height = input_video_height;

    (void)ni_gettimeofday(&start_time, NULL);
    (void)ni_gettimeofday(&previous_time, NULL);
    (void)ni_gettimeofday(&current_time, NULL);
    start_timestamp = privious_timestamp = current_timestamp = time(NULL);

    if (bit_depth == 10 && (pix_fmt != NI_PIX_FMT_YUV420P10LE &&
        pix_fmt != NI_PIX_FMT_P010LE))
    {
        ni_log(NI_LOG_INFO, "Warning: The -b | --bit_depth option would be "
               "deprecated in the future. If you have indicated both of the "
               "pixel format and bith depth option here, please notice the "
               "compatibility. Here it would set the pixel format as "
               "yuv420p10le by force with bit depth of 10!\n");
        pix_fmt = NI_PIX_FMT_YUV420P10LE;
    }

    if (mode == XCODER_APP_DECODE)
    {
        void *p_stream_info = NULL;
        ni_session_data_io_t in_pkt = {0};
        ni_session_data_io_t out_frame = {0};

        ni_log(NI_LOG_INFO, "Decoding Mode: %dx%d to %dx%d HWFrames %d\n", input_video_width,
               input_video_height, output_video_width, output_video_height,
               dec_ctx.hw_action);

        if (src_codec_format == NI_CODEC_FORMAT_H264)
            p_stream_info = &SPS;
        else if (src_codec_format == NI_CODEC_FORMAT_H265)
            p_stream_info = &HEVC_SPS;
        else if (src_codec_format == NI_CODEC_FORMAT_VP9)
            p_stream_info = &VP9_INFO;

        if (multi_thread)
        {
            ni_pthread_t dec_send_tid, dec_recv_tid;
            dec_send_param_t dec_send_param = {0};
            dec_recv_param_t dec_recv_param = {0};

            dec_send_param.p_dec_ctx = &dec_ctx;
            dec_send_param.p_in_pkt = &in_pkt;
            dec_send_param.input_video_width = input_video_width;
            dec_send_param.input_video_height = input_video_height;
            dec_send_param.pkt_size = pkt_size;
            dec_send_param.print_time = 0;   // not support now
            dec_send_param.p_total_bytes_sent = &total_bytes_sent;
            dec_send_param.p_xcoder_state = &xcoder_state;
            dec_send_param.p_stream_info = p_stream_info;

            dec_recv_param.p_dec_ctx = &dec_ctx;
            dec_recv_param.p_out_frame = &out_frame;
            dec_recv_param.output_video_width = output_video_width;
            dec_recv_param.output_video_height = output_video_height;
            dec_recv_param.p_file = p_file_list[0];
            dec_recv_param.p_total_bytes_received = &total_bytes_received;
            dec_recv_param.p_xcoder_state = &xcoder_state;
            dec_recv_param.mode = mode;
            dec_recv_param.frame_list = NULL;

            if (ni_pthread_create(&dec_send_tid, NULL, decoder_send_thread,
                                  &dec_send_param))
            {
                ni_log(NI_LOG_ERROR,
                        "Error: create decoder send thread failed in decode "
                        "mode\n");
                return -1;
            }
            if (ni_pthread_create(&dec_recv_tid, NULL, decoder_receive_thread,
                                  &dec_recv_param))
            {
                ni_log(NI_LOG_ERROR,
                        "Error: create decoder receive thread failed in decode "
                        "mode\n");
                return -1;
            }

            ni_pthread_join(dec_send_tid, NULL);
            ni_pthread_join(dec_recv_tid, NULL);

            (void)ni_gettimeofday(&current_time, NULL);
        } else
        {
            while (send_fin_flag == 0 || receive_fin_flag == 0)
            {
                (void)ni_gettimeofday(&current_time, NULL);
                print_time = need_time_print(&current_time, &previous_time);

                // Sending
                send_fin_flag = decoder_send_data(&dec_ctx, &in_pkt,
                                                  input_video_width,
                                                  input_video_height, pkt_size,
                                                  &total_bytes_sent, print_time,
                                                  &xcoder_state, p_stream_info);
                if (send_fin_flag < 0)
                {
                    ni_log(NI_LOG_ERROR,
                            "Error: decoder_send_data() failed, rc: %d\n",
                            send_fin_flag);
                    break;
                }

                // Receiving
                do
                {
                    rx_size = 0;
                    receive_fin_flag = decoder_receive_data(&dec_ctx, &out_frame,
                                                            output_video_width,
                                                            output_video_height,
                                                            p_file_list[0],
                                                            &total_bytes_received,
                                                            print_time, 1,
                                                            &xcoder_state,
                                                            &rx_size);

                    if (dec_ctx.hw_action == NI_CODEC_HW_ENABLE)
                    {
                        if (receive_fin_flag != NI_TEST_RETCODE_EAGAIN &&
                            receive_fin_flag != NI_TEST_RETCODE_END_OF_STREAM)
                        {
                            ni_frame_t *p_ni_frame = &out_frame.data.frame;
                            p_hwframe = (niFrameSurface1_t *)p_ni_frame->p_data[3];
                            ni_log(NI_LOG_DEBUG, "decoder_receive_data HW decode-only. recycle HW frame idx %u\n", p_hwframe->ui16FrameIdx);
                            ni_hwframe_buffer_recycle2(p_hwframe);
                            ni_frame_buffer_free(p_ni_frame);
                        }
                    }
                    else
                    {
                        ni_decoder_frame_buffer_free(&(out_frame.data.frame));
                    }

                    if (print_time)
                    {
                        previous_time = current_time;
                    }

                    // Error or eos
                    if (receive_fin_flag < 0 || out_frame.data.frame.end_of_stream)
                    {
                        break;
                    }

                    (void)ni_gettimeofday(&current_time, NULL);
                    print_time = need_time_print(&current_time, &previous_time);
                } while (!dec_ctx.decoder_low_delay && rx_size > 0); //drain consecutive outputs
                                                                     // Error or eos
                if (receive_fin_flag < 0 || out_frame.data.frame.end_of_stream)
                {
                    break;
                }
            }
        }

        decoder_stat_report_and_close(&dec_ctx, total_bytes_received);

        ni_rsrc_free_device_context(sdPara.p_dec_rsrc_ctx);
        rcPara.p_dec_rsrc_ctx = NULL;
        sdPara.p_dec_rsrc_ctx = NULL;

        ni_packet_buffer_free(&(in_pkt.data.packet));
        if (dec_ctx.hw_action != NI_CODEC_HW_ENABLE)
        {
            ni_decoder_frame_buffer_free(&(out_frame.data.frame));
        }
    } else if (mode == XCODER_APP_ENCODE)
    {
        ni_session_data_io_t in_frame = {0};
        ni_session_data_io_t out_packet = {0};
        ni_session_data_io_t sw_pix_frame[2];
        memset(sw_pix_frame, 0, 2 * sizeof(ni_session_data_io_t));

        ni_log(NI_LOG_INFO, "Encoding Mode: %dx%d to %dx%d\n", input_video_width,
               input_video_height, output_video_width, output_video_height);

        in_frame.data.frame.pixel_format = pix_fmt;
        ret = encoder_open(&enc_ctx_list[0], p_enc_api_param_list, output_total,
                           &encConfXcoderParamsList[0], &encConfXcoderGopList[0], NULL, arg_width,
                           arg_height, fps_num, fps_den, bitrate,
                           dst_codec_format, pix_fmt, 0, iXcoderGUID, NULL,
                           multi_thread, &enc_rate_emu_list[0],
                           (sw_pix_fmt != NI_SW_PIX_FMT_NONE) ? false : true); // zero copy is currently not supported for YUV444P
        if (ret != 0)
        {
            goto end;
        }

        if (multi_thread)
        {
            ni_pthread_t enc_send_tid, enc_recv_tid;
            enc_send_param_t enc_send_param = {0};
            enc_recv_param_t enc_recv_param = {0};

            enc_send_param.p_enc_ctx = &enc_ctx_list[0];
            enc_send_param.p_in_frame = &in_frame;
            enc_send_param.input_video_width = input_video_width;
            enc_send_param.input_video_height = input_video_height;
            enc_send_param.pfs = pfs;
            enc_send_param.yuv_buf = yuv_buf;
            memcpy(enc_send_param.input_arg_width, input_arg_width, sizeof(enc_send_param.input_arg_width));
            memcpy(enc_send_param.input_arg_height, input_arg_height, sizeof(enc_send_param.input_arg_height));
            memcpy(enc_send_param.input_bit_depth, input_bit_depth, sizeof(enc_send_param.input_bit_depth));
            memcpy(enc_send_param.input_arg_pfs, input_arg_pfs, sizeof(enc_send_param.input_arg_pfs));
            enc_send_param.output_total = output_total;
            enc_send_param.p_total_bytes_sent = &total_bytes_sent;
            enc_send_param.p_xcoder_state = &xcoder_state_list[0];
            enc_send_param.mode = mode;
            enc_send_param.frame_list = NULL;
            enc_send_param.dec_codec_format = -1;           // not use heres
            enc_send_param.input_total = input_total;            
            enc_send_param.sw_pix_fmt = sw_pix_fmt;
            enc_send_param.p_rate_emu = &enc_rate_emu_list[0];

            enc_recv_param.p_enc_ctx = &enc_ctx_list[0];
            enc_recv_param.p_out_packet = &out_packet;
            enc_recv_param.output_video_width = output_video_width;
            enc_recv_param.output_video_height = output_video_height;
            enc_recv_param.p_file = &p_file_list[0];
            enc_recv_param.output_total = output_total;
            enc_recv_param.p_number_of_packets = &number_of_packets_list[0];
            enc_recv_param.p_total_bytes_received = &total_bytes_received_list[0];
            enc_recv_param.p_xcoder_state = &xcoder_state_list[0];
            enc_recv_param.mode = mode;
            enc_recv_param.p_buffered_frame = &in_frame;

            if (ni_pthread_create(&enc_send_tid, NULL, encoder_send_thread,
                                  &enc_send_param))
            {
                ni_log(NI_LOG_ERROR, "Error: create encoder send thread failed in "
                        "encode mode\n");
                return -1;
            }
            if (ni_pthread_create(&enc_recv_tid, NULL, encoder_receive_thread,
                                  &enc_recv_param))
            {
                ni_log(NI_LOG_ERROR, "Error: create encoder recieve thread failed "
                        "in encode mode\n");
                return -1;
            }

            ni_pthread_join(enc_send_tid, NULL);
            ni_pthread_join(enc_recv_tid, NULL);
            (void)ni_gettimeofday(&current_time, NULL);
        } else
        {
            while (!end_of_all_streams &&
                (send_fin_flag == NI_TEST_RETCODE_SUCCESS ||
                receive_fin_flag == NI_TEST_RETCODE_SUCCESS))
            {
                (void)ni_gettimeofday(&current_time, NULL);
                print_time = need_time_print(&current_time, &previous_time);

                // encoder send again
                if (send_fin_flag == NI_TEST_RETCODE_EAGAIN)
                {
                    goto enc_mode_send;
                }

                chunk_size = read_yuv_from_file(input_arg_pfs[input_index],
                                                yuv_buf,
                                                input_arg_width[input_index],
                                                input_arg_height[input_index],
                                                pix_fmt, sw_pix_fmt, &eos,
                                                enc_ctx_list[0].session_run_state);
                if (chunk_size < 0)
                {
                    goto end;
                }

                // YUV444P reading
                if (sw_pix_fmt != NI_SW_PIX_FMT_NONE)
                {
                    ret = convert_yuv_444p_to_420p(&sw_pix_frame[0],
                                              eos ? NULL : yuv_buf,
                                              input_arg_width[input_index],
                                              input_arg_height[input_index],
                                              sw_pix_fmt, 0, dst_codec_format);
                    if (ret < 0)
                    {
                        goto end;
                    }
                }

enc_mode_send:
                for (i = 0; i < output_total; i++)
                {
                    if (sw_pix_fmt != NI_SW_PIX_FMT_NONE)
                    {
                        send_fin_flag = encoder_send_data3(&enc_ctx_list[i],
                                                &sw_pix_frame[i],
                                                input_arg_width[input_index],
                                                input_arg_height[input_index],
                                                &xcoder_state_list[i], eos);
                    } else
                    {
                        send_fin_flag = encoder_send_data(&enc_ctx_list[i],
                                                &in_frame, eos ? NULL : yuv_buf,
                                                input_arg_width[input_index],
                                                input_arg_height[input_index],
                                                &total_bytes_sent,
                                                &xcoder_state_list[i],
                                                input_index == input_total - 1,
                                                &enc_rate_emu_list[i]);
                    }

                    if (send_fin_flag == NI_TEST_RETCODE_EAGAIN)
                    {
                        break;
                    } else if (send_fin_flag == NI_TEST_RETCODE_NEXT_INPUT) // next input (will trigger sequence change)
                    {
                        input_index++;
                        get_total_file_size(input_arg_pfs[input_index]);
                        send_fin_flag = NI_TEST_RETCODE_SUCCESS;
                    }
                }

                receive_fin_flag = encoder_receive(&enc_ctx_list[0],
                                                  &in_frame, &out_packet,
                                                  output_video_width,
                                                  output_video_height,
                                                  &number_of_packets_list[0],
                                                  output_total, &p_file_list[0],
                                                  &total_bytes_received_list[0],
                                                  print_time,
                                                  &xcoder_state_list[0]);
                for (i = 0; receive_fin_flag >= 0 && i < output_total; i++)
                {
                    if (!xcoder_state_list[i].enc_eos_received)
                    {
                        ni_log(NI_LOG_DEBUG, "enc %d continues to read!\n", i);
                        end_of_all_streams = 0;
                        break;
                    } else
                    {
                        ni_log(NI_LOG_DEBUG, "enc %d eos !\n", i);
                        end_of_all_streams = 1;
                    }
                }

                if (print_time)
                {
                    previous_time = current_time;
                }
            }
        }

        encoder_stat_report_and_close(&enc_ctx_list[0], output_total,
                                      &number_of_packets_list[0],
                                      &total_bytes_received_list[0]);

        ni_rsrc_free_device_context(sdPara.p_enc_rsrc_ctx);
        rcPara.p_enc_rsrc_ctx = NULL;
        sdPara.p_enc_rsrc_ctx = NULL;

        ni_frame_buffer_free(&(in_frame.data.frame));
        ni_packet_buffer_free(&(out_packet.data.packet));
        for (i = 0; i < sizeof(sw_pix_frame)/sizeof(ni_session_data_io_t); i++)
        {
            ni_frame_buffer_free(&sw_pix_frame[i].data.frame);
        }
    } else if (mode == XCODER_APP_HWUP_ENCODE)
    {
        ni_session_data_io_t in_frame = {0};
        ni_session_data_io_t scale_frame = {0};
        ni_session_data_io_t swin_frame = {0};
        ni_session_data_io_t out_packet = {0};
        int is_p2p = 0;
        int pool_size = 3;   //3 starting buffers to be augmented with additional

        ni_log(NI_LOG_INFO, "Upload + Encoding Mode: %dx%d to %dx%d\n", input_video_width,
               input_video_height, output_video_width, output_video_height);
        // open and config uploader for encoding
        int upload_width = arg_width;
        int upload_height = arg_height;
        // pad to minimum encoder supported width and height
        if (arg_width < NI_MIN_WIDTH)
        {
            upload_width = NI_MIN_WIDTH;
        }
        if (arg_height < NI_MIN_HEIGHT)
        {
            upload_height = NI_MIN_HEIGHT;
        }
        // buffers by downstream entity like encoders
        ret = uploader_open_session(&upl_ctx, iXcoderGUID, upload_width, upload_height,
                                    pix_fmt, is_p2p, pool_size);
        if (ret != 0)
        {
            goto end;
        }

        if (multi_thread)
        {
            uploader_param_t uploader_param = {0};
            ni_pthread_t enc_send_tid, enc_recv_tid, uploader_tid;
            enc_send_param_t enc_send_param = {0};
            enc_recv_param_t enc_recv_param = {0};
            ni_frame_t *p_ni_frame = NULL;

            uploader_param.p_upl_ctx = &upl_ctx;
            uploader_param.p_sca_ctx = &sca_ctx;
            uploader_param.p_swin_frame = &swin_frame;
            uploader_param.p_scale_frame = &scale_frame;
            uploader_param.input_video_width = input_video_width;
            uploader_param.input_video_height = input_video_height;
            uploader_param.pfs = pfs;
            uploader_param.yuv_buf = yuv_buf;
            uploader_param.p_total_bytes_sent = &total_bytes_sent;
            uploader_param.pool_size = pool_size;
            uploader_param.frame_list = &frame_list;

            if (ni_pthread_create(&uploader_tid, NULL, uploader_thread,
                                  &uploader_param))
            {
                ni_log(NI_LOG_ERROR, "Error: create uploader thread failed in "
                        "upload mode\n");
                return -1;
            }

            // polling the first received HW frame
            while (frame_list_is_empty(&frame_list) && !g_end_of_all_threads)
            {
                ni_usleep(100);
            }

            if (!g_end_of_all_threads)
            {
                p_ni_frame = &frame_list.frames[frame_list.head].data.frame;
                p_hwframe = (niFrameSurface1_t *)p_ni_frame->p_data[3];

                ret = encoder_open(&enc_ctx_list[0], p_enc_api_param_list,
                                   output_total, &encConfXcoderParamsList[0],
                                   &encConfXcoderGopList[0],
                                   NULL, arg_width, arg_height, fps_num,
                                   fps_den, bitrate, dst_codec_format, pix_fmt,
                                   0, iXcoderGUID, p_hwframe, multi_thread,
                                   &enc_rate_emu_list[0], false);
                if (ret != 0)
                {
                    goto end;
                }

                // init and create encoding thread
                enc_send_param.p_enc_ctx = &enc_ctx_list[0];
                enc_send_param.p_in_frame = NULL;
                enc_send_param.input_video_width = input_video_width;
                enc_send_param.input_video_height = input_video_height;
                enc_send_param.pfs = pfs;
                enc_send_param.yuv_buf = yuv_buf;
                enc_send_param.output_total = output_total;
                enc_send_param.p_total_bytes_sent = &total_bytes_sent;
                enc_send_param.p_xcoder_state = &xcoder_state_list[0];
                enc_send_param.mode = mode;
                enc_send_param.frame_list = &frame_list;
                enc_send_param.dec_codec_format = dec_ctx.codec_format;
                enc_send_param.sw_pix_fmt = NI_SW_PIX_FMT_NONE;
                enc_send_param.p_rate_emu = &enc_rate_emu_list[0];

                enc_recv_param.p_enc_ctx = &enc_ctx_list[0];
                enc_recv_param.p_out_packet = &out_packet;
                enc_recv_param.output_video_width = output_video_width;
                enc_recv_param.output_video_height = output_video_height;
                enc_recv_param.p_file = &p_file_list[0];
                enc_recv_param.output_total = output_total;
                enc_recv_param.p_number_of_packets = &number_of_packets_list[0];
                enc_recv_param.p_total_bytes_received = &total_bytes_received_list[0];
                enc_recv_param.p_xcoder_state = &xcoder_state_list[0];
                enc_recv_param.mode = mode;

                if (ni_pthread_create(&enc_send_tid, NULL, encoder_send_thread,
                                      &enc_send_param))
                {
                    ni_log(NI_LOG_ERROR,
                            "Error: create encoder send thread failed in transcode "
                            "mode\n");
                    return -1;
                }
                if (ni_pthread_create(&enc_recv_tid, NULL, encoder_receive_thread,
                                      &enc_recv_param))
                {
                    ni_log(NI_LOG_ERROR,
                            "Error: create encoder recieve thread failed in "
                            "transcode mode\n");
                    return -1;
                }

                ni_pthread_join(enc_send_tid, NULL);
                ni_pthread_join(enc_recv_tid, NULL);
            }
            ni_pthread_join(uploader_tid, NULL);

            (void)ni_gettimeofday(&current_time, NULL);

            hwframe_list_release(&frame_list, pix_fmt);
        } else
        {
            int read_from_file = 0;
            ni_session_data_io_t *p_in_frame;

            p_hwframe = hwupload_frame(&upl_ctx, &sca_ctx, &swin_frame,
                                       &in_frame, &scale_frame, pix_fmt,
                                       input_video_width, input_video_height,
                                       pfs, yuv_buf, &total_bytes_sent, &eos);
            if (p_hwframe == NULL)
            {
                goto end;
            }

            ret = encoder_open(&enc_ctx_list[0], p_enc_api_param_list,
                      output_total, &encConfXcoderParamsList[0], &encConfXcoderGopList[0],
                      NULL, arg_width, arg_height, fps_num,
                      fps_den, bitrate, dst_codec_format,
                      is_ni_enc_pix_fmt(pix_fmt) ? pix_fmt : NI_PIX_FMT_YUV420P,
                      0, iXcoderGUID, p_hwframe, multi_thread,
                      &enc_rate_emu_list[0], false);
            if (ret != 0)
            {
                goto end;
            }

            while (!end_of_all_streams &&
                   (send_fin_flag == 0 || receive_fin_flag == 0))
            {
                (void)ni_gettimeofday(&current_time, NULL);
                print_time = need_time_print(&current_time, &previous_time);

                if (read_from_file && !eos)
                {
                    p_hwframe = hwupload_frame(&upl_ctx, &sca_ctx, &swin_frame,
                                               &in_frame, &scale_frame, pix_fmt,
                                               input_video_width,
                                               input_video_height, pfs, yuv_buf,
                                               &total_bytes_sent, &eos);
                    if (upl_ctx.status == NI_RETCODE_NVME_SC_WRITE_BUFFER_FULL)
                    {
                        ni_log(NI_LOG_DEBUG, "No space to write to, try to "
                               "read a packet\n");
                        p_in_frame = &in_frame;
                        goto receive_pkt;
                    } else if (p_hwframe == NULL)
                    {
                        ret = -1;
                        goto end;
                    }
                }

                // Sending
                p_in_frame = is_ni_enc_pix_fmt(pix_fmt) ? &in_frame : &scale_frame;
                for (i = 0; i < output_total; i++)
                {
                    send_fin_flag = encoder_send_data3(&enc_ctx_list[i],
                                                       p_in_frame,
                                                       input_video_width,
                                                       input_video_height,
                                                       &xcoder_state_list[i],
                                                       eos);
                    read_from_file = 1;   //since first frame read before while-loop
                    if (send_fin_flag < 0)   //Error
                    {
                        ni_log(NI_LOG_ERROR, "enc %d send error, quit !\n", i);
                        ni_hw_frame_ref(p_hwframe);
                        end_of_all_streams = 1;
                        break;
                    }
                    //track in array with unique index, free when enc read finds
                    //this must be implemented in application space for complete
                    //tracking of hwframes
                    if (!xcoder_state_list[i].enc_resend)
                    {
                        //successful read means there is recycle to check
                        ni_hw_frame_ref(p_hwframe);
                    } else
                    {
                        ni_log(NI_LOG_DEBUG, "enc %d need to re-send !\n", i);
                        ni_usleep(500);
                        i--;
                        continue;
                    }
                }
                if (end_of_all_streams)
                    break;

            receive_pkt:
                receive_fin_flag = encoder_receive(&enc_ctx_list[0],
                                                  p_in_frame, &out_packet,
                                                  output_video_width,
                                                  output_video_height,
                                                  &number_of_packets_list[0],
                                                  output_total, &p_file_list[0],
                                                  &total_bytes_received_list[0],
                                                  print_time,
                                                  &xcoder_state_list[0]);
                for (i = 0; receive_fin_flag >= 0 && i < output_total; i++)
                {
                    if (!xcoder_state_list[i].enc_eos_received)
                    {
                        ni_log(NI_LOG_DEBUG, "enc %d continues to read!\n", i);
                        end_of_all_streams = 0;
                        break;
                    } else
                    {
                        ni_log(NI_LOG_DEBUG, "enc %d eos !\n", i);
                        end_of_all_streams = 1;
                    }
                }

                if (print_time)
                {
                    previous_time = current_time;
                }
            } // encoder send/receive while loop
        } // encoder send/receive single thread mode

        encoder_stat_report_and_close(&enc_ctx_list[0], output_total,
                                      &number_of_packets_list[0],
                                      &total_bytes_received_list[0]);

        ni_device_session_close(&upl_ctx, 1, NI_DEVICE_TYPE_UPLOAD);
        if (!is_ni_enc_pix_fmt(pix_fmt))
        {   //Uploading rgba requires scaler conversion so close the session too
            ni_device_session_close(&sca_ctx, 1, NI_DEVICE_TYPE_SCALER);
        }
        ni_rsrc_free_device_context(sdPara.p_enc_rsrc_ctx);
        rcPara.p_enc_rsrc_ctx = NULL;
        sdPara.p_enc_rsrc_ctx = NULL;
        rcPara.p_upl_rsrc_ctx = NULL;
        sdPara.p_upl_rsrc_ctx = NULL;

        ni_frame_buffer_free(&in_frame.data.frame);
        ni_frame_buffer_free(&swin_frame.data.frame);
        ni_packet_buffer_free(&(out_packet.data.packet));
    } else if (mode == XCODER_APP_FILTER)
    {
#ifndef _WIN32
        // not support on windows

        ni_log(NI_LOG_INFO, "Scaling Mode\n");
        // default filter params
        ni_filter_params_t filter_params = {0};
        filter_params.scale_width = 1280;
        filter_params.scale_height = 720;
        filter_params.format = GC620_I420;

        ret = retrieve_filter_params(scaConfXcoderParams, &filter_params);
        if (ret != 0)
        {
            return -1;
        }

        int scale_width = filter_params.scale_width;
        int scale_height = filter_params.scale_height;
        int out_format = filter_params.format;
        if (filter_params.p2p)
        {
            sca_ctx.isP2P = 1;
            if (out_format != GC620_I420)
                fmt_ctx.isP2P = 1;
            else
                ovly_ctx.isP2P = 1;
        }

        disp_buffer_t *disp_buf[2] = { NULL };
        if (filter_params.p2p)
        {
            for (i = 0; i < sizeof(disp_buf)/sizeof(disp_buffer_t *); i++)
            {
                disp_buf[i] = calloc(1, sizeof(disp_buffer_t));
            }
        }

        ni_session_data_io_t in_pkt = {0};
        ni_session_data_io_t out_frame = {0};
        FILE *p_ovly_out = NULL;

        if (p_file_list[0])
        {
            char ovly_file[FILE_NAME_LEN] = {0};
            strcat(ovly_file, "overlay_");
            strcat(ovly_file, rcPara.fileName);
            ni_log(NI_LOG_INFO, "overlay output filename %s\n", ovly_file);
            p_ovly_out = fopen(ovly_file, "wb");
            if (!p_ovly_out)
            {
                ni_log(NI_LOG_ERROR, "Error: cannot open %s\n", ovly_file);
                goto end;
            }
        }

        while (send_fin_flag == 0 || receive_fin_flag == 0)
        {
            (void)ni_gettimeofday(&current_time, NULL);
            print_time = need_time_print(&current_time, &previous_time);

            // Sending
            send_fin_flag = decoder_send_data(&dec_ctx, &in_pkt,
                                              input_video_width,
                                              input_video_height, pkt_size,
                                              &total_bytes_sent, print_time,
                                              &xcoder_state, &SPS);
            if (send_fin_flag < 0)
            {
                ni_log(NI_LOG_ERROR, "Error: decoder_send_data() failed, rc: %d\n",
                        send_fin_flag);
                break;
            }

            // Receiving
            receive_fin_flag = decoder_receive_data(
                &dec_ctx, &out_frame, output_video_width, output_video_height,
                p_file_list[0], &total_bytes_received, print_time, 0,
                &xcoder_state, &rx_size);   // not write to file
            if (print_time)
            {
                previous_time = current_time;
            }

            if (receive_fin_flag < 0 || out_frame.data.frame.end_of_stream)
            {
                ni_frame_buffer_free(&(out_frame.data.frame));
                break;
            } else if (receive_fin_flag != 2)
            {
                // got yuv hwframe from decoder. save dec hwframe idx
                p_hwframe = (niFrameSurface1_t *)out_frame.data.frame.p_data[3];
                ni_hw_frame_ref(p_hwframe);

                // set decoded hwframe descriptor
                p_hwframe->ui16width = output_video_width;
                p_hwframe->ui16height = output_video_height;
                p_hwframe->bit_depth = 1;
                p_hwframe->encoding_type = NI_PIXEL_PLANAR_FORMAT_PLANAR;

                // scale the decoded frame(always yuv) and write to output file
                ni_session_data_io_t scale_session_data = {0};
                ret = scale_filter(&sca_ctx, &out_frame.data.frame,
                                   &scale_session_data, iXcoderGUID,scale_width,
                                   scale_height, GC620_I420, out_format);
                if (ret != 0)
                    break;

                ret = scaler_output_write(&sca_ctx, &scale_session_data,
                                          scale_width, scale_height, out_format,
                                          p_file_list[0], disp_buf[0]);
                if (ret != 0)
                    break;

                // draw box on the decoded frame and write to output file
                ni_session_data_io_t drawbox_session_data = {0};
                ret = drawbox_filter(&crop_ctx, &pad_ctx, &ovly_ctx, &fmt_ctx,
                                     &out_frame.data.frame,
                                     &drawbox_session_data, NULL, iXcoderGUID,
                                     GC620_I420, out_format);
                if (ret != 0)
                    break;

                if (out_format == GC620_I420)
                    ret = scaler_output_write(
                        &ovly_ctx, &drawbox_session_data, output_video_width,
                        output_video_height, out_format, p_ovly_out, disp_buf[1]);
                else
                    ret = scaler_output_write(
                        &fmt_ctx, &drawbox_session_data, output_video_width,
                        output_video_height, out_format, p_ovly_out, disp_buf[1]);
                if (ret != 0)
                    break;

                // free decoded hw frame
                ni_hw_frame_unref(p_hwframe->ui16FrameIdx);
                // free decoded frame and scaler output frame
                ni_frame_buffer_free(&out_frame.data.frame);
                ni_frame_buffer_free(&scale_session_data.data.frame);
                ni_frame_buffer_free(&drawbox_session_data.data.frame);

            } else
            {
                ni_frame_buffer_free(&(out_frame.data.frame));
            }
        }

        decoder_stat_report_and_close(&dec_ctx, total_bytes_received);

        // close device
        if (filter_params.p2p)
        {
            for (i = 0; i < sizeof(disp_buf)/sizeof(disp_buffer_t *); i++)
            {
                if (disp_buf[i]->fd >= 0)
                {
                    if (disp_buf[i]->mmap_data != MAP_FAILED)
                    {
                        munmap((void *)disp_buf[i]->mmap_data,disp_buf[i]->len);
                    }
                    close(disp_buf[i]->fd);
                    free(disp_buf[i]->data);
                }
                free(disp_buf[i]);
            }
        }

        ni_device_session_close(&sca_ctx, 1, NI_DEVICE_TYPE_SCALER);
        ni_device_session_close(&crop_ctx, 1, NI_DEVICE_TYPE_SCALER);
        ni_device_session_close(&pad_ctx, 1, NI_DEVICE_TYPE_SCALER);
        ni_device_session_close(&ovly_ctx, 1, NI_DEVICE_TYPE_SCALER);
        if (fmt_ctx.device_handle != NI_INVALID_DEVICE_HANDLE)
            ni_device_session_close(&fmt_ctx, 1, NI_DEVICE_TYPE_SCALER);
        ni_rsrc_free_device_context(sdPara.p_dec_rsrc_ctx);
        ni_rsrc_free_device_context(sdPara.p_sca_rsrc_ctx);
        ni_rsrc_free_device_context(sdPara.p_crop_rsrc_ctx);
        ni_rsrc_free_device_context(sdPara.p_pad_rsrc_ctx);
        ni_rsrc_free_device_context(sdPara.p_ovly_rsrc_ctx);
        ni_rsrc_free_device_context(sdPara.p_fmt_rsrc_ctx);
        rcPara.p_dec_rsrc_ctx = NULL;
        rcPara.p_sca_rsrc_ctx = NULL;
        rcPara.p_crop_rsrc_ctx = NULL;
        rcPara.p_pad_rsrc_ctx = NULL;
        rcPara.p_ovly_rsrc_ctx = NULL;
        rcPara.p_fmt_rsrc_ctx = NULL;
        sdPara.p_dec_rsrc_ctx = NULL;
        sdPara.p_sca_rsrc_ctx = NULL;
        sdPara.p_crop_rsrc_ctx = NULL;
        sdPara.p_pad_rsrc_ctx = NULL;
        sdPara.p_ovly_rsrc_ctx = NULL;
        sdPara.p_fmt_rsrc_ctx = NULL;

        scan_and_clean_hwdescriptors();
        ni_packet_buffer_free(&in_pkt.data.packet);
        ni_frame_buffer_free(&out_frame.data.frame);
#endif
    } else if (mode == XCODER_APP_TRANSCODE)
    {
        ni_session_data_io_t in_pkt = {0};
        ni_session_data_io_t out_frame = {0};
        ni_session_data_io_t enc_in_frame = {0};
        ni_session_data_io_t out_packet = {0};
        ni_frame_t *p_ni_frame = NULL;
        void *p_stream_info = NULL;

        ni_log(NI_LOG_INFO, "Xcoding Mode: %dx%d to %dx%d\n", input_video_width,
               input_video_height, output_video_width, output_video_height);

        if (src_codec_format == NI_CODEC_FORMAT_H264)
            p_stream_info = &SPS;
        else if (src_codec_format == NI_CODEC_FORMAT_H265)
            p_stream_info = &HEVC_SPS;
        else if (src_codec_format == NI_CODEC_FORMAT_VP9)
        {
            p_stream_info = &VP9_INFO;
            fps_num = VP9_INFO.timebase.den;
            fps_den = VP9_INFO.timebase.num;
        }

        if (multi_thread)
        {
            ni_pthread_t dec_send_tid, dec_recv_tid;
            dec_send_param_t dec_send_param = {0};
            dec_recv_param_t dec_recv_param = {0};

            ni_pthread_t enc_send_tid, enc_recv_tid;
            enc_send_param_t enc_send_param = {0};
            enc_recv_param_t enc_recv_param = {0};

            // init and create decoding thread
            dec_send_param.p_dec_ctx = &dec_ctx;
            dec_send_param.p_in_pkt = &in_pkt;
            dec_send_param.input_video_width = input_video_width;
            dec_send_param.input_video_height = input_video_height;
            dec_send_param.pkt_size = pkt_size;
            dec_send_param.print_time = 0;   // not support now
            dec_send_param.p_total_bytes_sent = &total_bytes_sent;
            dec_send_param.p_xcoder_state = &xcoder_state;
            dec_send_param.p_stream_info = p_stream_info;
            dec_recv_param.p_dec_ctx = &dec_ctx;
            dec_recv_param.p_out_frame = NULL;
            dec_recv_param.output_video_width = output_video_width;
            dec_recv_param.output_video_height = output_video_height;
            dec_recv_param.p_file = p_file_list[0];
            dec_recv_param.p_total_bytes_received = &total_bytes_received;
            dec_recv_param.p_xcoder_state = &xcoder_state;
            dec_recv_param.mode = mode;
            dec_recv_param.frame_list = &frame_list;

            if (ni_pthread_create(&dec_send_tid, NULL, decoder_send_thread,
                                  &dec_send_param))
            {
                ni_log(NI_LOG_ERROR,
                        "Error: create decoder send thread failed in transcode "
                        "mode\n");
                return -1;
            }
            if (ni_pthread_create(&dec_recv_tid, NULL, decoder_receive_thread,
                                  &dec_recv_param))
            {
                ni_log(NI_LOG_ERROR,
                        "Error: create decoder receive thread failed in "
                        "transcode mode\n");
                return -1;
            }

            // polling the first received decoded frame
            while (frame_list_is_empty(&frame_list) && !g_end_of_all_threads)
            {
                ni_usleep(100);
            }

            if (!g_end_of_all_threads)
            {
                p_ni_frame = &frame_list.frames[frame_list.head].data.frame;
                p_hwframe = dec_ctx.hw_action == NI_CODEC_HW_ENABLE ?
                            (niFrameSurface1_t *)p_ni_frame->p_data[3] : NULL;

                ret = encoder_open(&enc_ctx_list[0], p_enc_api_param_list,
                                   output_total, &encConfXcoderParamsList[0],
                                   &encConfXcoderGopList[0],
                                   p_ni_frame, arg_width, arg_height, fps_num,
                                   fps_den, bitrate, dst_codec_format,
                                   dec_ctx.pixel_format,
                                   p_ni_frame->aspect_ratio_idc, iXcoderGUID,
                                   p_hwframe, multi_thread,
                                   &enc_rate_emu_list[0], false);
                if (ret != 0)
                {
                    goto end;
                }

                // init and create encoding thread
                enc_send_param.p_enc_ctx = &enc_ctx_list[0];
                enc_send_param.p_in_frame = &enc_in_frame;
                enc_send_param.input_video_width = input_video_width;
                enc_send_param.input_video_height = input_video_height;
                enc_send_param.pfs = pfs;
                enc_send_param.yuv_buf = yuv_buf;
                enc_send_param.output_total = output_total;
                enc_send_param.p_total_bytes_sent = &total_bytes_sent;
                enc_send_param.p_xcoder_state = &xcoder_state_list[0];
                enc_send_param.mode = mode;
                enc_send_param.frame_list = &frame_list;
                enc_send_param.dec_codec_format = dec_ctx.codec_format;
                enc_send_param.sw_pix_fmt = NI_SW_PIX_FMT_NONE;
                enc_send_param.p_rate_emu = &enc_rate_emu_list[0];

                enc_recv_param.p_enc_ctx = &enc_ctx_list[0];
                enc_recv_param.p_out_packet = &out_packet;
                enc_recv_param.output_video_width = output_video_width;
                enc_recv_param.output_video_height = output_video_height;
                enc_recv_param.p_file = &p_file_list[0];
                enc_recv_param.output_total = output_total;
                enc_recv_param.p_number_of_packets = &number_of_packets_list[0];
                enc_recv_param.p_total_bytes_received = &total_bytes_received_list[0];
                enc_recv_param.p_xcoder_state = &xcoder_state_list[0];
                enc_recv_param.mode = mode;

                if (ni_pthread_create(&enc_send_tid, NULL, encoder_send_thread,
                                      &enc_send_param))
                {
                    ni_log(NI_LOG_ERROR, "Error: create encoder send thread failed "
                            "in transcode mode\n");
                    return -1;
                }
                if (ni_pthread_create(&enc_recv_tid, NULL, encoder_receive_thread,
                                      &enc_recv_param))
                {
                    ni_log(NI_LOG_ERROR, "Error: create encoder recieve thread "
                            "failed in transcode mode\n");
                    return -1;
                }

                ni_pthread_join(enc_send_tid, NULL);
                ni_pthread_join(enc_recv_tid, NULL);
            }
            ni_pthread_join(dec_send_tid, NULL);
            ni_pthread_join(dec_recv_tid, NULL);

            (void)ni_gettimeofday(&current_time, NULL);

            hwframe_list_release(&frame_list, pix_fmt);
        } else
        {
            int encoder_opened = 0;

            while (!end_of_all_streams &&
                (send_fin_flag == 0 || receive_fin_flag == 0))
            {
                (void)ni_gettimeofday(&current_time, NULL);
                print_time = need_time_print(&current_time, &previous_time);
                if (print_time)
                {
                    previous_time = current_time;
                }

                // bitstream Sending
            decode_send:
                send_fin_flag = decoder_send_data(&dec_ctx, &in_pkt,
                                                  input_video_width,
                                                  input_video_height, pkt_size,
                                                  &total_bytes_sent, print_time,
                                                  &xcoder_state, p_stream_info);
                if (send_fin_flag < 0)
                {
                    break;
                }

                // YUV Receiving: not writing to file
                receive_fin_flag = decoder_receive_data(
                    &dec_ctx, &out_frame, output_video_width,
                    output_video_height, p_file_list[0], &total_bytes_received,
                    print_time, 0, &xcoder_state, &rx_size);

                if (receive_fin_flag == NI_TEST_RETCODE_EAGAIN)
                {
                    ni_log(NI_LOG_DEBUG, 
                        "no decoder output, jump to encoder receive!\n");
                    if (!dec_ctx.hw_action)
                    {
                        ni_decoder_frame_buffer_free(&(out_frame.data.frame));
                    } else
                    {
                        ni_frame_buffer_free(&(out_frame.data.frame));
                    }

                    // use first encode config low delay flag for call flow
                    if (p_enc_api_param_list[0].low_delay_mode <= 0 &&
                        encoder_opened)
                    {
                        ni_log(NI_LOG_DEBUG,
                               "no decoder output, jump to encoder receive!\n");
                        goto encode_recv;
                    } else
                    {
                        ni_log(NI_LOG_DEBUG,
                               "no decoder output, encode low_delay, jump to "
                               "decoder send!\n");
                        goto decode_send;
                    }
                } else if (!encoder_opened)
                {
                    p_hwframe = dec_ctx.hw_action == NI_CODEC_HW_ENABLE ?
                     (niFrameSurface1_t *)out_frame.data.frame.p_data[3] : NULL;

                    ret = encoder_open(&enc_ctx_list[0], p_enc_api_param_list,
                                       output_total, &encConfXcoderParamsList[0],
                                       &encConfXcoderGopList[0],
                                       &out_frame.data.frame, arg_width,
                                       arg_height, fps_num, fps_den, bitrate,
                                       dst_codec_format, dec_ctx.pixel_format,
                                       out_frame.data.frame.aspect_ratio_idc,
                                       iXcoderGUID, p_hwframe, multi_thread,
                                       &enc_rate_emu_list[0], false);
                    if (ret != 0)
                    {
                        break;
                    }
                    encoder_opened = 1;
                }

                //encode_send
                for (i = 0; i < output_total; i++)
                {
                    // YUV Sending
                    send_fin_flag = encoder_send_data2(&enc_ctx_list[i],
                                                       &out_frame,
                                                       &enc_in_frame,
                                                       input_video_width,
                                                       input_video_height,
                                                       &total_bytes_sent,
                                                       &xcoder_state_list[i]);
                    if (send_fin_flag < 0)   //Error
                    {
                        break;
                    } else if (send_fin_flag == NI_TEST_RETCODE_EAGAIN)
                    {
                        // need to resend
                        continue;
                    }
                }

                // encoder send handling
                if (send_fin_flag < 0)
                {
                    break;
                } else if (enc_ctx_list[0].hw_action)
                {
                    p_hwframe = (niFrameSurface1_t *)out_frame.data.frame.p_data[3];
                    ni_hw_frame_ref(p_hwframe);
                    ni_frame_wipe_aux_data(&out_frame.data.frame);
                } else
                {
                    ni_decoder_frame_buffer_free(&out_frame.data.frame);
                }

            encode_recv:
                receive_fin_flag = encoder_receive(&enc_ctx_list[0],
                                                  &out_frame, &out_packet,
                                                  output_video_width,
                                                  output_video_height,
                                                  &number_of_packets_list[0],
                                                  output_total, &p_file_list[0],
                                                  &total_bytes_received_list[0],
                                                  print_time,
                                                  &xcoder_state_list[0]);
                for (i = 0; receive_fin_flag >= 0 && i < output_total; i++)
                {
                    if (!xcoder_state_list[i].enc_eos_received)
                    {
                        ni_log(NI_LOG_DEBUG, "enc %d continues to read!\n", i);
                        end_of_all_streams = 0;
                        break;
                    } else
                    {
                        ni_log(NI_LOG_DEBUG, "enc %d eos !\n", i);
                        end_of_all_streams = 1;
                    }
                }

                if (print_time)
                {
                    previous_time = current_time;
                }
            }
        }

        decoder_stat_report_and_close(&dec_ctx, total_bytes_received);

        encoder_stat_report_and_close(&enc_ctx_list[0], output_total,
                                      &number_of_packets_list[0],
                                      &total_bytes_received_list[0]);

        ni_rsrc_free_device_context(sdPara.p_dec_rsrc_ctx);
        rcPara.p_dec_rsrc_ctx = NULL;
        sdPara.p_dec_rsrc_ctx = NULL;

        ni_packet_buffer_free(&in_pkt.data.packet);
        ni_frame_buffer_free(&out_frame.data.frame);

        ni_rsrc_free_device_context(sdPara.p_enc_rsrc_ctx);
        rcPara.p_enc_rsrc_ctx = NULL;
        sdPara.p_enc_rsrc_ctx = NULL;

        ni_frame_buffer_free(&(enc_in_frame.data.frame));
        ni_packet_buffer_free(&(out_packet.data.packet));
    }

end:

/* // disable as the following can't pass clang-tidy
    if ((int32_t)dec_ctx.blk_io_handle != (int32_t)NI_INVALID_DEVICE_HANDLE && (int32_t)dec_ctx.device_handle !=(int32_t) NI_INVALID_DEVICE_HANDLE)
    {
        ni_log(NI_LOG_DEBUG, "xcoder app end - close decoder blk_io_handle %d device_handle %d\n", dec_ctx.blk_io_handle, dec_ctx.device_handle);
#ifdef _WIN32
        ni_device_close(dec_ctx.device_handle);
#elif __linux__
        ni_device_close(dec_ctx.device_handle);
        ni_device_close(dec_ctx.blk_io_handle);
#endif
    }
    
    if ((int32_t)enc_ctx.blk_io_handle != (int32_t)NI_INVALID_DEVICE_HANDLE && (int32_t)enc_ctx.device_handle != (int32_t)NI_INVALID_DEVICE_HANDLE)
    {
        ni_log(NI_LOG_DEBUG, "xcoder app end - close encoder blk_io_handle %d device_handle %d\n", enc_ctx.blk_io_handle, enc_ctx.device_handle);
#ifdef _WIN32
        ni_device_close(enc_ctx.device_handle);
#elif __linux__
        ni_device_close(enc_ctx.device_handle);
        ni_device_close(enc_ctx.blk_io_handle);
#endif
    }
    
    if ((int32_t)upl_ctx.blk_io_handle != (int32_t)NI_INVALID_DEVICE_HANDLE && (int32_t)upl_ctx.device_handle != (int32_t)NI_INVALID_DEVICE_HANDLE)
    {
        ni_log(NI_LOG_DEBUG, "xcoder app end - close uploader blk_io_handle %d device_handle %d\n", upl_ctx.blk_io_handle, upl_ctx.device_handle);
#ifdef _WIN32
        ni_device_close(upl_ctx.device_handle);
#elif __linux__
        ni_device_close(upl_ctx.device_handle);
        ni_device_close(upl_ctx.blk_io_handle);
#endif
    }
*/

    ni_device_session_context_clear(&dec_ctx);
    ni_device_session_context_clear(&upl_ctx);
    ni_device_session_context_clear(&sca_ctx);
    ni_device_session_context_clear(&crop_ctx);
    ni_device_session_context_clear(&pad_ctx);
    ni_device_session_context_clear(&ovly_ctx);
    ni_device_session_context_clear(&fmt_ctx);

    for (i = 0; i < output_total; i++)
    {
        ni_device_session_context_clear(&enc_ctx_list[i]);
        if (p_file_list[i] != NULL)
        {
            fclose(p_file_list[i]);
        }
    }

    for (input_index = 0; input_index < input_total; input_index++)
    {
        if (input_arg_pfs[input_index])
        {
            close(input_arg_pfs[input_index]);
        }
    }
    ni_aligned_free(yuv_buf);
    free(p_dec_api_param);
    free(p_enc_api_param_list);
    free(g_file_cache);
    g_file_cache = NULL;

    ni_log(NI_LOG_INFO, "All Done.\n");

    return ret;
}
