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
 *  \brief  Example code on how to programmatically work with NI Quadra using
 *          libxcoder API
 *
 ******************************************************************************/


#ifdef _WIN32
#include <io.h>
#include "ni_getopt.h"
#elif __linux__
#define _POSIX_C_SOURCE 200809L
#include <getopt.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/time.h>
#endif

#include <fcntl.h>
#include <errno.h>
#include "ni_device_api.h"
#include "ni_rsrc_api.h"
#include "ni_util.h"
#include "ni_device_test.h"
#include "ni_bitstream.h"

typedef struct _ni_err_rc_txt_entry
{
    ni_retcode_t rc;
    const char *txt;
} ni_err_rc_txt_entry_t;

static const ni_err_rc_txt_entry_t ni_err_rc_description[] = {
    NI_RETCODE_SUCCESS,
    "SUCCESS",
    NI_RETCODE_FAILURE,
    "FAILURE",
    NI_RETCODE_INVALID_PARAM,
    "INVALID_PARAM",
    NI_RETCODE_ERROR_MEM_ALOC,
    "ERROR_MEM_ALOC",
    NI_RETCODE_ERROR_NVME_CMD_FAILED,
    "ERROR_NVME_CMD_FAILED",
    NI_RETCODE_ERROR_INVALID_SESSION,
    "ERROR_INVALID_SESSION",
    NI_RETCODE_ERROR_RESOURCE_UNAVAILABLE,
    "ERROR_RESOURCE_UNAVAILABLE",
    NI_RETCODE_PARAM_INVALID_NAME,
    "PARAM_INVALID_NAME",
    NI_RETCODE_PARAM_INVALID_VALUE,
    "PARAM_INVALID_VALUE",
    NI_RETCODE_PARAM_ERROR_FRATE,
    "PARAM_ERROR_FRATE",
    NI_RETCODE_PARAM_ERROR_BRATE,
    "PARAM_ERROR_BRATE",
    NI_RETCODE_PARAM_ERROR_TRATE,
    "PARAM_ERROR_TRATE",
    NI_RETCODE_PARAM_ERROR_VBV_BUFFER_SIZE,
    "PARAM_ERROR_VBV_BUFFER_SIZE",
    NI_RETCODE_PARAM_ERROR_INTRA_PERIOD,
    "PARAM_ERROR_INTRA_PERIOD",
    NI_RETCODE_PARAM_ERROR_INTRA_QP,
    "PARAM_ERROR_INTRA_QP",
    NI_RETCODE_PARAM_ERROR_GOP_PRESET,
    "PARAM_ERROR_GOP_PRESET",
    NI_RETCODE_PARAM_ERROR_CU_SIZE_MODE,
    "PARAM_ERROR_CU_SIZE_MODE",
    NI_RETCODE_PARAM_ERROR_MX_NUM_MERGE,
    "PARAM_ERROR_MX_NUM_MERGE",
    NI_RETCODE_PARAM_ERROR_DY_MERGE_8X8_EN,
    "PARAM_ERROR_DY_MERGE_8X8_EN",
    NI_RETCODE_PARAM_ERROR_DY_MERGE_16X16_EN,
    "PARAM_ERROR_DY_MERGE_16X16_EN",
    NI_RETCODE_PARAM_ERROR_DY_MERGE_32X32_EN,
    "PARAM_ERROR_DY_MERGE_32X32_EN",
    NI_RETCODE_PARAM_ERROR_CU_LVL_RC_EN,
    "PARAM_ERROR_CU_LVL_RC_EN",
    NI_RETCODE_PARAM_ERROR_HVS_QP_EN,
    "PARAM_ERROR_HVS_QP_EN",
    NI_RETCODE_PARAM_ERROR_HVS_QP_SCL,
    "PARAM_ERROR_HVS_QP_SCL",
    NI_RETCODE_PARAM_ERROR_MN_QP,
    "PARAM_ERROR_MN_QP",
    NI_RETCODE_PARAM_ERROR_MX_QP,
    "PARAM_ERROR_MX_QP",
    NI_RETCODE_PARAM_ERROR_MX_DELTA_QP,
    "PARAM_ERROR_MX_DELTA_QP",
    NI_RETCODE_PARAM_ERROR_CONF_WIN_TOP,
    "PARAM_ERROR_CONF_WIN_TOP",
    NI_RETCODE_PARAM_ERROR_CONF_WIN_BOT,
    "PARAM_ERROR_CONF_WIN_BOT",
    NI_RETCODE_PARAM_ERROR_CONF_WIN_L,
    "PARAM_ERROR_CONF_WIN_L",
    NI_RETCODE_PARAM_ERROR_CONF_WIN_R,
    "PARAM_ERROR_CONF_WIN_R",
    NI_RETCODE_PARAM_ERROR_USR_RMD_ENC_PARAM,
    "PARAM_ERROR_USR_RMD_ENC_PARAM",
    NI_RETCODE_PARAM_ERROR_BRATE_LT_TRATE,
    "PARAM_ERROR_BRATE_LT_TRATE",
    NI_RETCODE_PARAM_ERROR_RCENABLE,
    "PARAM_ERROR_RCENABLE",
    NI_RETCODE_PARAM_ERROR_MAXNUMMERGE,
    "PARAM_ERROR_MAXNUMMERGE",
    NI_RETCODE_PARAM_ERROR_CUSTOM_GOP,
    "PARAM_ERROR_CUSTOM_GOP",
    NI_RETCODE_PARAM_ERROR_PIC_WIDTH,
    "PARAM_ERROR_PIC_WIDTH",
    NI_RETCODE_PARAM_ERROR_PIC_HEIGHT,
    "PARAM_ERROR_PIC_HEIGHT",
    NI_RETCODE_PARAM_ERROR_DECODING_REFRESH_TYPE,
    "PARAM_ERROR_DECODING_REFRESH_TYPE",
    NI_RETCODE_PARAM_ERROR_CUSIZE_MODE_8X8_EN,
    "PARAM_ERROR_CUSIZE_MODE_8X8_EN",
    NI_RETCODE_PARAM_ERROR_CUSIZE_MODE_16X16_EN,
    "PARAM_ERROR_CUSIZE_MODE_16X16_EN",
    NI_RETCODE_PARAM_ERROR_CUSIZE_MODE_32X32_EN,
    "PARAM_ERROR_CUSIZE_MODE_32X32_EN",
    NI_RETCODE_PARAM_ERROR_TOO_BIG,
    "PARAM_ERROR_TOO_BIG",
    NI_RETCODE_PARAM_ERROR_TOO_SMALL,
    "PARAM_ERROR_TOO_SMALL",
    NI_RETCODE_PARAM_ERROR_ZERO,
    "PARAM_ERROR_ZERO",
    NI_RETCODE_PARAM_ERROR_OOR,
    "PARAM_ERROR_OOR",
    NI_RETCODE_PARAM_ERROR_WIDTH_TOO_BIG,
    "PARAM_ERROR_WIDTH_TOO_BIG",
    NI_RETCODE_PARAM_ERROR_WIDTH_TOO_SMALL,
    "PARAM_ERROR_WIDTH_TOO_SMALL",
    NI_RETCODE_PARAM_ERROR_HEIGHT_TOO_BIG,
    "PARAM_ERROR_HEIGHT_TOO_BIG",
    NI_RETCODE_PARAM_ERROR_HEIGHT_TOO_SMALL,
    "PARAM_ERROR_HEIGHT_TOO_SMALL",
    NI_RETCODE_PARAM_ERROR_AREA_TOO_BIG,
    "PARAM_ERROR_AREA_TOO_BIG",
    NI_RETCODE_ERROR_EXCEED_MAX_NUM_SESSIONS,
    "ERROR_EXCEED_MAX_NUM_SESSIONS",
    NI_RETCODE_ERROR_GET_DEVICE_POOL,
    "ERROR_GET_DEVICE_POOL",
    NI_RETCODE_ERROR_LOCK_DOWN_DEVICE,
    "ERROR_LOCK_DOWN_DEVICE",
    NI_RETCODE_ERROR_UNLOCK_DEVICE,
    "ERROR_UNLOCK_DEVICE",
    NI_RETCODE_ERROR_OPEN_DEVICE,
    "ERROR_OPEN_DEVICE",
    NI_RETCODE_ERROR_INVALID_HANDLE,
    "ERROR_INVALID_HANDLE",
    NI_RETCODE_ERROR_INVALID_ALLOCATION_METHOD,
    "ERROR_INVALID_ALLOCATION_METHOD",
    NI_RETCODE_ERROR_VPU_RECOVERY,
    "ERROR_VPU_RECOVERY",

    NI_RETCODE_PARAM_WARNING_DEPRECATED,
    "PARAM_WARNING_DEPRECATED",
    NI_RETCODE_PARAM_ERROR_LOOK_AHEAD_DEPTH,
    "PARAM_ERROR_LOOK_AHEAD_DEPTH",
    NI_RETCODE_PARAM_ERROR_FILLER,
    "PARAM_ERROR_FILLER",
    NI_RETCODE_PARAM_ERROR_PICSKIP,
    "PARAM_ERROR_PICSKIP",

    NI_RETCODE_PARAM_WARN,
    "PARAM_WARN",

    NI_RETCODE_NVME_SC_WRITE_BUFFER_FULL,
    "NVME_SC_WRITE_BUFFER_FULL",
    NI_RETCODE_NVME_SC_RESOURCE_UNAVAILABLE,
    "NVME_SC_RESOURCE_UNAVAILABLE",
    NI_RETCODE_NVME_SC_RESOURCE_IS_EMPTY,
    "NVME_SC_RESOURCE_IS_EMPTY",
    NI_RETCODE_NVME_SC_RESOURCE_NOT_FOUND,
    "NVME_SC_RESOURCE_NOT_FOUND",
    NI_RETCODE_NVME_SC_REQUEST_NOT_COMPLETED,
    "NVME_SC_REQUEST_NOT_COMPLETED",
    NI_RETCODE_NVME_SC_REQUEST_IN_PROGRESS,
    "NVME_SC_REQUEST_IN_PROGRESS",
    NI_RETCODE_NVME_SC_INVALID_PARAMETER,
    "NVME_SC_INVALID_PARAMETER",
    NI_RETCODE_NVME_SC_VPU_RECOVERY,
    "NVME_SC_VPU_RECOVERY",
    NI_RETCODE_NVME_SC_VPU_RSRC_INSUFFICIENT,
    "NVME_SC_VPU_RSRC_INSUFFICIENT",
    NI_RETCODE_NVME_SC_VPU_GENERAL_ERROR,
    "NVME_SC_VPU_GENERAL_ERROR",
};

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
    uint8_t chroma_qp_table
        [2]
        [QP_MAX_NUM +
         1];   ///< pre-scaled (with chroma_qp_index_offset) version of qp_table
    int chroma_qp_diff;
    uint8_t data[4096];
    size_t data_size;

    uint32_t dequant4_buffer[6][QP_MAX_NUM + 1][16];
    uint32_t dequant8_buffer[6][QP_MAX_NUM + 1][64];
    uint32_t (*dequant4_coeff[6])[16];
    uint32_t (*dequant8_coeff[6])[64];
} ni_h264_pps_t;

#define NI_MAX_BUFFERED_FRAME 45

typedef struct _ni_test_frame_list
{
    ni_session_data_io_t ni_test_frame[NI_MAX_BUFFERED_FRAME];
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
    device_state_t *p_xcodeState;
    ni_h264_sps_t *p_SPS;
} dec_send_param_t;

typedef struct dec_recv_param
{
    ni_session_context_t *p_dec_ctx;
    ni_session_data_io_t *p_out_frame;
    int output_video_width;
    int output_video_height;
    FILE *p_file;
    unsigned long long *p_total_bytes_received;
    device_state_t *p_xcodeState;
    int mode;
    ni_test_frame_list_t *test_frame_list;
} dec_recv_param_t;

typedef struct enc_send_param
{
    ni_session_context_t *p_enc_ctx;
    ni_session_data_io_t *p_in_frame;
    int input_video_width;
    int input_video_height;
    int pfs;
    unsigned long *p_total_bytes_sent;
    device_state_t *p_xcodeState;
    int mode;
    ni_test_frame_list_t *test_frame_list;
    uint32_t dec_codec_format;   // used in transcode mode
    int *p_input_exhausted;      // used in upload mode
    niFrameSurface1_t *p_hwframe_pool_tracker;   // used in upload mode
} enc_send_param_t;

typedef struct enc_recv_param
{
    ni_session_context_t *p_enc_ctx;
    ni_session_data_io_t *p_out_packet;
    int output_video_width;
    int output_video_height;
    FILE *p_file;
    unsigned long long *p_total_bytes_received;
    device_state_t *p_xcodeState;
    int mode;
    niFrameSurface1_t *p_hwframe_pool_tracker;   // used in upload mode
} enc_recv_param_t;

typedef struct uploader_param
{
    ni_session_context_t *p_upl_ctx;
    ni_session_data_io_t *p_swin_frame;
    int input_video_width;
    int input_video_height;
    int pfs;
    unsigned long *p_total_bytes_sent;
    int *p_input_exhausted;
    int pool_size;
    ni_test_frame_list_t *test_frame_list;
} uploader_param_t;

volatile int send_fin_flag = 0, receive_fin_flag = 0, err_flag = 0;
volatile unsigned int number_of_frames = 0;
volatile unsigned int number_of_packets = 0;
struct timeval start_time, previous_time, current_time;
time_t start_timestamp = 0, privious_timestamp = 0, current_timestamp = 0;

// max YUV frame size
#define MAX_YUV_FRAME_SIZE (7680 * 4320 * 3 / 2)

static uint8_t *g_file_cache = NULL;
static uint8_t *g_curr_cache_pos = NULL;
// a counter for reconfigFile line entry index
static int g_reconfigCount = 0;

volatile unsigned int total_file_size = 0;
volatile unsigned int data_left_size = 0;
volatile int g_repeat = 1;

static const char *ni_get_rc_txt(ni_retcode_t rc)
{
    int i;
    for (i = 0;
         i < sizeof(ni_err_rc_description) / sizeof(ni_err_rc_txt_entry_t); i++)
    {
        if (rc == ni_err_rc_description[i].rc)
        {
            return ni_err_rc_description[i].txt;
        }
    }
    return "rc not supported";
}

void arg_error_exit(char *arg_name, char *param)
{
    fprintf(stderr, "Error: unrecognized argument for %s, \"%s\"\n", arg_name,
            param);
    exit(-1);
}

static inline bool test_frames_isempty(ni_test_frame_list_t *list)
{
    return (list->head == list->tail);
}

static inline bool test_frames_isfull(ni_test_frame_list_t *list)
{
    return (list->head == ((list->tail + 1) % NI_MAX_BUFFERED_FRAME));
}

static inline int test_frames_length(ni_test_frame_list_t *list)
{
    return ((list->tail - list->head + NI_MAX_BUFFERED_FRAME) %
            NI_MAX_BUFFERED_FRAME);
}

static inline int enq_test_frames(ni_test_frame_list_t *list)
{
    if (test_frames_isfull(list))
    {
        return -1;
    }
    list->tail = (list->tail + 1) % NI_MAX_BUFFERED_FRAME;
    return 0;
}

static inline int deq_test_frames(ni_test_frame_list_t *list)
{
    if (test_frames_isempty(list))
    {
        return -1;
    }
    list->head = (list->head + 1) % NI_MAX_BUFFERED_FRAME;
    return 0;
}

//Applies only to hwframe where recycling HW frame to FW is needed
//Loop through unsent frames to set in tracking list for cleanup
static inline void drain_test_list(enc_send_param_t *p_enc_send_param,
                                   ni_test_frame_list_t *list)
{
    ni_session_data_io_t *p_temp_frame = NULL;
    ni_frame_t *p_ni_temp_frame = NULL;
    uint16_t current_hwframe_index;
    if (p_enc_send_param->p_enc_ctx->hw_action)
    {
        //store the unsent frames in the tracker
        //to be cleared out by scan at end
        while (!test_frames_isempty(list))
        {
            p_temp_frame = &list->ni_test_frame[list->head];
            p_ni_temp_frame = &p_temp_frame->data.frame;
            current_hwframe_index =
                ((niFrameSurface1_t *)((uint8_t *)p_ni_temp_frame->p_data[3]))
                    ->ui16FrameIdx;
            memcpy(p_enc_send_param->p_hwframe_pool_tracker +
                       current_hwframe_index,
                   (uint8_t *)p_ni_temp_frame->p_data[3],
                   sizeof(niFrameSurface1_t));
            deq_test_frames(list);
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
            to_copy = data_left_size;
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
uint32_t read_next_chunk_from_file(int pfs, uint8_t *p_dst, uint32_t to_read)
{
    uint8_t *tmp_dst = p_dst;
    ni_log(NI_LOG_DEBUG, 
        "read_next_chunk_from_file:p_dst %p len %u totalSize %u left %u\n",
        tmp_dst, to_read, total_file_size, data_left_size);
    unsigned int to_copy = to_read;
    unsigned long tmpFileSize = to_read;
    if (data_left_size == 0)
    {
        if (g_repeat > 1)
        {
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
        fprintf(stderr, "Error: reading file, quit! left-to-read %lu\n",
                tmpFileSize);
        fprintf(stderr, "Error: input file read error\n");
        return -1;
    }
    data_left_size -= one_read_size;

    return to_copy;
}

// current position of the input data buffer
static uint32_t curr_nal_start = 0;
static uint32_t curr_found_pos = 0;

// reset input data buffer position to the start
void reset_data_buf_pos(void)
{
    curr_nal_start = 0;
    curr_found_pos = 0;
}

// rewind input data buffer position by a number of bytes, if possible
void rewind_data_buf_pos_by(uint32_t nb_bytes)
{
    if (curr_found_pos > nb_bytes)
    {
        curr_found_pos -= nb_bytes;
    } else
    {
        ni_log(NI_LOG_ERROR, "Error %s %d bytes!\n", __func__, nb_bytes);
    }
}

// find/copy next H.264 NAL unit (including start code) and its type;
// return NAL data size if found, 0 otherwise
uint32_t find_h264_next_nalu(uint8_t *p_dst, int *nal_type)
{
    uint32_t data_size;
    uint32_t i = curr_found_pos;

    if (i + 3 >= total_file_size)
    {
        ni_log(NI_LOG_DEBUG,
               "%s reaching end, curr_pos %d "
               "total input size %u\n",
               __func__, curr_found_pos, total_file_size);

        if (g_repeat > 1)
        {
            g_repeat--;
            ni_log(NI_LOG_DEBUG, "input processed, %d left\n", g_repeat);
            reset_data_buf_pos();
        }
        return 0;
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
    curr_nal_start = i;

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
            curr_found_pos = (int)total_file_size;
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

/*!*****************************************************************************
 *  \brief  Send decoder input data
 *
 *  \param  
 *
 *  \return
 ******************************************************************************/
ni_retcode_t decoder_send_data(ni_session_context_t *p_dec_ctx,
                               ni_session_data_io_t *p_in_data, int sos_flag,
                               int input_video_width, int input_video_height,
                               int pkt_size, unsigned int file_size,
                               unsigned long *total_bytes_sent, int print_time,
                               device_state_t *p_device_state,
                               ni_h264_sps_t *sps)
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

    // TBD Demo: decoder flush at 200th packet
#if 0
    if (200 == p_dec_ctx->pkt_num)
    {
        if (NI_RETCODE_SUCCESS != ni_device_dec_session_flush(p_dec_ctx))
        {
            ni_log(NI_LOG_ERROR, "decoder_send_data: mid-flush failed!\n");
            exit(-1);
        }
    }
#endif

    if (0 == p_in_pkt->data_len)
    {
        memset(p_in_pkt, 0, sizeof(ni_packet_t));

        if (NI_CODEC_FORMAT_H264 == p_dec_ctx->codec_format)
        {
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

    p_in_pkt->start_of_stream = sos_flag;
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
            // todo save offset
        }
        p_in_pkt->data_len = send_size;

        p_in_pkt->end_of_stream = 1;
        printf("Sending p_last packet (size %u) + eos\n", p_in_pkt->data_len);
    } else
    {
        if (new_packet)
        {
            send_size =
                ni_packet_copy(p_in_pkt->p_data, tmp_buf, frame_pkt_size,
                               p_dec_ctx->p_leftover, &p_dec_ctx->prev_size);
            // todo: update offset with send_size
            // p_in_pkt->data_len is the actual packet size to be sent to decoder
            p_in_pkt->data_len += saved_prev_size;
        }
    }

    tx_size =
        ni_device_session_write(p_dec_ctx, p_in_data, NI_DEVICE_TYPE_DECODER);

    if (tx_size < 0)
    {
        // Error
        fprintf(stderr, "Error: sending data error. rc:%d\n", tx_size);
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
            //printf("Sent %d < %d , re-try next time ?\n", tx_size, send_size);
        }
    }

    *total_bytes_sent += tx_size;

    if (p_dec_ctx->ready_to_close)
    {
        p_device_state->dec_eos_sent = 1;
    }

    if (print_time)
    {
        printf("decoder_send_data: success, total sent: %lu\n",
               *total_bytes_sent);
    }

    if (tx_size > 0)
    {
        ni_log(NI_LOG_DEBUG, "decoder_send_data: reset packet_buffer.\n");
        ni_packet_buffer_free(p_in_pkt);
    }

#if 0
    bytes_sent += chunk_size;
    printf("[W] %d percent %d bytes sent. rc:%d result:%d\n", bytes_sent*100/file_size, chunk_size, rc, result);
    sos_flag = 0;
    if (NI_RETCODE_NVME_SC_WRITE_BUFFER_FULL == rc)
    {
        printf("Buffer Full.\n");
    }
    else if (rc != 0)
    {
        // Error
        fprintf(stderr, "Error: sending data error. rc:%d result:%d.\n", rc, result);
        err_flag = 1;
        return 2;
    }
#endif

    retval = NI_RETCODE_SUCCESS;

    END;

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
                         device_state_t *p_device_state)
{
    int rc = NI_RETCODE_FAILURE;
    int end_flag = 0;
    int rx_size = 0;
    bool b_is_hwframe = p_dec_ctx->hw_action;
    ni_frame_t *p_out_frame = &(p_out_data->data.frame);
    int width, height;
    // for now read only planar data from decoder; ToDO: to read non-planar data
    int is_planar = 1;

    ni_log(NI_LOG_DEBUG, "===> decoder_receive_data hwframe %d <===\n", b_is_hwframe);

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

    end_flag = p_out_frame->end_of_stream;

    if (rx_size < 0)
    {
        fprintf(stderr, "Error: receiving data error. rc:%d\n", rx_size);
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
        for (i = 0; i < 3; i++)
        {
            uint8_t *src = p_out_frame->p_data[i];
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
                    fprintf(stderr,
                            "Error: writing data plane %d: height %d error!\n",
                            i, plane_height);
                    fprintf(stderr, "Error: ferror rc = %d\n", ferror(p_file));
                }
                src += plane_width;
            }
        }
        if (fflush(p_file))
        {
            fprintf(stderr,
                    "Error: writing data frame flush failed! errno %d\n",
                    errno);
        }
    }

    *total_bytes_received += rx_size;

    if (print_time)
    {
        printf("[R] Got:%d  Frames= %u  fps=%f  Total bytes %llu\n", rx_size,
               number_of_frames,
               ((float)number_of_frames /
                (float)(current_time.tv_sec - start_time.tv_sec)),
               (unsigned long long)*total_bytes_received);
    }

    if (end_flag)
    {
        printf("Decoder Receiving done.\n");
        p_device_state->dec_eos_received = 1;
        rc = 1;
    } else if (0 == rx_size)
    {
        rc = 2;
    }

    ni_log(NI_LOG_DEBUG, "decoder_receive_data: success\n");

    END;

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
                ni_log(NI_LOG_DEBUG, "%s(): frame #%lu reconfig BR by frame %d\n",
                               __func__, p_enc_ctx->frame_num,
                               api_param->reconf_hash[g_reconfigCount][1]);

                g_reconfigCount++;
            }
            break;
        /*
        case XCODER_TEST_RECONF_INTRAPRD:
            if (p_enc_ctx->frame_num ==
                api_param->reconf_hash[g_reconfigCount][0])
            {
                p_enc_ctx->enc_change_params->enable_option |=
                    NI_SET_CHANGE_PARAM_INTRA_PARAM;
                p_enc_ctx->enc_change_params->intraQP =
                    api_param->reconf_hash[g_reconfigCount][1];
                p_enc_ctx->enc_change_params->intraPeriod =
                    api_param->reconf_hash[g_reconfigCount][2];
                p_enc_ctx->enc_change_params->repeatHeaders =
                    api_param->reconf_hash[g_reconfigCount][3];
                ni_log(NI_LOG_DEBUG, 
                    "%s(): frame #%lu reconf intraQP %d intraPeriod %d "
                    "repeatHeaders %d\n",
                    __func__, p_enc_ctx->frame_num,
                    p_enc_ctx->enc_change_params->intraQP,
                    p_enc_ctx->enc_change_params->intraPeriod,
                    p_enc_ctx->enc_change_params->repeatHeaders);

                // frame reconf_len needs to be set here
                frame->reconf_len = sizeof(ni_encoder_change_params_t);
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

                // frame reconf_len needs to be set here
                frame->reconf_len = sizeof(ni_encoder_change_params_t);
                g_reconfigCount++;
            }
            break;
        */
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
        /*
        case XCODER_TEST_RECONF_RC_MIN_MAX_QP:
            if (p_enc_ctx->frame_num ==
                api_param->reconf_hash[g_reconfigCount][0])
            {
                p_enc_ctx->enc_change_params->enable_option |=
                    NI_SET_CHANGE_PARAM_RC_MIN_MAX_QP;
                p_enc_ctx->enc_change_params->minQpI =
                    api_param->reconf_hash[g_reconfigCount][1];
                p_enc_ctx->enc_change_params->maxQpI =
                    api_param->reconf_hash[g_reconfigCount][2];
                p_enc_ctx->enc_change_params->minQpPB =
                    api_param->reconf_hash[g_reconfigCount][3];
                p_enc_ctx->enc_change_params->maxQpPB =
                    api_param->reconf_hash[g_reconfigCount][4];

                // frame reconf_len needs to be set here
                frame->reconf_len = sizeof(ni_encoder_change_params_t);
                g_reconfigCount++;
            }
            break;
        */
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
                    "%s(): frame #%lu reconfig framerate by frame (%d/%d)\n",
                    __func__, p_enc_ctx->frame_num, framerate->framerate_num,
                    framerate->framerate_denom);

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
        case XCODER_TEST_RECONF_OFF:
        default:;
    }
}

/*!*****************************************************************************
 *  \brief Read from input file, send to uploader, retrieve HW descriptor
 *
 *  \param  
 *
 *  \return
 ******************************************************************************/
int upload_send_data_get_desc(
    ni_session_context_t *p_upl_ctx,
    ni_session_data_io_t *p_swin_data,   //intermediate for swf
    ni_session_data_io_t *p_in_data, int input_video_width,
    int input_video_height, int pfs, unsigned int file_size,
    unsigned long *bytes_sent, int *input_exhausted)
{
    static uint8_t tmp_buf[MAX_YUV_FRAME_SIZE];
    //volatile static int started = 0;
    //volatile static int need_to_resend = 0;
    int frame_size = input_video_width * input_video_height * 3 *
        p_upl_ctx->bit_depth_factor / 2;
    uint32_t chunk_size;
    int retval;
    ni_frame_t *p_in_frame = &(p_in_data->data.frame);       //hwframe
    ni_frame_t *p_swin_frame = &(p_swin_data->data.frame);   //swframe

    niFrameSurface1_t *dst_surf;

    ni_log(NI_LOG_DEBUG, "===> upload_send_data <===\n");

    chunk_size = read_next_chunk_from_file(pfs, tmp_buf, frame_size);
    if (chunk_size == -1)
    {
        fprintf(stderr, "Error: could not read file!");
        return -1;
    }

    p_in_frame->start_of_stream = 0;

    p_in_frame->end_of_stream = 0;
    p_in_frame->force_key_frame = 0;
    if (chunk_size == 0)
    {
        p_in_frame->end_of_stream = 1;
        ni_log(NI_LOG_DEBUG, "upload_send_data: read chunk size 0, eos!\n");
        *input_exhausted = 1;
    }
    p_in_frame->video_width = p_swin_frame->video_width = input_video_width;
    p_in_frame->video_height = p_swin_frame->video_height = input_video_height;

    // only metadata header for now
    p_in_frame->extra_data_len = p_swin_frame->extra_data_len =
        NI_APP_ENC_FRAME_META_DATA_SIZE;

    int dst_stride[NI_MAX_NUM_DATA_POINTERS] = {0};
    int dst_height_aligned[NI_MAX_NUM_DATA_POINTERS] = {0};
    //bool alignment_2pass_wa = 0;   // default 2pass disabled; ToDo
    int is_nv12 = 0;   // default NOT NV12 frame; ToDo
    ni_get_hw_yuv420p_dim(input_video_width, input_video_height,
                          p_upl_ctx->bit_depth_factor, is_nv12, dst_stride,
                          dst_height_aligned);

    ni_frame_buffer_alloc_pixfmt(
        p_swin_frame, /*todo semi planar format*/
        (p_upl_ctx->bit_depth_factor == 1 ? NI_PIX_FMT_YUV420P :
                                            NI_PIX_FMT_YUV420P10LE),
        input_video_width, input_video_height, dst_stride,
        p_upl_ctx->codec_format == NI_CODEC_FORMAT_H264,
        (int)(p_in_frame->extra_data_len));
    if (!p_swin_frame->p_data[0])
    {
        fprintf(stderr, "Error: could not allocate YUV frame buffer!");
        return -1;
    }

    // alloc dest avframe buff
    //can also be ni_frame_buffer_alloc()
    ni_frame_buffer_alloc_hwenc(p_in_frame, input_video_width,
                                input_video_height,
                                (int)p_in_frame->extra_data_len);
    if (!p_in_frame->p_data[3])
    {
        fprintf(stderr, "Error: could not allocate hw frame buffer!");
        return -1;
    }

    dst_surf = (niFrameSurface1_t *)p_in_frame->p_data[3];

    ni_log(NI_LOG_DEBUG, "p_dst alloc linesize = %d/%d/%d  src height=%d  "
                   "dst height aligned = %d/%d/%d\n",
                   dst_stride[0], dst_stride[1], dst_stride[2],
                   input_video_height, dst_height_aligned[0],
                   dst_height_aligned[1], dst_height_aligned[2]);

    uint8_t *p_src[NI_MAX_NUM_DATA_POINTERS];
    int src_stride[NI_MAX_NUM_DATA_POINTERS];
    int src_height[NI_MAX_NUM_DATA_POINTERS];

    src_stride[0] = input_video_width * p_upl_ctx->bit_depth_factor;
    src_stride[1] = src_stride[2] = src_stride[0] / 2;

    src_height[0] = input_video_height;
    src_height[1] = src_height[0] / 2;
    src_height[2] = src_height[1];   // ToDo: (is_nv12 ? 0 : src_height[1]);

    p_src[0] = tmp_buf;
    p_src[1] = tmp_buf + src_stride[0] * src_height[0];
    p_src[2] = p_src[1] + src_stride[1] * src_height[1];

    ni_copy_hw_yuv420p((uint8_t **)(p_swin_frame->p_data), p_src,
                       input_video_width, input_video_height,
                       p_upl_ctx->bit_depth_factor, is_nv12, 0, dst_stride,
                       dst_height_aligned, src_stride, src_height);

    retval = ni_device_session_hwup(p_upl_ctx, p_swin_data, dst_surf);
    if (retval < 0)
    {
        fprintf(stderr, "Error: failed ni_device_session_hwup():%d\n", retval);
        return -1;
    } else
    {
        *bytes_sent += p_swin_frame->data_len[0] + p_swin_frame->data_len[1] +
            p_swin_frame->data_len[2] + p_swin_frame->data_len[3];
        ni_log(NI_LOG_DEBUG, "upload_send_data: total sent data size=%lu\n",
                       *bytes_sent);

        /* TODO Should be within libxcoderAPI*/
        dst_surf->ui16width = input_video_width;
        dst_surf->ui16height = input_video_height;
        dst_surf->ui32nodeAddress = 0;   // always 0 offset for upload
        dst_surf->encoding_type = (is_nv12) ?
            NI_PIXEL_PLANAR_FORMAT_SEMIPLANAR :
            NI_PIXEL_PLANAR_FORMAT_PLANAR;
        /*  */
        number_of_frames++;

        ni_log(NI_LOG_DEBUG, "upload_send_data: FID = %d success, number:%u\n",
                       dst_surf->ui16FrameIdx, number_of_frames);
    }

    return 0;
}

/*!*****************************************************************************
 *  \brief  Send encoder input data, read from input file
 *
 *  \param  
 *
 *  \return
 ******************************************************************************/
int encoder_send_data(ni_session_context_t *p_enc_ctx,
                      ni_session_data_io_t *p_in_data, int sos_flag,
                      int input_video_width, int input_video_height, int pfs,
                      unsigned int file_size, unsigned long *bytes_sent,
                      device_state_t *p_device_state)
{
    static uint8_t tmp_buf[MAX_YUV_FRAME_SIZE];
    volatile static int started = 0;
    volatile static int need_to_resend = 0;
    int frame_size = input_video_width * input_video_height * 3 *
        p_enc_ctx->bit_depth_factor / 2;
    uint32_t chunk_size;
    int oneSent;
    ni_frame_t *p_in_frame = &(p_in_data->data.frame);
    // employ a ni_frame_t as a data holder for encode info that is to be sent
    // to encoder; the encode info comes from either decoded frame (e.g. close
    // caption, HDR/HDR+), or encode configuration specified by xcoder-params
    ni_frame_t dec_frame = {0};
    ni_xcoder_params_t *p_param =
        (ni_xcoder_params_t *)p_enc_ctx->p_session_config;

    ni_log(NI_LOG_DEBUG, "===> encoder_send_data <===\n");

    if (p_device_state->enc_eos_sent == 1)
    {
        ni_log(NI_LOG_DEBUG, "encoder_send_data: ALL data (incl. eos) sent "
                       "already!\n");
        return 0;
    }

    if (need_to_resend)
    {
        goto send_frame;
    }

    p_in_frame->start_of_stream = 0;
    if (!started)
    {
        started = 1;
        p_in_frame->start_of_stream = 1;
    }
    p_in_frame->end_of_stream = 0;
    p_in_frame->force_key_frame = 0;

    p_in_frame->video_width = input_video_width;
    p_in_frame->video_height = input_video_height;

    // reset encoder change data buffer
    memset(p_enc_ctx->enc_change_params, 0, sizeof(ni_encoder_change_params_t));

    // extra data starts with metadata header, and reset various aux data size
    p_in_frame->extra_data_len = NI_APP_ENC_FRAME_META_DATA_SIZE;
    p_in_frame->roi_len = 0;
    p_in_frame->reconf_len = 0;
    p_in_frame->sei_total_len = 0;

    // collect encode reconfig and demo info and save them in the data holder
    // struct, to be used in the aux data prep and copy later
    prep_reconf_demo_data(p_enc_ctx, &dec_frame);

    int dst_stride[NI_MAX_NUM_DATA_POINTERS] = {0};
    int dst_height_aligned[NI_MAX_NUM_DATA_POINTERS] = {0};
    bool alignment_2pass_wa = 0;   // default 2pass disabled; ToDo
    int is_nv12 = 0;               // default NOT NV12 frame; ToDo
    ni_get_hw_yuv420p_dim(input_video_width, input_video_height,
                          p_enc_ctx->bit_depth_factor, is_nv12, dst_stride,
                          dst_height_aligned);

    // ROI demo mode takes higher priority over aux data
    // Note: when ROI demo modes enabled, supply ROI map for the specified range
    //       frames, and 0 map for others
    if (p_param->roi_demo_mode && p_param->cfg_enc_params.roi_enable)
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

    // frame type is unknown in encode only case, so supply a dummy one
    int should_send_sei_with_frame = ni_should_send_sei_with_frame(
        p_enc_ctx, PIC_TYPE_P,
        (ni_xcoder_params_t *)p_enc_ctx->p_session_config);

    // encode only case does not have the following SEI support for now;
    // data buffer for various SEI: HDR mastering display color volume, HDR
    // content light level, close caption, User data unregistered, HDR10+ etc.
    uint8_t mdcv_data[NI_MAX_SEI_DATA];
    uint8_t cll_data[NI_MAX_SEI_DATA];
    uint8_t cc_data[NI_MAX_SEI_DATA];
    uint8_t udu_data[NI_MAX_SEI_DATA];
    uint8_t hdrp_data[NI_MAX_SEI_DATA];

    // but still have to prep for auxiliary data (including those generated by
    // reconf demo encode info)
    ni_enc_prep_aux_data(p_enc_ctx, p_in_frame, &dec_frame,
                         p_enc_ctx->codec_format, should_send_sei_with_frame,
                         mdcv_data, cll_data, cc_data, udu_data, hdrp_data);

    p_in_frame->extra_data_len += p_in_frame->sei_total_len;

    // data layout requirement: leave space for reconfig data if at least one of
    // reconfig, SEI or ROI is present
    // Note: ROI is present when enabled, so use encode config flag instead of
    //       frame's roi_len as it can be 0 indicating a 0'd ROI map setting !
    if (p_in_frame->reconf_len || p_in_frame->sei_total_len ||
        (p_param->roi_demo_mode && p_param->cfg_enc_params.roi_enable))
    {
        p_in_frame->extra_data_len += sizeof(ni_encoder_change_params_t);
    }

    ni_encoder_frame_buffer_alloc(
        p_in_frame, input_video_width, dst_height_aligned[0], dst_stride,
        p_enc_ctx->codec_format == NI_CODEC_FORMAT_H264,
        (int)(p_in_frame->extra_data_len), alignment_2pass_wa);
    if (!p_in_frame->p_data[0])
    {
        fprintf(stderr, "Error: could not allocate YUV frame buffer!");
        return -1;
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
        p_in_frame->reconf_len, p_in_frame->roi_len, p_in_frame->force_pic_qp,
        p_in_frame->sei_user_data_unreg_len,
        p_in_frame->use_cur_src_as_long_term_pic,
        p_in_frame->use_long_term_ref);

    uint8_t *p_src[NI_MAX_NUM_DATA_POINTERS];
    int src_stride[NI_MAX_NUM_DATA_POINTERS];
    int src_height[NI_MAX_NUM_DATA_POINTERS];

    src_stride[0] = input_video_width * p_enc_ctx->bit_depth_factor;
    src_stride[1] = src_stride[2] = src_stride[0] / 2;

    src_height[0] = input_video_height;
    src_height[1] = src_height[0] / 2;
    src_height[2] = src_height[1];   // ToDo: (is_nv12 ? 0 : src_height[1]);

    p_src[0] = tmp_buf;
    p_src[1] = tmp_buf + src_stride[0] * src_height[0];
    p_src[2] = p_src[1] + src_stride[1] * src_height[1];

    uint8_t plane_idx;
    bool need_copy = 0;
    for (plane_idx = 0; plane_idx < NI_MAX_NUM_DATA_POINTERS - 1; plane_idx++)
    {
        if ((src_stride[plane_idx] != dst_stride[plane_idx]) ||
            (dst_height_aligned[plane_idx] != src_height[plane_idx]))
        {
            ni_log(NI_LOG_TRACE,
                   "encoder_send_data: plane_idx %d src_stride %d dst_stride "
                   "%d dst_height_aligned %d src_height %d need_copy 1\n",
                   plane_idx, src_stride[plane_idx], dst_stride[plane_idx],
                   dst_height_aligned[plane_idx], src_height[plane_idx]);
            need_copy = 1;
            break;
        }
    }
    if (((ni_xcoder_params_t *)p_enc_ctx->p_session_config)
            ->cfg_enc_params.conf_win_right)
    {
        ni_log(NI_LOG_TRACE,
               "encoder_send_data: conf_win_right %d need_copy 1\n",
               ((ni_xcoder_params_t *)p_enc_ctx->p_session_config)
                   ->cfg_enc_params.conf_win_right);
        need_copy = 1;
    }

    ni_log(NI_LOG_TRACE, "encoder_send_data: need_copy %d\n", need_copy);

    if (!need_copy)
    {
        // zero copy
        chunk_size = read_next_chunk_from_file(
            pfs, (uint8_t *)p_in_frame->p_data[0], frame_size);
        //ni_log(NI_LOG_TRACE,
        //    "encoder_send_data: p_data %p frame_size %d chunk_size %d\n",
        //    (uint8_t *)p_in_frame->p_data[0], frame_size, chunk_size);
        if (chunk_size == -1)
        {
            fprintf(stderr, "Error: could not read file!");
            return -1;
        }
    } else
    {
        chunk_size = read_next_chunk_from_file(pfs, tmp_buf, frame_size);
        //ni_log(NI_LOG_TRACE,
        //    "encoder_send_data: tmp_buf %p frame_size %d chunk_size %d\n",
        //    tmp_buf, frame_size, chunk_size);
        if (chunk_size == -1)
        {
            fprintf(stderr, "Error: could not read file!");
            return -1;
        }

        // YUV part of the encoder input data layout
        ni_copy_hw_yuv420p(
            (uint8_t **)(p_in_frame->p_data), p_src, input_video_width,
            input_video_height, p_enc_ctx->bit_depth_factor, is_nv12,
            ((ni_xcoder_params_t *)p_enc_ctx->p_session_config)
                ->cfg_enc_params.conf_win_right,
            dst_stride, dst_height_aligned, src_stride, src_height);
    }

    if (chunk_size == 0)
    {
        p_in_frame->end_of_stream = 1;
        ni_log(NI_LOG_DEBUG, "encoder_send_data: read chunk size 0, eos!\n");
    }

    // auxiliary data part of the encoder input data layout
    ni_enc_copy_aux_data(p_enc_ctx, p_in_frame, &dec_frame,
                         p_enc_ctx->codec_format, mdcv_data, cll_data, cc_data,
                         udu_data, hdrp_data, 0 /*swframe*/, is_nv12);

    // clean up data storage
    ni_frame_buffer_free(&dec_frame);

send_frame:
    oneSent =
        ni_device_session_write(p_enc_ctx, p_in_data, NI_DEVICE_TYPE_ENCODER);
    if (oneSent < 0)
    {
        fprintf(stderr,
                "Error: failed ni_device_session_write() for encoder\n");
        need_to_resend = 1;
        return -1;
    } else if (oneSent == 0 && !p_enc_ctx->ready_to_close)
    {
        need_to_resend = 1;
    } else
    {
        need_to_resend = 0;

        *bytes_sent += p_in_frame->data_len[0] + p_in_frame->data_len[1] +
            p_in_frame->data_len[2] + p_in_frame->data_len[3];
        ni_log(NI_LOG_DEBUG, "encoder_send_data: total sent data size=%lu\n",
                       *bytes_sent);

        ni_log(NI_LOG_DEBUG, "encoder_send_data: success\n");

        if (p_enc_ctx->ready_to_close)
        {
            p_device_state->enc_eos_sent = 1;
        }
    }

    return 0;
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
                       uint32_t dec_codec_format,
                       ni_session_data_io_t *p_dec_out_data,
                       ni_session_data_io_t *p_enc_in_data, int sos_flag,
                       int input_video_width, int input_video_height, int pfs,
                       unsigned int file_size, unsigned long *bytes_sent,
                       device_state_t *p_device_state)
{
    volatile static int started = 0;
    volatile static int need_to_resend_2 = 0;
    int oneSent;
    // pointer to data struct to be sent
    ni_session_data_io_t *p_to_send = NULL;
    // frame pointer to data frame struct to be sent
    ni_frame_t *p_in_frame = NULL;
    ni_xcoder_params_t *api_params =
        (ni_xcoder_params_t *)p_enc_ctx->p_session_config;
    int is_nv12 = 0;   // default NOT NV12 frame; ToDo
    int is_hwframe = p_enc_ctx->hw_action != NI_CODEC_HW_NONE;

    ni_log(NI_LOG_DEBUG, "===> encoder_send_data2 <===\n");

    if (p_device_state->enc_eos_sent == 1)
    {
        ni_log(NI_LOG_DEBUG, "encoder_send_data2: ALL data (incl. eos) sent "
                       "already!\n");
        return 1;
    }

    if (need_to_resend_2)
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
        bool alignment_2pass_wa = 0;   // default 2pass disabled; ToDo
        ni_get_hw_yuv420p_dim(input_video_width, input_video_height,
                              p_enc_ctx->bit_depth_factor, is_nv12, dst_stride,
                              dst_height_aligned);

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
            //ToDo add NV12 frame support
            ni_encoder_frame_buffer_alloc(
                p_in_frame, input_video_width, input_video_height, dst_stride,
                p_enc_ctx->codec_format == NI_CODEC_FORMAT_H264,
                (int)(p_in_frame->extra_data_len), alignment_2pass_wa);
            if (!p_in_frame->p_data[0])
            {
                fprintf(stderr, "Error: cannot allocate YUV frame buffer!");
                return -1;
            }
        } else
        {
            ni_frame_buffer_alloc_hwenc(p_in_frame, input_video_width,
                                        input_video_height,
                                        (int)(p_in_frame->extra_data_len));
            if (!p_in_frame->p_data[3])
            {
                fprintf(stderr, "Error: cannot allocate YUV frame buffer!");
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
        src_stride[3] = 0;

        p_src[0] = p_dec_out_data->data.frame.p_data[0];
        p_src[1] = p_dec_out_data->data.frame.p_data[1];
        p_src[2] = p_dec_out_data->data.frame.p_data[2];
        p_src[3] = p_dec_out_data->data.frame.p_data[3];

        if (!is_hwframe)
        {   // YUV part of the encoder input data layout
            ni_copy_hw_yuv420p(
                (uint8_t **)(p_in_frame->p_data), p_src, input_video_width,
                input_video_height, p_enc_ctx->bit_depth_factor, is_nv12,
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
                             cc_data, udu_data, hdrp_data, is_hwframe, is_nv12);
    }

    p_in_frame->video_width = input_video_width;
    p_in_frame->video_height = input_video_height;

    p_in_frame->start_of_stream = 0;
    if (!started)
    {
        started = 1;
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
        fprintf(stderr, "Error: encoder_send_data2\n");
        need_to_resend_2 = 1;
        return -1;
    } else if (oneSent == 0)
    {
        if (p_device_state->enc_eos_sent == 0 && p_enc_ctx->ready_to_close)
        {
            need_to_resend_2 = 0;
            p_device_state->enc_eos_sent = 1;
        } else
        {
            need_to_resend_2 = 1;
            return 2;
        }
    } else
    {
        need_to_resend_2 = 0;

        if (p_enc_ctx->ready_to_close)
        {
            p_device_state->enc_eos_sent = 1;
        }
#if 0
        *bytes_sent += p_in_frame->data_len[0] + p_in_frame->data_len[1] + p_in_frame->data_len[2]
            + p_in_frame->data_len[3];
        ni_log(NI_LOG_DEBUG, "encoder_send_data2: total sent data size=%u\n", *bytes_sent);
#endif
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
                       ni_session_data_io_t *p_in_data, int sos_flag,
                       int input_video_width, int input_video_height,
                       device_state_t *p_device_state, int input_exhausted,
                       int *need_to_resend)
{
    volatile static int started = 0;
    //*need_to_resend = 0; //bring this outside and have upload check it too

    int oneSent;
    ni_frame_t *p_in_frame = &(p_in_data->data.frame);

    ni_log(NI_LOG_DEBUG, "===> encoder_send_data3 <===\n");

    if (p_device_state->enc_eos_sent == 1)
    {
        ni_log(NI_LOG_DEBUG, "encoder_send_data3: ALL data (incl. eos) sent "
                       "already!\n");
        return 0;
    }

    if (*need_to_resend)
    {
        goto send_frame;
    }

    p_in_frame->start_of_stream = 0;
    if (!started)
    {
        started = 1;
        p_in_frame->start_of_stream = 1;
    }
    p_in_frame->end_of_stream = 0;
    p_in_frame->force_key_frame = 0;
    if (input_exhausted)
    {
        p_in_frame->end_of_stream = 1;
        ni_log(NI_LOG_DEBUG, "encoder_send_data3: read chunk size 0, eos!\n");
    }
    p_in_frame->video_width = input_video_width;
    p_in_frame->video_height = input_video_height;

    // only metadata header for now
    p_in_frame->extra_data_len = NI_APP_ENC_FRAME_META_DATA_SIZE;

send_frame:
    oneSent =
        ni_device_session_write(p_enc_ctx, p_in_data, NI_DEVICE_TYPE_ENCODER);
    if (oneSent < 0)
    {
        fprintf(stderr,
                "Error: failed ni_device_session_write() for encoder\n");
        *need_to_resend = 1;
        return -1;
    } else if (oneSent == 0 && !p_enc_ctx->ready_to_close)
    {
        *need_to_resend = 1;
        ni_log(NI_LOG_DEBUG, "NEEDED TO RESEND");
    } else
    {
        *need_to_resend = 0;

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
*  \brief  Simple scan to Recycle remaining memory bins left around
*
*  \param p_pool_tracker - start address of hwdesc pool
*
*  \return
******************************************************************************/
int scan_and_clean_hwdescriptors(niFrameSurface1_t *p_pool_tracker)
{
    int i;
    int recycled = 0;
    for (i = 0; i < NI_MAX_HWDESC_FRAME_INDEX; i++)
    {
        if (p_pool_tracker[i].ui16FrameIdx)
        {
            ni_hwframe_buffer_recycle(&p_pool_tracker[i],
                                      p_pool_tracker[i].device_handle);
            //zero for tracking purposes
            p_pool_tracker[i].ui16FrameIdx = 0;
            recycled++;
        }
    }
    return recycled;
}

/*!*****************************************************************************
 *  \brief  Receive output data from encoder
 *
 *  \param  
 *
 *  \return 0 - success got packet
 *          1 - received eos
 *          2 - got nothing, need retry
 *          -1 - failure
 ******************************************************************************/
int encoder_receive_data(ni_session_context_t *p_enc_ctx,
                         ni_session_data_io_t *p_out_data,
                         int output_video_width, int output_video_height,
                         FILE *p_file, unsigned long long *total_bytes_received,
                         int print_time)
{
    int packet_size = NI_MAX_TX_SZ;
    int rc = 0;
    int end_flag = 0;
    int rx_size = 0;
    ni_packet_t *p_out_pkt = &(p_out_data->data.packet);
    int meta_size = NI_FW_ENC_BITSTREAM_META_DATA_SIZE;

    ni_log(NI_LOG_DEBUG, "===> encoder_receive_data <===\n");

    if (NI_INVALID_SESSION_ID == p_enc_ctx->session_id ||
        NI_INVALID_DEVICE_HANDLE == p_enc_ctx->blk_io_handle)
    {
        ni_log(NI_LOG_DEBUG, "encode session not opened yet, return\n");
        return 0;
    }

    rc = ni_packet_buffer_alloc(p_out_pkt, packet_size);
    if (rc != NI_RETCODE_SUCCESS)
    {
        fprintf(stderr, "Error: malloc packet failed, ret = %d!\n", rc);
        return -1;
    }

receive_data:
    rc = ni_device_session_read(p_enc_ctx, p_out_data, NI_DEVICE_TYPE_ENCODER);

    end_flag = p_out_pkt->end_of_stream;
    rx_size = rc;

    ni_log(NI_LOG_DEBUG, "encoder_receive_data: received data size=%d\n", rx_size);

    if (rx_size > meta_size)
    {
        if (p_file &&
            (fwrite((uint8_t *)p_out_pkt->p_data + meta_size,
                    p_out_pkt->data_len - meta_size, 1, p_file) != 1))
        {
            fprintf(stderr, "Error: writing data %u bytes error!\n",
                    p_out_pkt->data_len - meta_size);
            fprintf(stderr, "Error: ferror rc = %d\n", ferror(p_file));
        }

        *total_bytes_received += rx_size - meta_size;

        if (0 == p_enc_ctx->pkt_num)
        {
            p_enc_ctx->pkt_num = 1;
            ni_log(NI_LOG_DEBUG, "got encoded stream header, keep reading ..\n");
            goto receive_data;
        }
        number_of_packets++;

        ni_log(NI_LOG_DEBUG, "Got:   Packets= %u\n", number_of_packets);
    } else if (rx_size != 0)
    {
        fprintf(stderr, "Error: received %d bytes, <= metadata size %d!\n",
                rx_size, meta_size);
        return -1;
    } else if (!end_flag &&
               ((ni_xcoder_params_t *)(p_enc_ctx->p_session_config))
                   ->low_delay_mode)
    {
        ni_log(NI_LOG_DEBUG, "low delay mode and NO pkt, keep reading ..\n");
        goto receive_data;
    }

    if (print_time)
    {
        int timeDiff = (int)(current_time.tv_sec - start_time.tv_sec);
        if (timeDiff == 0)
        {
            timeDiff = 1;
        }
        printf("[R] Got:%d   Packets= %u fps=%f  Total bytes %llu\n", rx_size,
               number_of_packets,
               ((float)number_of_packets) / ((float)timeDiff),
               *total_bytes_received);
    }

    if (end_flag)
    {
        printf("Encoder Receiving done.\n");
        return 1;
    } else if (0 == rx_size)
    {
        return 2;
    }

    ni_log(NI_LOG_DEBUG, "encoder_receive_data: success\n");

    return 0;
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
    mbWidth =
        ((p_param->source_width + max_cu_size - 1) & (~(max_cu_size - 1))) /
        roiMapBlockUnitSize;
    mbHeight =
        ((p_param->source_height + max_cu_size - 1) & (~(max_cu_size - 1))) /
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
int encoder_open_session(
    ni_session_context_t *p_enc_ctx, int dst_codec_format, int iXcoderGUID,
    ni_xcoder_params_t *p_enc_params, int src_bit_depth, int width, int height,
    ni_hrd_params_t *hrd_params, ni_color_primaries_t color_primaries,
    ni_color_transfer_characteristic_t color_trc, ni_color_space_t color_space,
    int video_full_range_flag, int sar_num, int sar_den, int is_planar)
{
    int ret = 0;

    p_enc_ctx->p_session_config = p_enc_params;
    p_enc_ctx->session_id = NI_INVALID_SESSION_ID;
    p_enc_ctx->codec_format = dst_codec_format;

    // assign the card GUID in the encoder context and let session open
    // take care of the rest
    p_enc_ctx->device_handle = NI_INVALID_DEVICE_HANDLE;
    p_enc_ctx->blk_io_handle = NI_INVALID_DEVICE_HANDLE;
    p_enc_ctx->hw_id = iXcoderGUID;

    // default: little endian
    p_enc_ctx->src_bit_depth = src_bit_depth;
    p_enc_ctx->src_endian = NI_FRAME_LITTLE_ENDIAN;
    p_enc_ctx->bit_depth_factor = 1;
    if (10 == p_enc_ctx->src_bit_depth)
    {
        p_enc_ctx->bit_depth_factor = 2;
    }

    int linesize_aligned = width;
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
    p_enc_params->source_width = linesize_aligned;

    int height_aligned = height;
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
    p_enc_params->source_height = height_aligned;

    // VUI setting including color setting is done by specifying them in the
    // encoder config
    p_enc_params->cfg_enc_params.colorDescPresent = 0;
    // ToDo: when to enable
    if (color_primaries != 0)
    {
        p_enc_params->cfg_enc_params.colorDescPresent = 1;
    }
    p_enc_params->cfg_enc_params.colorPrimaries = color_primaries;
    p_enc_params->cfg_enc_params.colorTrc = color_trc;
    p_enc_params->cfg_enc_params.colorSpace = color_space;
    p_enc_params->cfg_enc_params.videoFullRange = video_full_range_flag;
    p_enc_params->cfg_enc_params.aspectRatioWidth = sar_num;
    p_enc_params->cfg_enc_params.aspectRatioHeight = sar_den;

    // default planar encoder input data; ToDo: HW frames
    p_enc_params->cfg_enc_params.planar = is_planar;

    ret = ni_device_session_open(p_enc_ctx, NI_DEVICE_TYPE_ENCODER);
    if (ret < 0)
    {
        fprintf(stderr, "Error: encoder_open_session failure!\n");
    } else
    {
        printf("Encoder device %d session open successful.\n", iXcoderGUID);
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

    if (ret < 0)
    {
        fprintf(stderr, "Error: ni_decoder_session_open() failure!\n");
        return -1;
    } else
    {
        printf("Decoder device %d session open successful.\n", iXcoderGUID);
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
                          int src_bit_depth, int width, int height,
                          int is_planar, int pool_size)
{
    int ret = 0;
    p_upl_ctx->session_id = NI_INVALID_SESSION_ID;

    // assign the card GUID in the encoder context and let session open
    // take care of the rest
    p_upl_ctx->device_handle = NI_INVALID_DEVICE_HANDLE;
    p_upl_ctx->blk_io_handle = NI_INVALID_DEVICE_HANDLE;
    p_upl_ctx->hw_id = iXcoderGUID;

    // default: little endian
    p_upl_ctx->src_bit_depth = src_bit_depth;
    p_upl_ctx->src_endian = NI_FRAME_LITTLE_ENDIAN;
    p_upl_ctx->bit_depth_factor = 1;
    p_upl_ctx->pixel_format =
        (is_planar ? NI_PIX_FMT_YUV420P : NI_PIX_FMT_NV12);
    if (10 == p_upl_ctx->src_bit_depth)
    {
        p_upl_ctx->bit_depth_factor = 2;
        p_upl_ctx->src_bit_depth = 10;
        p_upl_ctx->pixel_format =
            (is_planar ? NI_PIX_FMT_YUV420P10LE : NI_PIX_FMT_P010LE);
    } else if (32 == p_upl_ctx->src_bit_depth)
    {
        //rgba, not currently supported in this tool
        fprintf(stderr, "Error: uploader rgba is toDO!\n");
        return -1;
    }
    p_upl_ctx->active_video_width = width;
    p_upl_ctx->active_video_height = height;

    ret = ni_device_session_open(p_upl_ctx, NI_DEVICE_TYPE_UPLOAD);
    if (ret < 0)
    {
        fprintf(stderr, "Error: uploader_open_session failure!\n");
        return ret;
    } else
    {
        printf("Uploader device %d session open successful.\n", iXcoderGUID);
    }
    ret = ni_device_session_init_framepool(p_upl_ctx, pool_size, 0);
    if (ret < 0)
    {
        fprintf(stderr, "Error: uploader_config_session failure!\n");
    } else
    {
        printf("Uploader device %d configured successful.\n", iXcoderGUID);
    }
    return ret;
}

void print_usage(void)
{
    printf("Video decoder/encoder/transcoder application directly using Netint "
           "Libxcoder API v%s\n"
           "Usage: xcoder [options]\n"
           "\n"
           "options:\n"
           "-h | --help             Show help.\n"
           "-v | --version          Print version info.\n"
           "-t | --multi-thread     Enable multi-threaded sending and receiving"
           " in all modes.\n"
           "-l | --loglevel         Set loglevel of libxcoder API.\n"
           "                        [none, fatal, error, info, debug, trace]\n"
           "                        (Default: info)\n"
           "-c | --card             Set card index to use.\n"
           "                        See `ni_rsrc_mon` for cards on system.\n"
           "                        (Default: 0)\n"
           "-i | --input            Input file path.\n"
           "-r | --repeat           (Positive integer) to Repeat input X \n"
           "                        times for performance test.(Default: 1)\n"
           "-s | --size             Resolution of input file in format "
           "WIDTHxHEIGHT.\n"
           "                        (eg. '1920x1080')\n"
           "-m | --mode             Input to output codec processing mode in "
           "format:\n"
           "                        InType2OutType. [a2y, h2y, y2a, u2a, y2h, "
           "u2h, a2a, "
           "a2h, h2a, h2h]\n"
           "                        Type notation: y=YUV420P a=AVC, h=HEVC, "
           "u=hwupload yuv\n"
           "-b | --bitdepth         Input and output bit depth. [8, 10]\n"
           "                        (Default: 8)\n"
           "-x | --xcoder-params    Encoding params. See \"Encoding "
           "Parameters\" chapter in\n"
           "                        "
           "QuadraIntegration&ProgrammingGuide*.pdf for help.\n"
           "                        (Default: \"\")\n"
           "-d | --decoder-params   Decoding params. See \"Decoding "
           "Parameters\" chapter in\n"
           "                        "
           "QuadraIntegration&ProgrammingGuide*.pdf for help.\n"
           "                        (Default: \"\")\n"
           "-o | --output           Output file path.\n",
           NI_XCODER_REVISION);
}

// retrieve key and value from 'key=value' pair, return 0 if successful
// otherwise non-0
static int get_key_value(char *p_str, char *key, char *value)
{
    if (!p_str || !key || !value)
    {
        return 1;
    }

    char *p = strchr(p_str, '=');
    if (!p)
    {
        return 1;
    } else
    {
        *p = '\0';
        key[0] = '\0';
        value[0] = '\0';
        strcpy(key, p_str);
        strcpy(value, p + 1);
        return 0;
    }
}

// retrieve config parameter valus from --xcoder-params,
// return 0 if successful, -1 otherwise
static int retrieve_xcoder_params(char xcoderParams[],
                                  ni_xcoder_params_t *params,
                                  ni_session_context_t *ctx)
{
    char key[64], value[64];
    char *p = xcoderParams;
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
            get_key_value(curr, key, value))
        {
            fprintf(stderr,
                    "Error: xcoder-params p_config key/value not "
                    "retrieved: %s\n",
                    curr);
            ret = -1;
            break;
        }
        ret = ni_encoder_params_set_value(params, key, value);
        switch (ret)
        {
            case NI_RETCODE_PARAM_INVALID_NAME:
                fprintf(stderr, "Error: unknown option: %s.\n", key);
                break;
            case NI_RETCODE_PARAM_INVALID_VALUE:
                fprintf(stderr, "Error: invalid value for %s: %s.\n", key,
                        value);
                break;
            default:
                break;
        }

        if (NI_RETCODE_SUCCESS != ret)
        {
            fprintf(stderr, "Error: config parsing failed %d: %s\n", ret,
                    ni_get_rc_txt(ret));
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
    ctx->keep_alive_timeout = params->cfg_enc_params.keep_alive_timeout;

    return ret;
}

// retrieve config parameter valus from --decoder-params,
// return 0 if successful, -1 otherwise
static int retrieve_decoder_params(char xcoderParams[],
                                   ni_xcoder_params_t *params,
                                   ni_session_context_t *ctx)
{
    char key[64], value[64];
    char *p = xcoderParams;
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
            get_key_value(curr, key, value))
        {
            fprintf(stderr,
                    "Error: decoder-params p_config key/value not "
                    "retrieved: %s\n",
                    curr);
            ret = -1;
            break;
        }
        ret = ni_decoder_params_set_value(params, key, value);
        switch (ret)
        {
            case NI_RETCODE_PARAM_INVALID_NAME:
                fprintf(stderr, "Error: unknown option: %s.\n", key);
                break;
            case NI_RETCODE_PARAM_INVALID_VALUE:
                fprintf(stderr, "Error: invalid value for %s: %s.\n", key,
                        value);
                break;
            default:
                break;
        }

        if (NI_RETCODE_SUCCESS != ret)
        {
            fprintf(stderr, "Error: config parsing failed %d: %s\n", ret,
                    ni_get_rc_txt(ret));
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
    ctx->keep_alive_timeout = params->dec_input_params.keep_alive_timeout;
    
    return ret;
}

void *decoder_send_thread(void *args)
{
    dec_send_param_t *p_dec_send_param = args;
    ni_session_context_t *p_dec_ctx = p_dec_send_param->p_dec_ctx;
    int sos_flag = 1;
    int retval;

    printf("decoder_send_thread start\n");
    for (;;)
    {
        retval = decoder_send_data(
            p_dec_ctx, p_dec_send_param->p_in_pkt, sos_flag,
            p_dec_send_param->input_video_width,
            p_dec_send_param->input_video_height, p_dec_send_param->pkt_size,
            total_file_size, p_dec_send_param->p_total_bytes_sent,
            p_dec_send_param->print_time, p_dec_send_param->p_xcodeState,
            p_dec_send_param->p_SPS);
        sos_flag = 0;
        if (retval < 0)   // Error
        {
            fprintf(stderr, "Error: decoder_send_thread break!\n");
            break;
        } else if (p_dec_send_param->p_xcodeState->dec_eos_sent)   //eos
        {
            printf("decoder_send_thread reach eos\n");
            break;
        }
    }

    ni_log(NI_LOG_TRACE, "decoder_send_thread exit\n");
    return NULL;
}

void *decoder_receive_thread(void *args)
{
    dec_recv_param_t *p_dec_recv_param = args;
    ni_test_frame_list_t *frame_list = p_dec_recv_param->test_frame_list;
    ni_session_data_io_t *p_out_frame = NULL;
    ni_frame_t *p_ni_frame = NULL;
    int retval;
    int print_time;
    struct timeval previous_time_l = previous_time;

    printf("decoder_receive_thread start\n");
    for (;;)
    {
        if (p_dec_recv_param->mode == XCODER_APP_DECODE)
        {
            (void)ni_gettimeofday(&current_time, NULL);
            print_time = ((current_time.tv_sec - previous_time.tv_sec) > 1);
            p_ni_frame = &p_dec_recv_param->p_out_frame->data.frame;
            retval = decoder_receive_data(
                p_dec_recv_param->p_dec_ctx, p_dec_recv_param->p_out_frame,
                p_dec_recv_param->output_video_width,
                p_dec_recv_param->output_video_height, p_dec_recv_param->p_file,
                p_dec_recv_param->p_total_bytes_received, print_time,
                1,   // save to file
                p_dec_recv_param->p_xcodeState);
            ni_decoder_frame_buffer_free(p_ni_frame);

            if (retval < 0)   // Error
            {
                fprintf(
                    stderr,
                    "Error: decoder_receive_thread break in decode mode!\n");
                break;
            } else if (p_ni_frame->end_of_stream)   //eos
            {
                printf("decoder_receive_thread reach eos\n");
                break;
            } else if (retval == 2)
            {
                ni_usleep(100);
            }
            if (print_time)
            {
                previous_time = current_time;
            }
        } else
        {
            while (test_frames_isfull(frame_list))
            {
                ni_usleep(100);
            }
            print_time = ((current_time.tv_sec - previous_time_l.tv_sec) > 1);
            p_out_frame = &frame_list->ni_test_frame[frame_list->tail];
            p_ni_frame = &p_out_frame->data.frame;
            retval = decoder_receive_data(
                p_dec_recv_param->p_dec_ctx, p_out_frame,
                p_dec_recv_param->output_video_width,
                p_dec_recv_param->output_video_height, p_dec_recv_param->p_file,
                p_dec_recv_param->p_total_bytes_received, print_time,
                0,   // do not save to file
                p_dec_recv_param->p_xcodeState);
            if (retval < 0)   // Error
            {
                if (!p_dec_recv_param->p_dec_ctx->hw_action)
                {
                    ni_decoder_frame_buffer_free(p_ni_frame);
                } else
                {
                    ni_frame_buffer_free(p_ni_frame);
                }
                fprintf(
                    stderr,
                    "Error: decoder_receive_thread break in transcode mode!\n");
                break;
            } else if (p_ni_frame->end_of_stream)   //eos
            {
                enq_test_frames(frame_list);
                printf("decoder_receive_thread reach eos\n");
                break;
            } else if (retval == 2)   // eagain
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
                enq_test_frames(frame_list);
            }
            if (print_time)
            {
                // current_time will be updated in transcode mode
                previous_time_l = current_time;
            }
        }
    }

    ni_log(NI_LOG_TRACE, "decoder_receive_thread exit\n");
    return NULL;
}

void *encoder_send_thread(void *args)
{
    enc_send_param_t *p_enc_send_param = args;
    ni_session_context_t *p_enc_ctx = p_enc_send_param->p_enc_ctx;
    ni_test_frame_list_t *frame_list = p_enc_send_param->test_frame_list;
    ni_session_data_io_t *p_dec_frame = NULL;
    ni_session_data_io_t *p_upl_frame = NULL;
    ni_frame_t *p_ni_frame = NULL;
    int need_to_resend = 0;
    int input_exhausted = 0;
    int sos_flag = 1;
    int retval;

    printf("encoder_send_thread start\n");
    for (;;)
    {
        if (p_enc_send_param->mode == XCODER_APP_ENCODE)
        {
            retval = encoder_send_data(
                p_enc_ctx, p_enc_send_param->p_in_frame, sos_flag,
                p_enc_send_param->input_video_width,
                p_enc_send_param->input_video_height, p_enc_send_param->pfs,
                total_file_size, p_enc_send_param->p_total_bytes_sent,
                p_enc_send_param->p_xcodeState);
            sos_flag = 0;
            if (retval < 0)   // Error
            {
                fprintf(stderr,
                        "Error: encoder_send_thread break in encode mode!\n");
                break;
            } else if (p_enc_send_param->p_xcodeState->enc_eos_sent)   //eos
            {
                ni_log(NI_LOG_DEBUG, 
                    "encoder_send_thread reach eos in encode mode!\n");
                break;
            }
        } else if (p_enc_send_param->mode == XCODER_APP_TRANSCODE)
        {
            while (test_frames_isempty(frame_list))
            {
                ni_usleep(100);
            }
            p_dec_frame = &frame_list->ni_test_frame[frame_list->head];
            p_ni_frame = &p_dec_frame->data.frame;
            retval = encoder_send_data2(
                p_enc_ctx, p_enc_send_param->dec_codec_format, p_dec_frame,
                p_enc_send_param->p_in_frame, sos_flag,
                p_enc_send_param->input_video_width,
                p_enc_send_param->input_video_height, p_enc_send_param->pfs,
                total_file_size, p_enc_send_param->p_total_bytes_sent,
                p_enc_send_param->p_xcodeState);
            sos_flag = 0;
            if (retval == 0)
            {
                if (p_enc_send_param->p_enc_ctx->hw_action)
                {
                    // skip copy the frame when eos received.
                    if (!p_enc_send_param->p_xcodeState->enc_eos_sent)
                    {
                        //track in array with unique index, free when enc read finds
                        //this must be implemented in application space for complete
                        //tracking of hwframes
                        //If encoder write  had no buffer avail space the next time we
                        //update the tracker will be redundant
                        uint16_t current_hwframe_index =
                            ((niFrameSurface1_t *)((uint8_t *)
                                                       p_ni_frame->p_data[3]))
                                ->ui16FrameIdx;
                        ni_log(NI_LOG_DEBUG, 
                            "send frame index:%d, head:%d, eos:%d\n",
                            current_hwframe_index, frame_list->head,
                            p_enc_send_param->p_xcodeState->enc_eos_sent);
                        memcpy(p_enc_send_param->p_hwframe_pool_tracker +
                                   current_hwframe_index,
                               (uint8_t *)p_ni_frame->p_data[3],
                               sizeof(niFrameSurface1_t));
                    }
                    ni_frame_wipe_aux_data(p_ni_frame);   //reusebuff
                } else
                {
                    ni_decoder_frame_buffer_free(p_ni_frame);
                }
                deq_test_frames(frame_list);
            } else if (retval < 0)   //Error
            {
                if (p_enc_send_param->p_enc_ctx->hw_action)
                {
                    //pre close cleanup will clear it out
                    uint16_t current_hwframe_index =
                        ((niFrameSurface1_t *)((uint8_t *)
                                                   p_ni_frame->p_data[3]))
                            ->ui16FrameIdx;
                    memcpy(p_enc_send_param->p_hwframe_pool_tracker +
                               current_hwframe_index,
                           (uint8_t *)p_ni_frame->p_data[3],
                           sizeof(niFrameSurface1_t));
                } else
                {
                    ni_decoder_frame_buffer_free(p_ni_frame);
                }
                fprintf(
                    stderr,
                    "Error: encoder_send_thread break in transcode mode!\n");
                break;
            }
            if (p_enc_send_param->p_xcodeState->enc_eos_sent)   // eos
            {
                printf("encoder_send_thread reach eos in transcode mode!\n");
                break;
            }
        } else
        {
            while (test_frames_isempty(frame_list))
            {
                ni_usleep(100);
            }
            p_upl_frame = &frame_list->ni_test_frame[frame_list->head];
            if (*p_enc_send_param->p_input_exhausted &&
                p_upl_frame->data.frame.end_of_stream)
            {
                input_exhausted = 1;
            }
            retval = encoder_send_data3(p_enc_send_param->p_enc_ctx,
                                        p_upl_frame, sos_flag,
                                        p_enc_send_param->input_video_width,
                                        p_enc_send_param->input_video_height,
                                        p_enc_send_param->p_xcodeState,
                                        input_exhausted, &need_to_resend);

            sos_flag = 0;
            if (retval == 2)   //Error
            {
                fprintf(
                    stderr,
                    "Error: encoder_send_thread break in transcode mode!\n");
                break;
            }

            if (need_to_resend == 0)
            {
                //successful read means there is recycle to check
                uint16_t current_hwframe_index =
                    ((niFrameSurface1_t *)((uint8_t *)p_upl_frame->data.frame
                                               .p_data[3]))
                        ->ui16FrameIdx;
                ni_log(NI_LOG_DEBUG, "send frame index:%d, head:%d\n",
                               current_hwframe_index, frame_list->head);
                memcpy((p_enc_send_param->p_hwframe_pool_tracker +
                        current_hwframe_index),
                       (uint8_t *)p_upl_frame->data.frame.p_data[3],
                       sizeof(niFrameSurface1_t));
                deq_test_frames(frame_list);
            }

            if (p_enc_send_param->p_xcodeState->enc_eos_sent)   // eos
            {
                printf("encoder_send_thread reach eos in upload mode!\n");
                break;
            }
        }
    }

    ni_log(NI_LOG_TRACE, "encoder_send_thread exit\n");
    return NULL;
}

void *encoder_receive_thread(void *args)
{
    enc_recv_param_t *p_enc_recv_param = args;
    ni_packet_t *p_ni_packet = &p_enc_recv_param->p_out_packet->data.packet;
    niFrameSurface1_t *p_hwframe = NULL;
    int retval;
    int print_time;

    printf("encoder_receive_thread start\n");
    for (;;)
    {
        (void)ni_gettimeofday(&current_time, NULL);
        print_time = ((current_time.tv_sec - previous_time.tv_sec) > 1);
        retval = encoder_receive_data(
            p_enc_recv_param->p_enc_ctx, p_enc_recv_param->p_out_packet,
            p_enc_recv_param->output_video_width,
            p_enc_recv_param->output_video_height, p_enc_recv_param->p_file,
            p_enc_recv_param->p_total_bytes_received, print_time);
        if (retval < 0)   // Error
        {
            fprintf(stderr, "Error: encoder_receive_thread break!\n");
            break;
        } else if (p_ni_packet->end_of_stream)   // eos
        {
            printf("encoder_receive_thread reach eos\n");
            break;
        } else if (retval == 2)
        {
            ni_usleep(100);
        } else if (p_enc_recv_param->p_enc_ctx->hw_action)
        {
            //encoder only returns valid recycle index
            //when there's something to recycle.
            //This range is suitable for all memory bins
            if (p_ni_packet->recycle_index > 0 &&
                p_ni_packet->recycle_index < NI_MAX_HWDESC_FRAME_INDEX)
            {
                p_hwframe = p_enc_recv_param->p_hwframe_pool_tracker +
                    p_ni_packet->recycle_index;

                ni_log(NI_LOG_DEBUG, "recycle index:%d, %d\n",
                               p_hwframe->ui16FrameIdx,
                               p_hwframe->device_handle);
                if (p_hwframe->ui16FrameIdx)
                {
                    ni_hwframe_buffer_recycle(p_hwframe,
                                              p_hwframe->device_handle);
                    p_hwframe->ui16FrameIdx = 0;
                }
            }
        }
        if (print_time)
        {
            previous_time = current_time;
        }
    }

    ni_log(NI_LOG_TRACE, "encoder_receive_thread exit\n");
    return NULL;
}

void *uploader_thread(void *args)
{
    uploader_param_t *p_upl_param = args;
    ni_test_frame_list_t *frame_list = p_upl_param->test_frame_list;
    ni_session_data_io_t *p_out_frame = NULL;
    int retval;

    printf("uploader_thread start\n");
    for (;;)
    {
        while (test_frames_isfull(frame_list))
        {
            ni_usleep(100);
        }
        p_out_frame = &frame_list->ni_test_frame[frame_list->tail];
        retval = upload_send_data_get_desc(
            p_upl_param->p_upl_ctx, p_upl_param->p_swin_frame, p_out_frame,
            p_upl_param->input_video_width, p_upl_param->input_video_height,
            p_upl_param->pfs, total_file_size, p_upl_param->p_total_bytes_sent,
            p_upl_param->p_input_exhausted);

        if (retval < 0)
        {
            if (p_upl_param->p_upl_ctx->status ==
                NI_RETCODE_NVME_SC_WRITE_BUFFER_FULL)
            {   //No space to write to, wait for downstream to free some
                ni_usleep(100);
                continue;
            }
            fprintf(stderr, "Error: uploader_thread break!\n");
            break;
        } else if (*p_upl_param->p_input_exhausted)   // eos
        {
            enq_test_frames(frame_list);
            printf("uploader_thread reach eos\n");
            break;
        }
        uint16_t current_hwframe_index =
            ((niFrameSurface1_t *)((uint8_t *)
                                       p_out_frame->data.frame.p_data[3]))
                ->ui16FrameIdx;
        ni_log(NI_LOG_DEBUG, "uploader:%d, tail:%d\n", current_hwframe_index,
                       frame_list->tail);
        enq_test_frames(frame_list);
    }

    ni_log(NI_LOG_TRACE, "uploader_thread exit\n");
    return NULL;
}

/*!*****************************************************************************
 *  \brief  main 
 *
 *  \param  
 *
 *  \return
 ******************************************************************************/
int main(int argc, char *argv[])
{
    tx_data_t sdPara = {0};
    rx_data_t rcPara = {0};
    device_state_t xcodeState = {0};
    int err = 0, pfs = 0, sos_flag = 0;
    char xcoderGUID[32];
    int iXcoderGUID = 0;
    uint32_t result = 0;
    unsigned long total_bytes_sent;
    unsigned long long total_bytes_received;
    unsigned long long xcodeRecvTotal;
    FILE *p_file = NULL;
    char *n;   // used for parsing width and height from --size
    char mode_description[128];
    int input_video_width;
    int input_video_height;
    int arg_width = 0;
    int arg_height = 0;
    int mode = -1;
    int multi_thread = 0;
    size_t i;
    int pkt_size;
    int input_exhausted = 0;
    int num_post_recycled = 0;
    ni_xcoder_params_t api_param;
    ni_xcoder_params_t dec_api_param;
    niFrameSurface1_t *p_hwframe;
    char encConfXcoderParams[2048] = {0};
    char decConfXcoderParams[2048] = {0};
    int ret = 0;
    int recycle_index = 0;
    ni_log_level_t log_level;

    //inelegant solution but quick when memory is cheap
    niFrameSurface1_t hwframe_pool_tracker[NI_MAX_HWDESC_FRAME_INDEX];
    memset(hwframe_pool_tracker, 0,
           NI_MAX_HWDESC_FRAME_INDEX * sizeof(niFrameSurface1_t));
    ni_device_handle_t dev_handle = NI_INVALID_DEVICE_HANDLE,
                       dev_handle_1 = NI_INVALID_DEVICE_HANDLE;
    int src_codec_format = 0, dst_codec_format = 0;
    ni_log_level_t loglevel = NI_LOG_ERROR;
    int bit_depth = 8;
    ni_h264_sps_t SPS = {0};   // input header SPS

    // Input arg handling
    int opt;
    int opt_index;
    const char *opt_string = "hvtl:c:i:s:m:b:x:d:o:r:";
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
        {"decoder-params", required_argument, NULL, 'd'},
        {"output", required_argument, NULL, 'o'},
        {"repeat", required_argument, NULL, 'r'},
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
                printf("Ver:  %s\n"
                       "Date: %s\n"
                       "ID:   %s\n",
                       NI_XCODER_REVISION, NI_SW_RELEASE_TIME,
                       NI_SW_RELEASE_ID);
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
                strcpy(sdPara.fileName, optarg);
                break;
            case 's':
                arg_width = (int)strtol(optarg, &n, 10);
                arg_height = atoi(n + 1);
                if ((*n != 'x') || (!arg_width || !arg_height))
                    arg_error_exit("-s | --size", optarg);
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
                    strcmp(optarg, "u2a") != 0 && strcmp(optarg, "u2h") != 0)
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

                break;
            case 'b':
                if (!(atoi(optarg) == 8 || atoi(optarg) == 10))
                    arg_error_exit("-b | --bitdepth", optarg);
                bit_depth = atoi(optarg);
                break;
            case 'x':
                strcpy(encConfXcoderParams, optarg);
                break;
            case 'd':
                strcpy(decConfXcoderParams, optarg);
                break;
            case 'o':
                strcpy(rcPara.fileName, optarg);
                break;
            case 'r':
                if (!(atoi(optarg) >= 1))
                    arg_error_exit("-r | --repeat", optarg);
                g_repeat = atoi(optarg);
                break;
            default:
                print_usage();
                exit(1);
        }
    }

    // Check required args are present
    if (!sdPara.fileName[0])
    {
        printf("Error: missing argument for -i | --input\n");
        exit(-1);
    }
    if ((mode != XCODER_APP_TRANSCODE) && (mode != XCODER_APP_DECODE) &&
        (mode != XCODER_APP_ENCODE) && (mode != XCODER_APP_HWUP_ENCODE))
    {
        printf("Error: missing argument for -m | --mode\n");
        exit(-1);
    }
    if (!rcPara.fileName[0])
    {
        printf("Error: missing argument for -o | --output\n");
        exit(-1);
    }

    sdPara.mode = mode;
    rcPara.mode = mode;

    // Print high-level description of processing to occur and codecs involved
    printf("%s...\n", mode_description);

    pkt_size = 131040;   // hardcoded input data chunk size (for H.265)

#ifdef _WIN32
    pfs = open(sdPara.fileName, O_RDONLY | O_BINARY);
#else
    pfs = open(sdPara.fileName, O_RDONLY);
#endif

    if (pfs < 0)
    {
        fprintf(stderr, "Error: cannot open %s\n", sdPara.fileName);
        fprintf(stderr, "Error: input file read failure\n");
        err_flag = 1;
        goto end;
    }
    printf("SUCCESS: Opened input file: %s with file id = %d\n",
           sdPara.fileName, pfs);

    lseek(pfs, 0, SEEK_END);
    total_file_size = lseek(pfs, 0, SEEK_CUR);
    lseek(pfs, 0, SEEK_SET);
    unsigned long tmpFileSize = total_file_size;

    if (mode == XCODER_APP_ENCODE || mode == XCODER_APP_HWUP_ENCODE)
    {
        ni_log(NI_LOG_DEBUG, "YUV input read will repeat %d times\n", g_repeat);
        ni_log(NI_LOG_DEBUG, "YUV input, do not read all to cache\n");
    } else
    {
        //try to allocate memory for input file buffer, quit if failure
        if (total_file_size > 0 && !(g_file_cache = malloc(total_file_size)))
        {
            fprintf(stderr,
                    "Error: input file size %u exceeding max malloc, quit\n",
                    total_file_size);
            goto end;
        }
        g_curr_cache_pos = g_file_cache;
        printf("Reading %u bytes in total ..\n", total_file_size);
        while (tmpFileSize)
        {
            int one_read_size = read(pfs, g_curr_cache_pos, 4096);
            if (one_read_size == -1)
            {
                fprintf(stderr, "Error: reading file, quit! left-to-read %lu\n",
                        tmpFileSize);
                fprintf(stderr, "Error: input file read error\n");
                goto end;
            } else
            {
                tmpFileSize -= one_read_size;
                g_curr_cache_pos += one_read_size;
            }
        }
        printf("read %u bytes from input file into memory\n", total_file_size);
    }
    g_curr_cache_pos = g_file_cache;
    data_left_size = total_file_size;

    if (strcmp(rcPara.fileName, "null") != 0)
    {
        p_file = fopen(rcPara.fileName, "wb");
        if (p_file == NULL)
        {
            fprintf(stderr, "Error: cannot open %s\n", rcPara.fileName);
            err_flag = 1;
            goto end;
        }
    }
    printf("SUCCESS: Opened output file: %s\n", rcPara.fileName);

    // for H.264, probe the source and use the probed source info as defaults
    if (NI_CODEC_FORMAT_H264 == src_codec_format &&
        (mode == XCODER_APP_TRANSCODE || mode == XCODER_APP_DECODE))
    {
        if (probe_h264_stream_info(&SPS))
        {
            fprintf(stderr,
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
    }

    sdPara.arg_width = arg_width;
    sdPara.arg_height = arg_height;
    rcPara.arg_width = arg_width;
    rcPara.arg_height = arg_height;

    // set up decoder p_config with some hard coded numbers
    if (ni_decoder_init_default_params(&dec_api_param, 25, 1, 200000, arg_width,
                                       arg_height) < 0)
    {
        fprintf(stderr, "Error: decoder p_config set up error\n");
        return -1;
    }

    send_fin_flag = 0;
    receive_fin_flag = 0;

    ni_session_context_t dec_ctx = {0};
    ni_session_context_t enc_ctx = {0};
    ni_session_context_t upl_ctx = {0};

    if (ni_device_session_context_init(&dec_ctx) < 0)
    {
        fprintf(stderr, "Error: init decoder context error\n");
        return -1;
    }
    if (ni_device_session_context_init(&enc_ctx) < 0)
    {
        fprintf(stderr, "Error: init decoder context error\n");
        return -1;
    }
    if (ni_device_session_context_init(&upl_ctx) < 0)
    {
        fprintf(stderr, "Error: init decoder context error\n");
        return -1;
    }

    sdPara.p_dec_ctx = (void *)&dec_ctx;
    sdPara.p_enc_ctx = (void *)&enc_ctx;
    sdPara.p_upl_ctx = (void *)&upl_ctx;
    rcPara.p_dec_ctx = (void *)&dec_ctx;
    rcPara.p_enc_ctx = (void *)&enc_ctx;
    rcPara.p_upl_ctx = (void *)&upl_ctx;

    enc_ctx.nb_rois = 0;
    enc_ctx.roi_side_data_size = 0;
    enc_ctx.nb_rois = 0;
    enc_ctx.roi_side_data_size = 0;
    enc_ctx.av_rois = NULL;

    enc_ctx.codec_format = dst_codec_format;

    if (mode == XCODER_APP_TRANSCODE || mode == XCODER_APP_DECODE)
    {
        dec_ctx.p_session_config = NULL;
        dec_ctx.session_id = NI_INVALID_SESSION_ID;
        dec_ctx.codec_format = src_codec_format;

        // default decode the UDU; ToDo: make it configurable
        dec_ctx.enable_user_data_sei_passthru = 1;

        // no need to directly allocate resource context
        rcPara.p_dec_rsrc_ctx = sdPara.p_dec_rsrc_ctx = NULL;

        // assign the card GUID in the decoder context and let session open
        // take care of the rest
        dec_ctx.device_handle = dec_ctx.blk_io_handle =
            NI_INVALID_DEVICE_HANDLE;
        dec_ctx.hw_id = iXcoderGUID;

        dec_ctx.p_session_config = &dec_api_param;
        // default: little endian
        dec_ctx.src_bit_depth = bit_depth;
        dec_ctx.src_endian = NI_FRAME_LITTLE_ENDIAN;
        dec_ctx.bit_depth_factor = 1;
        if (10 == dec_ctx.src_bit_depth)
        {
            dec_ctx.bit_depth_factor = 2;
        }

        // check and set ni_decoder_params from --xcoder-params
        if (retrieve_decoder_params(decConfXcoderParams, &dec_api_param,
                                    &dec_ctx))   //
        {
            fprintf(stderr, "Error: encoder p_config parsing error\n");
            return -1;
        }
        if (dec_ctx.hw_action && mode == XCODER_APP_DECODE)
        {
            fprintf(
                stderr,
                "Error: decode only with hwframes produces unusable output\n");
            return -1;
        }

        // Decode, use all the parameters specified by user
        if (0 !=
            (ret = decoder_open_session(&dec_ctx, iXcoderGUID, &dec_api_param)))
        {
            goto end;
        }
    }

    if (mode == XCODER_APP_TRANSCODE || mode == XCODER_APP_ENCODE ||
        mode == XCODER_APP_HWUP_ENCODE)
    {
        // no need to directly allocate resource context for encoder
        rcPara.p_enc_rsrc_ctx = sdPara.p_enc_rsrc_ctx = NULL;
        rcPara.p_upl_rsrc_ctx = sdPara.p_upl_rsrc_ctx = NULL;
    }

    // encoder session open, if needed, will be at the first frame arrival as it
    // carries source stream info that may be useful in encoding config

    sos_flag = 1;
    total_bytes_received = 0;
    xcodeRecvTotal = 0;
    total_bytes_sent = 0;

    printf("user video resolution: %dx%d\n", arg_width, arg_height);
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

#if 0
#ifdef __linux__
    struct timespec start, end;
    clock_gettime(CLOCK_PROCESS_CPUTIME_ID, &start);
#endif
#endif

    if (mode == XCODER_APP_DECODE)
    {
        printf("Decoding Mode: %dx%d to %dx%d HWFrames %d\n", input_video_width,
               input_video_height, output_video_width, output_video_height,
               dec_ctx.hw_action);
        ni_session_data_io_t in_pkt = {0};
        ni_session_data_io_t out_frame = {0};
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
            dec_send_param.p_xcodeState = &xcodeState;
            dec_send_param.p_SPS = &SPS;

            dec_recv_param.p_dec_ctx = &dec_ctx;
            dec_recv_param.p_out_frame = &out_frame;
            dec_recv_param.output_video_width = output_video_width;
            dec_recv_param.output_video_height = output_video_height;
            dec_recv_param.p_file = p_file;
            dec_recv_param.p_total_bytes_received = &total_bytes_received;
            dec_recv_param.p_xcodeState = &xcodeState;
            dec_recv_param.mode = mode;
            dec_recv_param.test_frame_list = NULL;

            if (ni_pthread_create(&dec_send_tid, NULL, decoder_send_thread,
                                  &dec_send_param))
            {
                fprintf(stderr,
                        "Error: create decoder send thread failed in decode "
                        "mode\n");
                return -1;
            }
            if (ni_pthread_create(&dec_recv_tid, NULL, decoder_receive_thread,
                                  &dec_recv_param))
            {
                fprintf(stderr,
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
                int print_time =
                    ((current_time.tv_sec - previous_time.tv_sec) > 1);

                // Sending
                send_fin_flag = decoder_send_data(
                    &dec_ctx, &in_pkt, sos_flag, input_video_width,
                    input_video_height, pkt_size, total_file_size,
                    &total_bytes_sent, print_time, &xcodeState, &SPS);
                sos_flag = 0;
                if (send_fin_flag < 0)
                {
                    fprintf(stderr,
                            "Error: decoder_send_data() failed, rc: %d\n",
                            send_fin_flag);
                    break;
                }

                // Receiving
                receive_fin_flag = decoder_receive_data(
                    &dec_ctx, &out_frame, output_video_width,
                    output_video_height, p_file, &total_bytes_received,
                    print_time, 1, &xcodeState);

                ni_decoder_frame_buffer_free(&(out_frame.data.frame));
                if (print_time)
                {
                    previous_time = current_time;
                }

                // Error or eos
                if (receive_fin_flag < 0 || out_frame.data.frame.end_of_stream)
                {
                    break;
                }
            }
        }
        unsigned int time_diff =
            (unsigned int)(current_time.tv_sec - start_time.tv_sec);
        if (time_diff == 0)
            time_diff = 1;

        printf("[R] Got:  Frames= %u  fps=%f  Total bytes %llu\n",
               number_of_frames, ((float)number_of_frames / (float)time_diff),
               total_bytes_received);

        ni_device_session_close(&dec_ctx, 1, NI_DEVICE_TYPE_DECODER);
        ni_rsrc_free_device_context(sdPara.p_dec_rsrc_ctx);
        rcPara.p_dec_rsrc_ctx = NULL;
        sdPara.p_dec_rsrc_ctx = NULL;

        ni_packet_buffer_free(&(in_pkt.data.packet));
        ni_decoder_frame_buffer_free(&(out_frame.data.frame));
    } else if (mode == XCODER_APP_ENCODE)
    {
        printf("Encoding Mode: %dx%d to %dx%d\n", input_video_width,
               input_video_height, output_video_width, output_video_height);

        // set up encoder p_config, using some hard coded numbers
        if (ni_encoder_init_default_params(&api_param, 30, 1, 200000, arg_width,
                                           arg_height,
                                           enc_ctx.codec_format) < 0)
        {
            fprintf(stderr, "Error: encoder init default set up error\n");
            return -1;
        }

        // check and set ni_encoder_params from --xcoder-params
        if (retrieve_xcoder_params(encConfXcoderParams, &api_param, &enc_ctx))
        {
            fprintf(stderr, "Error: encoder p_config parsing error\n");
            return -1;
        }

        ni_hrd_params_t hrd_params;
        int video_full_range_flag = 0;
        if (api_param.video_full_range_flag >= 0)
        {
            ni_log(NI_LOG_DEBUG, "Using user-configured video_full_range_flag "
                           "%d\n",
                           api_param.video_full_range_flag);
            video_full_range_flag = api_param.video_full_range_flag;
        }

        // for encode from YUV, use all the parameters specified by user
        if (0 !=
            (ret = encoder_open_session(
                 &enc_ctx, dst_codec_format, iXcoderGUID, &api_param, bit_depth,
                 arg_width, arg_height, &hrd_params, api_param.color_primaries,
                 api_param.color_transfer_characteristic, api_param.color_space,
                 video_full_range_flag, api_param.sar_num, api_param.sar_denom,
                 1)))
        {
            goto end;
        }

        ni_session_data_io_t in_frame = {0};
        ni_session_data_io_t out_packet = {0};
        if (multi_thread)
        {
            ni_pthread_t enc_send_tid, enc_recv_tid;
            enc_send_param_t enc_send_param = {0};
            enc_recv_param_t enc_recv_param = {0};

            enc_send_param.p_enc_ctx = &enc_ctx;
            enc_send_param.p_in_frame = &in_frame;
            enc_send_param.input_video_width = input_video_width;
            enc_send_param.input_video_height = input_video_height;
            enc_send_param.pfs = pfs;
            enc_send_param.p_total_bytes_sent = &total_bytes_sent;
            enc_send_param.p_xcodeState = &xcodeState;
            enc_send_param.mode = mode;
            enc_send_param.test_frame_list = NULL;
            enc_send_param.dec_codec_format = -1;           // not use heres
            enc_send_param.p_input_exhausted = NULL;        // not use heres
            enc_send_param.p_hwframe_pool_tracker = NULL;   // not use heres

            enc_recv_param.p_enc_ctx = &enc_ctx;
            enc_recv_param.p_out_packet = &out_packet;
            enc_recv_param.output_video_width = output_video_width;
            enc_recv_param.output_video_height = output_video_height;
            enc_recv_param.p_file = p_file;
            enc_recv_param.p_total_bytes_received = &total_bytes_received;
            enc_recv_param.p_xcodeState = &xcodeState;
            enc_recv_param.mode = mode;
            enc_recv_param.p_hwframe_pool_tracker = NULL;

            if (ni_pthread_create(&enc_send_tid, NULL, encoder_send_thread,
                                  &enc_send_param))
            {
                fprintf(stderr,
                        "Error: create encoder send thread failed in encode "
                        "mode\n");
                return -1;
            }
            if (ni_pthread_create(&enc_recv_tid, NULL, encoder_receive_thread,
                                  &enc_recv_param))
            {
                fprintf(stderr,
                        "Error: create encoder recieve thread failed in encode "
                        "mode\n");
                return -1;
            }
            ni_pthread_join(enc_send_tid, NULL);
            ni_pthread_join(enc_recv_tid, NULL);
            (void)ni_gettimeofday(&current_time, NULL);
        } else
        {
            while (send_fin_flag == 0 || receive_fin_flag == 0)
            {
                (void)ni_gettimeofday(&current_time, NULL);
                int print_time =
                    ((current_time.tv_sec - previous_time.tv_sec) > 1);

                // Sending
                send_fin_flag = encoder_send_data(
                    &enc_ctx, &in_frame, sos_flag, input_video_width,
                    input_video_height, pfs, total_file_size, &total_bytes_sent,
                    &xcodeState);
                sos_flag = 0;
                if (send_fin_flag == 2)   //Error
                {
                    break;
                }

                // Receiving
                receive_fin_flag = encoder_receive_data(
                    &enc_ctx, &out_packet, output_video_width,
                    output_video_height, p_file, &total_bytes_received,
                    print_time);

                if (print_time)
                {
                    previous_time = current_time;
                }

                // Error or eos
                if (receive_fin_flag < 0 ||
                    out_packet.data.packet.end_of_stream)
                {
                    break;
                }
            }
        }
        int timeDiff = (int)(current_time.tv_sec - start_time.tv_sec);
        if (timeDiff == 0)
            timeDiff = 1;
        printf("[R] Got:  Packets= %u fps=%f  Total bytes %llu\n",
               number_of_packets,
               ((float)number_of_packets) / ((float)timeDiff),
               total_bytes_received);

        ni_device_session_close(&enc_ctx, 1, NI_DEVICE_TYPE_ENCODER);
        ni_rsrc_free_device_context(sdPara.p_enc_rsrc_ctx);
        rcPara.p_enc_rsrc_ctx = NULL;
        sdPara.p_enc_rsrc_ctx = NULL;

        ni_frame_buffer_free(&(in_frame.data.frame));
        ni_packet_buffer_free(&(out_packet.data.packet));
    } else if (mode == XCODER_APP_HWUP_ENCODE)
    {
        ni_session_data_io_t in_frame = {0};
        ni_session_data_io_t swin_frame = {0};
        ni_session_data_io_t out_packet = {0};
        printf("Upload + Encoding Mode: %dx%d to %dx%d\n", input_video_width,
               input_video_height, output_video_width, output_video_height);

        if (multi_thread)
        {
            ni_test_frame_list_t test_frame_list = {0};
            ni_pthread_t uploader_tid;
            uploader_param_t uploader_param = {0};
            ni_pthread_t enc_send_tid, enc_recv_tid;
            enc_send_param_t enc_send_param = {0};
            enc_recv_param_t enc_recv_param = {0};
            ni_frame_t *p_ni_frame = NULL;

            //open uploader
            int pool_size =
                3;   //3 starting buffers to be augmented with additional
            // buffers by downstream entity like encoders
            if (0 !=
                (ret = uploader_open_session(&upl_ctx, iXcoderGUID, bit_depth,
                                             arg_width, arg_height,
                                             1 /*assume planar*/, pool_size)))
            {
                goto end;
            }

            uploader_param.p_upl_ctx = &upl_ctx;
            uploader_param.p_swin_frame = &swin_frame;
            uploader_param.input_video_width = input_video_width;
            uploader_param.input_video_height = input_video_height;
            uploader_param.pfs = pfs;
            uploader_param.p_total_bytes_sent = &total_bytes_sent;
            uploader_param.p_input_exhausted = &input_exhausted;
            uploader_param.pool_size = pool_size;
            uploader_param.test_frame_list = &test_frame_list;

            if (ni_pthread_create(&uploader_tid, NULL, uploader_thread,
                                  &uploader_param))
            {
                fprintf(
                    stderr,
                    "Error: create uploader thread failed in upload mode\n");
                return -1;
            }

            // wait until received first HW frame
            while (test_frames_isempty(&test_frame_list))
            {
                ni_usleep(100);
            }

            p_ni_frame =
                &test_frame_list.ni_test_frame[test_frame_list.head].data.frame;
            // set up encoder p_config, using some hard coded numbers
            if (ni_encoder_init_default_params(&api_param, 30, 1, 200000,
                                               arg_width, arg_height,
                                               enc_ctx.codec_format) < 0)
            {
                fprintf(stderr, "Error: encoder init default set up error\n");
                return -1;
            }

            // check and set ni_encoder_params from --xcoder-params
            if (retrieve_xcoder_params(encConfXcoderParams, &api_param,
                                       &enc_ctx))
            {
                fprintf(stderr, "Error: encoder p_config parsing error\n");
                return -1;
            }

            ni_hrd_params_t hrd_params;
            int video_full_range_flag = 0;
            if (api_param.video_full_range_flag >= 0)
            {
                ni_log(NI_LOG_DEBUG, "Using user-configured video_full_range_flag "
                               "%d\n",
                               api_param.video_full_range_flag);
                video_full_range_flag = api_param.video_full_range_flag;
            }

            //always HWframe mode if preceeded by HWUPLOAD
            enc_ctx.hw_action = NI_CODEC_HW_ENABLE;
            api_param.hwframes = 1;
            //to determine if from same device and buffer dimensions in memory
            //needs to be done where input frame is available to check
            p_hwframe = p_ni_frame->p_data[3];
            enc_ctx.sender_handle =
                (ni_device_handle_t)(int64_t)p_hwframe->device_handle;
            api_param.rootBufId = p_hwframe->ui16FrameIdx;

            // for encode from YUV, use all the parameters specified by user
            if (0 !=
                (ret = encoder_open_session(
                    &enc_ctx, dst_codec_format, iXcoderGUID, &api_param, bit_depth,
                    arg_width, arg_height, &hrd_params, api_param.color_primaries,
                    api_param.color_transfer_characteristic, api_param.color_space,
                    video_full_range_flag, api_param.sar_num, api_param.sar_denom,
                    1)))
            {
                goto end;
            }

            // init and create encoding thread
            enc_send_param.p_enc_ctx = &enc_ctx;
            enc_send_param.p_in_frame = NULL;
            enc_send_param.input_video_width = input_video_width;
            enc_send_param.input_video_height = input_video_height;
            enc_send_param.pfs = pfs;
            enc_send_param.p_total_bytes_sent = &total_bytes_sent;
            enc_send_param.p_xcodeState = &xcodeState;
            enc_send_param.mode = mode;
            enc_send_param.test_frame_list = &test_frame_list;
            enc_send_param.dec_codec_format = dec_ctx.codec_format;
            enc_send_param.p_input_exhausted = &input_exhausted;
            enc_send_param.p_hwframe_pool_tracker = hwframe_pool_tracker;

            enc_recv_param.p_enc_ctx = &enc_ctx;
            enc_recv_param.p_out_packet = &out_packet;
            enc_recv_param.output_video_width = output_video_width;
            enc_recv_param.output_video_height = output_video_height;
            enc_recv_param.p_file = p_file;
            enc_recv_param.p_total_bytes_received = &total_bytes_received;
            enc_recv_param.p_xcodeState = &xcodeState;
            enc_recv_param.mode = mode;
            enc_recv_param.p_hwframe_pool_tracker = hwframe_pool_tracker;

            if (ni_pthread_create(&enc_send_tid, NULL, encoder_send_thread,
                                  &enc_send_param))
            {
                fprintf(stderr,
                        "Error: create encoder send thread failed in transcode "
                        "mode\n");
                return -1;
            }
            if (ni_pthread_create(&enc_recv_tid, NULL, encoder_receive_thread,
                                  &enc_recv_param))
            {
                fprintf(stderr,
                        "Error: create encoder recieve thread failed in "
                        "transcode mode\n");
                return -1;
            }
            ni_pthread_join(uploader_tid, NULL);
            ni_pthread_join(enc_send_tid, NULL);
            ni_pthread_join(enc_recv_tid, NULL);
            (void)ni_gettimeofday(&current_time, NULL);
            drain_test_list(&enc_send_param, &test_frame_list);
            for (i = 0; i < NI_MAX_BUFFERED_FRAME; i++)
            {
                ni_frame_buffer_free(
                    &(test_frame_list.ni_test_frame[i].data.frame));
            }
        } else
        {
            //open uploader
            int pool_size =
                3;   //3 starting buffers to be augmented with additional
            // buffers by downstream entity like encoders
            if (0 !=
                (ret = uploader_open_session(&upl_ctx, iXcoderGUID, bit_depth,
                                             arg_width, arg_height,
                                             1 /*assume planar*/, pool_size)))
            {
                goto end;
            }

            //need to have the hwframe before open encoder
            if (upload_send_data_get_desc(&upl_ctx, &swin_frame, &in_frame,
                                          input_video_width, input_video_height,
                                          pfs, total_file_size,
                                          &total_bytes_sent, &input_exhausted))
            {
                fprintf(stderr, "Error: upload frame error\n");
                return -1;
            }

            // set up encoder p_config, using some hard coded numbers
            if (ni_encoder_init_default_params(&api_param, 30, 1, 200000,
                                               arg_width, arg_height,
                                               enc_ctx.codec_format) < 0)
            {
                fprintf(stderr, "Error: encoder init default set up error\n");
                return -1;
            }

            // check and set ni_encoder_params from --xcoder-params
            if (retrieve_xcoder_params(encConfXcoderParams, &api_param,
                                       &enc_ctx))
            {
                fprintf(stderr, "Error: encoder p_config parsing error\n");
                return -1;
            }

            ni_hrd_params_t hrd_params;
            int video_full_range_flag = 0;
            if (api_param.video_full_range_flag >= 0)
            {
                ni_log(NI_LOG_DEBUG, "Using user-configured video_full_range_flag "
                               "%d\n",
                               api_param.video_full_range_flag);
                video_full_range_flag = api_param.video_full_range_flag;
            }

            //always HWframe mode if preceeded by HWUPLOAD
            enc_ctx.hw_action = NI_CODEC_HW_ENABLE;
            api_param.hwframes = 1;
            //to determine if from same device and buffer dimensions in memory
            //needs to be done where input frame is available to check
            p_hwframe = in_frame.data.frame.p_data[3];
            enc_ctx.sender_handle =
                (ni_device_handle_t)(int64_t)p_hwframe->device_handle;
            api_param.rootBufId = p_hwframe->ui16FrameIdx;

            // for encode from YUV, use all the parameters specified by user
            if (0 !=
                (ret = encoder_open_session(
                     &enc_ctx, dst_codec_format, iXcoderGUID, &api_param, bit_depth,
                     arg_width, arg_height, &hrd_params, api_param.color_primaries,
                     api_param.color_transfer_characteristic, api_param.color_space,
                     video_full_range_flag, api_param.sar_num, api_param.sar_denom,
                     1)))
            {
                goto end;
            }
            int read_from_file = 0;
            int need_to_resend = 0;
            unsigned int prev_num_pkt;
            while (send_fin_flag == 0 || receive_fin_flag == 0)
            {
                (void)ni_gettimeofday(&current_time, NULL);
                int print_time =
                    ((current_time.tv_sec - previous_time.tv_sec) > 1);

                if (read_from_file && !input_exhausted && need_to_resend == 0)
                {
                    if (upload_send_data_get_desc(
                            &upl_ctx, &swin_frame, &in_frame, input_video_width,
                            input_video_height, pfs, total_file_size,
                            &total_bytes_sent, &input_exhausted))
                    {
                        fprintf(stderr, "Error: upload frame error\n");
                        return -1;
                    }
                }

                // Sending
                send_fin_flag = encoder_send_data3(
                    &enc_ctx, &in_frame, sos_flag, input_video_width,
                    input_video_height, &xcodeState, input_exhausted,
                    &need_to_resend);
                read_from_file = 1;   //since first frame read before while-loop

                sos_flag = 0;
                if (send_fin_flag == 2)   //Error
                {
                    break;
                }
                //track in array with unique index, free when enc read finds
                //this must be implemented in application space for complete
                //tracking of hwframes
                if (need_to_resend == 0)
                {
                    //successful read means there is recycle to check
                    uint16_t current_hwframe_index =
                        ((niFrameSurface1_t *)((uint8_t *)in_frame.data.frame
                                                   .p_data[3]))
                            ->ui16FrameIdx;
                    memcpy(&hwframe_pool_tracker[current_hwframe_index],
                           (uint8_t *)in_frame.data.frame.p_data[3],
                           sizeof(niFrameSurface1_t));
                }

                // Receiving
                prev_num_pkt = number_of_packets;
                receive_fin_flag = encoder_receive_data(
                    &enc_ctx, &out_packet, output_video_width,
                    output_video_height, p_file, &total_bytes_received,
                    print_time);
                recycle_index = out_packet.data.packet.recycle_index;
                if (prev_num_pkt < number_of_packets && enc_ctx.hw_action &&
                    recycle_index > 0 &&
                    recycle_index < NI_MAX_HWDESC_FRAME_INDEX)
                //encoder only returns valid recycle index
                //when there's something to recycle.
                //This range is suitable for all memory bins
                {
                    if (hwframe_pool_tracker[recycle_index].ui16FrameIdx)
                    {
                        ni_hwframe_buffer_recycle(
                            &hwframe_pool_tracker[recycle_index],
                            hwframe_pool_tracker[recycle_index].device_handle);
                        //zero for tracking purposes
                        hwframe_pool_tracker[recycle_index].ui16FrameIdx = 0;
                    }
                }
                if (print_time)
                {
                    previous_time = current_time;
                }

                // Error or eos
                if (receive_fin_flag < 0 ||
                    out_packet.data.packet.end_of_stream)
                {
                    break;
                }
            }
        }
        int timeDiff = (int)(current_time.tv_sec - start_time.tv_sec);
        if (timeDiff == 0)
            timeDiff = 1;
        printf("[R] Got:  Packets= %u fps=%f  Total bytes %llu\n",
               number_of_packets,
               ((float)number_of_packets) / ((float)timeDiff),
               total_bytes_received);

        num_post_recycled = scan_and_clean_hwdescriptors(hwframe_pool_tracker);
        ni_log(NI_LOG_DEBUG, "Cleanup recycled %d internal buffers\n",
                       num_post_recycled);
        ni_device_session_close(&enc_ctx, 1, NI_DEVICE_TYPE_ENCODER);
        ni_device_session_close(&upl_ctx, 1, NI_DEVICE_TYPE_UPLOAD);
        ni_rsrc_free_device_context(sdPara.p_enc_rsrc_ctx);
        rcPara.p_enc_rsrc_ctx = NULL;
        sdPara.p_enc_rsrc_ctx = NULL;
        rcPara.p_upl_rsrc_ctx = NULL;
        sdPara.p_upl_rsrc_ctx = NULL;

        ni_frame_buffer_free(&(in_frame.data.frame));
        ni_frame_buffer_free(&(swin_frame.data.frame));
        ni_packet_buffer_free(&(out_packet.data.packet));
    } else if (mode == XCODER_APP_TRANSCODE)
    {
        printf("Xcoding Mode: %dx%d to %dx%d\n", input_video_width,
               input_video_height, output_video_width, output_video_height);

        ni_session_data_io_t in_pkt = {0};
        ni_session_data_io_t out_frame = {0};
        ni_session_data_io_t enc_in_frame = {0};
        ni_session_data_io_t out_packet = {0};
        uint32_t prev_num_pkt;
        ni_frame_t *p_ni_frame = NULL;

        if (multi_thread)
        {
            ni_test_frame_list_t test_frame_list = {0};

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
            dec_send_param.p_xcodeState = &xcodeState;
            dec_send_param.p_SPS = &SPS;

            dec_recv_param.p_dec_ctx = &dec_ctx;
            dec_recv_param.p_out_frame = NULL;
            dec_recv_param.output_video_width = output_video_width;
            dec_recv_param.output_video_height = output_video_height;
            dec_recv_param.p_file = p_file;
            dec_recv_param.p_total_bytes_received = &total_bytes_received;
            dec_recv_param.p_xcodeState = &xcodeState;
            dec_recv_param.mode = mode;
            dec_recv_param.test_frame_list = &test_frame_list;

            if (ni_pthread_create(&dec_send_tid, NULL, decoder_send_thread,
                                  &dec_send_param))
            {
                fprintf(stderr,
                        "Error: create decoder send thread failed in transcode "
                        "mode\n");
                return -1;
            }
            if (ni_pthread_create(&dec_recv_tid, NULL, decoder_receive_thread,
                                  &dec_recv_param))
            {
                fprintf(stderr,
                        "Error: create decoder receive thread failed in "
                        "transcode mode\n");
                return -1;
            }

            // wait until received first decoded frame
            while (test_frames_isempty(&test_frame_list))
            {
                ni_usleep(100);
            }

            p_ni_frame =
                &test_frame_list.ni_test_frame[test_frame_list.head].data.frame;
            // open the encode session when the first frame arrives and the session
            // is not opened yet, with the source stream and user-configured encode
            // info both considered when constructing VUI in the stream headers
            int color_pri = p_ni_frame->color_primaries;
            int color_trc = p_ni_frame->color_trc;
            int color_space = p_ni_frame->color_space;
            int video_full_range_flag = p_ni_frame->video_full_range_flag;
            int sar_num = p_ni_frame->sar_width;
            int sar_den = p_ni_frame->sar_height;
            int fps_num = 0, fps_den = 0;

            // calculate the source fps and set it as the default target fps, based
            // on the timing_info passed in from the decoded frame
            if (p_ni_frame->vui_num_units_in_tick && p_ni_frame->vui_time_scale)
            {
                if (NI_CODEC_FORMAT_H264 == p_ni_frame->src_codec)
                {
                    if (0 == (p_ni_frame->vui_time_scale % 2))
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

            // set up encoder p_config, using some info from source
            if (ni_encoder_init_default_params(&api_param, fps_num, fps_den,
                                               200000, arg_width, arg_height,
                                               enc_ctx.codec_format) < 0)
            {
                fprintf(stderr, "Error: encoder init default set up error\n");
                return -1;
            }

            // check and set ni_encoder_params from --xcoder-params
            // Note: the parameter setting has to be in this order so that user
            //       configured values can overwrite the source/default ones if
            //       desired.
            if (retrieve_xcoder_params(encConfXcoderParams, &api_param,
                                       &enc_ctx))
            {
                fprintf(stderr, "Error: encoder p_config parsing error\n");
                return -1;
            }

            if (color_pri != api_param.color_primaries &&
                NI_COL_PRI_UNSPECIFIED != api_param.color_primaries)
            {
                ni_log(NI_LOG_DEBUG, "Using user-configured color primaries %d to "
                               "overwrite source %d\n",
                               api_param.color_primaries, color_pri);
                color_pri = api_param.color_primaries;
            }
            if (color_trc != api_param.color_transfer_characteristic &&
                NI_COL_TRC_UNSPECIFIED !=
                    api_param.color_transfer_characteristic)
            {
                ni_log(NI_LOG_DEBUG, "Using user-configured color trc %d to overwrite"
                               " source %d\n",
                               api_param.color_transfer_characteristic,
                               color_trc);
                color_trc = api_param.color_transfer_characteristic;
            }
            if (color_space != api_param.color_space &&
                NI_COL_SPC_UNSPECIFIED != api_param.color_space)
            {
                ni_log(NI_LOG_DEBUG, "Using user-configured color space %d to "
                               "overwrite source %d\n",
                               api_param.color_space, color_space);
                color_space = api_param.color_space;
            }
            if (api_param.video_full_range_flag >= 0)
            {
                ni_log(NI_LOG_DEBUG, "Using user-configured video_full_range_flag "
                               "%d\n",
                               api_param.video_full_range_flag);
                video_full_range_flag = api_param.video_full_range_flag;
            }
            if (p_ni_frame->aspect_ratio_idc > 0 &&
                p_ni_frame->aspect_ratio_idc < NI_NUM_PIXEL_ASPECT_RATIO)
            {
                sar_num =
                    ni_h264_pixel_aspect_list[p_ni_frame->aspect_ratio_idc].num;
                sar_den =
                    ni_h264_pixel_aspect_list[p_ni_frame->aspect_ratio_idc].den;
            } else if (api_param.sar_denom)
            {
                sar_num = api_param.sar_num;
                sar_den = api_param.sar_denom;
            }

            ni_hrd_params_t hrd_params;
            if (!dec_ctx.hw_action)
            {
                enc_ctx.hw_action = NI_CODEC_HW_NONE;
            } else
            {
                //Items in this else condition could be improved by being handled in libxcoder
                enc_ctx.hw_action = NI_CODEC_HW_ENABLE;
                api_param.hwframes = 1;
                //to determine if from same device and buffer dimensions in memory
                //needs to be done where input frame is available to check
                p_hwframe = p_ni_frame->p_data[3];
                enc_ctx.sender_handle =
                    (ni_device_handle_t)(int64_t)p_hwframe->device_handle;
                api_param.rootBufId = p_hwframe->ui16FrameIdx;
            }

            if (0 !=
                (ret = encoder_open_session(
                     &enc_ctx, dst_codec_format, iXcoderGUID, &api_param,
                     bit_depth, arg_width, arg_height, &hrd_params, color_pri,
                     color_trc, color_space, video_full_range_flag, sar_num,
                     sar_den, 1)))
            {
                ni_log(NI_LOG_ERROR, "Error: encoder_open_session failed, stop!\n");
                goto end;
            }

            // init and create encoding thread
            enc_send_param.p_enc_ctx = &enc_ctx;
            enc_send_param.p_in_frame = &enc_in_frame;
            enc_send_param.input_video_width = input_video_width;
            enc_send_param.input_video_height = input_video_height;
            enc_send_param.pfs = pfs;
            enc_send_param.p_total_bytes_sent = &total_bytes_sent;
            enc_send_param.p_xcodeState = &xcodeState;
            enc_send_param.mode = mode;
            enc_send_param.test_frame_list = &test_frame_list;
            enc_send_param.dec_codec_format = dec_ctx.codec_format;
            enc_send_param.p_input_exhausted = NULL;   // not use heres
            enc_send_param.p_hwframe_pool_tracker = hwframe_pool_tracker;

            enc_recv_param.p_enc_ctx = &enc_ctx;
            enc_recv_param.p_out_packet = &out_packet;
            enc_recv_param.output_video_width = output_video_width;
            enc_recv_param.output_video_height = output_video_height;
            enc_recv_param.p_file = p_file;
            enc_recv_param.p_total_bytes_received = &xcodeRecvTotal;
            enc_recv_param.p_xcodeState = &xcodeState;
            enc_recv_param.mode = mode;
            enc_recv_param.p_hwframe_pool_tracker = hwframe_pool_tracker;

            if (ni_pthread_create(&enc_send_tid, NULL, encoder_send_thread,
                                  &enc_send_param))
            {
                fprintf(stderr,
                        "Error: create encoder send thread failed in transcode "
                        "mode\n");
                return -1;
            }
            if (ni_pthread_create(&enc_recv_tid, NULL, encoder_receive_thread,
                                  &enc_recv_param))
            {
                fprintf(stderr,
                        "Error: create encoder recieve thread failed in "
                        "transcode mode\n");
                return -1;
            }
            ni_pthread_join(dec_send_tid, NULL);
            ni_pthread_join(dec_recv_tid, NULL);
            ni_pthread_join(enc_send_tid, NULL);
            ni_pthread_join(enc_recv_tid, NULL);
            (void)ni_gettimeofday(&current_time, NULL);
            drain_test_list(&enc_send_param, &test_frame_list);
            for (i = 0; i < NI_MAX_BUFFERED_FRAME; i++)
            {
                ni_frame_buffer_free(
                    &(test_frame_list.ni_test_frame[i].data.frame));
            }
        } else
        {
            int need_to_resend = 0;
            while (send_fin_flag == 0 || receive_fin_flag == 0)
            {
                (void)ni_gettimeofday(&current_time, NULL);
                int print_time =
                    ((current_time.tv_sec - previous_time.tv_sec) > 1);

                if (need_to_resend)
                    goto encode_send;
                // bitstream Sending
                send_fin_flag = decoder_send_data(
                    &dec_ctx, &in_pkt, sos_flag, input_video_width,
                    input_video_height, pkt_size, total_file_size,
                    &total_bytes_sent, print_time, &xcodeState, &SPS);

                sos_flag = 0;
                if (send_fin_flag == 2)   //Error
                {
                    break;
                }

                // YUV Receiving: not writing to file
                receive_fin_flag = decoder_receive_data(
                    &dec_ctx, &out_frame, output_video_width,
                    output_video_height, p_file, &total_bytes_received,
                    print_time, 0, &xcodeState);

                if (print_time)
                {
                    previous_time = current_time;
                }

                if (2 == receive_fin_flag)
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
                    goto encode_recv;
                } else if (NI_INVALID_SESSION_ID == enc_ctx.session_id ||
                           NI_INVALID_DEVICE_HANDLE == enc_ctx.blk_io_handle)
                {
                    // open the encode session when the first frame arrives and the session
                    // is not opened yet, with the source stream and user-configured encode
                    // info both considered when constructing VUI in the stream headers
                    int color_pri = out_frame.data.frame.color_primaries;
                    int color_trc = out_frame.data.frame.color_trc;
                    int color_space = out_frame.data.frame.color_space;
                    int video_full_range_flag =
                        out_frame.data.frame.video_full_range_flag;
                    int sar_num = out_frame.data.frame.sar_width;
                    int sar_den = out_frame.data.frame.sar_height;
                    int fps_num = 0, fps_den = 0;

                    // calculate the source fps and set it as the default target fps, based
                    // on the timing_info passed in from the decoded frame
                    if (out_frame.data.frame.vui_num_units_in_tick &&
                        out_frame.data.frame.vui_time_scale)
                    {
                        if (NI_CODEC_FORMAT_H264 ==
                            out_frame.data.frame.src_codec)
                        {
                            if (0 == (out_frame.data.frame.vui_time_scale % 2))
                            {
                                fps_num =
                                    (int)(out_frame.data.frame.vui_time_scale /
                                          2);
                                fps_den = (int)(out_frame.data.frame
                                                    .vui_num_units_in_tick);
                            } else
                            {
                                fps_num =
                                    (int)(out_frame.data.frame.vui_time_scale);
                                fps_den = (int)(2 *
                                                out_frame.data.frame
                                                    .vui_num_units_in_tick);
                            }
                        } else if (NI_CODEC_FORMAT_H265 ==
                                   out_frame.data.frame.src_codec)
                        {
                            fps_num = out_frame.data.frame.vui_time_scale;
                            fps_den =
                                out_frame.data.frame.vui_num_units_in_tick;
                        }
                    }

                    // set up encoder p_config, using some info from source
                    if (ni_encoder_init_default_params(
                            &api_param, fps_num, fps_den, 200000, arg_width,
                            arg_height, enc_ctx.codec_format) < 0)
                    {
                        fprintf(stderr,
                                "Error: encoder init default set up error\n");
                        break;
                    }

                    // check and set ni_encoder_params from --xcoder-params
                    // Note: the parameter setting has to be in this order so that user
                    //       configured values can overwrite the source/default ones if
                    //       desired.
                    if (retrieve_xcoder_params(encConfXcoderParams, &api_param,
                                               &enc_ctx))
                    {
                        fprintf(stderr,
                                "Error: encoder p_config parsing error\n");
                        break;
                    }

                    if (color_pri != api_param.color_primaries &&
                        NI_COL_PRI_UNSPECIFIED != api_param.color_primaries)
                    {
                        ni_log(NI_LOG_DEBUG, 
                            "Using user-configured color primaries %d to "
                            "overwrite source %d\n",
                            api_param.color_primaries, color_pri);
                        color_pri = api_param.color_primaries;
                    }
                    if (color_trc != api_param.color_transfer_characteristic &&
                        NI_COL_TRC_UNSPECIFIED !=
                            api_param.color_transfer_characteristic)
                    {
                        ni_log(NI_LOG_DEBUG, 
                            "Using user-configured color trc %d to overwrite"
                            " source %d\n",
                            api_param.color_transfer_characteristic, color_trc);
                        color_trc = api_param.color_transfer_characteristic;
                    }
                    if (color_space != api_param.color_space &&
                        NI_COL_SPC_UNSPECIFIED != api_param.color_space)
                    {
                        ni_log(NI_LOG_DEBUG, 
                            "Using user-configured color space %d to "
                            "overwrite source %d\n",
                            api_param.color_space, color_space);
                        color_space = api_param.color_space;
                    }
                    if (api_param.video_full_range_flag >= 0)
                    {
                        ni_log(NI_LOG_DEBUG, 
                            "Using user-configured video_full_range_flag "
                            "%d\n",
                            api_param.video_full_range_flag);
                        video_full_range_flag = api_param.video_full_range_flag;
                    }
                    if (out_frame.data.frame.aspect_ratio_idc > 0 &&
                        out_frame.data.frame.aspect_ratio_idc <
                            NI_NUM_PIXEL_ASPECT_RATIO)
                    {
                        sar_num =
                            ni_h264_pixel_aspect_list[out_frame.data.frame
                                                          .aspect_ratio_idc]
                                .num;
                        sar_den =
                            ni_h264_pixel_aspect_list[out_frame.data.frame
                                                          .aspect_ratio_idc]
                                .den;
                    } else if (api_param.sar_denom)
                    {
                        sar_num = api_param.sar_num;
                        sar_den = api_param.sar_denom;
                    }

                    ni_hrd_params_t hrd_params;
                    if (!dec_ctx.hw_action)
                    {
                        enc_ctx.hw_action = NI_CODEC_HW_NONE;
                    } else
                    {   //Items in this else condition could be improved by being handled in libxcoder
                        enc_ctx.hw_action = NI_CODEC_HW_ENABLE;
                        api_param.hwframes = 1;
                        //to determine if from same device and buffer dimensions in memory
                        //needs to be done where input frame is available to check
                        p_hwframe = out_frame.data.frame.p_data[3];
                        enc_ctx.sender_handle =
                            (ni_device_handle_t)(
                                int64_t)p_hwframe->device_handle;
                        api_param.rootBufId = p_hwframe->ui16FrameIdx;
                    }
                    if (0 !=
                        (ret = encoder_open_session(
                             &enc_ctx, dst_codec_format, iXcoderGUID,
                             &api_param, bit_depth, arg_width, arg_height,
                             &hrd_params, color_pri, color_trc, color_space,
                             video_full_range_flag, sar_num, sar_den, 1)))
                    {
                        ni_log(NI_LOG_ERROR, 
                            "Error: encoder_open_session failed, stop!\n");
                        break;
                    }
                }

            // encoded bitstream Receiving
            encode_send:
                need_to_resend = 0;
                // YUV Sending
                send_fin_flag = encoder_send_data2(
                    &enc_ctx, dec_ctx.codec_format, &out_frame, &enc_in_frame,
                    sos_flag, input_video_width, input_video_height, pfs,
                    total_file_size, &total_bytes_sent, &xcodeState);
                sos_flag = 0;
                if (send_fin_flag < 0)   //Error
                {
                    if (!enc_ctx.hw_action)
                    {
                        ni_decoder_frame_buffer_free(&(out_frame.data.frame));
                    } else
                    {
                        //pre close cleanup will clear it out
                        uint16_t current_hwframe_index =
                            ((niFrameSurface1_t *)((uint8_t *)out_frame.data
                                                       .frame.p_data[3]))
                                ->ui16FrameIdx;
                        memcpy(&hwframe_pool_tracker[current_hwframe_index],
                               (uint8_t *)out_frame.data.frame.p_data[3],
                               sizeof(niFrameSurface1_t));
                    }
                    break;
                } else if (send_fin_flag == 2)
                {
                    need_to_resend = 1;
                    goto encode_recv;
                }
                if (enc_ctx.hw_action)
                {   //track in array with unique index, free when enc read finds
                    //this must be implemented in application space for complete
                    //tracking of hwframes
                    //If encoder write  had no buffer avail space the next time we
                    //update the tracker will be redundant
                    uint16_t current_hwframe_index =
                        ((niFrameSurface1_t *)((uint8_t *)out_frame.data.frame
                                                   .p_data[3]))
                            ->ui16FrameIdx;
                    memcpy(&hwframe_pool_tracker[current_hwframe_index],
                           (uint8_t *)out_frame.data.frame.p_data[3],
                           sizeof(niFrameSurface1_t));
                    ni_frame_wipe_aux_data(
                        &(out_frame.data.frame));   //reusebuff
                } else
                {
                    ni_decoder_frame_buffer_free(&(out_frame.data.frame));
                }

            // encoded bitstream Receiving
            encode_recv:
                prev_num_pkt = number_of_packets;
                receive_fin_flag = encoder_receive_data(
                    &enc_ctx, &out_packet, output_video_width,
                    output_video_height, p_file, &xcodeRecvTotal, print_time);
                recycle_index = out_packet.data.packet.recycle_index;
                if (prev_num_pkt < number_of_packets &&   //skip if nothing read
                    enc_ctx.hw_action && recycle_index > 0 &&
                    recycle_index < NI_MAX_HWDESC_FRAME_INDEX)
                //encoder only returns valid recycle index
                //when there's something to recycle. This range is suitable for all memory bins
                {
                    if (hwframe_pool_tracker[recycle_index].ui16FrameIdx)
                    {
                        ni_hwframe_buffer_recycle(
                            &hwframe_pool_tracker[recycle_index],
                            hwframe_pool_tracker[recycle_index].device_handle);
                        //zero for tracking purposes
                        hwframe_pool_tracker[recycle_index].ui16FrameIdx = 0;
                    }
                }
                if (print_time)
                {
                    previous_time = current_time;
                }

                // Error or encoder eos
                if (receive_fin_flag < 0 ||
                    out_packet.data.packet.end_of_stream)
                {
                    break;
                }
            }
        }
        unsigned int time_diff =
            (unsigned int)(current_time.tv_sec - start_time.tv_sec);
        if (time_diff == 0)
            time_diff = 1;

        printf("[R] Got:  Frames= %u  fps=%f  Total bytes %llu\n",
               number_of_frames, ((float)number_of_frames / (float)time_diff),
               total_bytes_received);
        printf("[R] Got:  Packets= %u fps=%f  Total bytes %llu\n",
               number_of_packets,
               ((float)number_of_packets) / ((float)time_diff), xcodeRecvTotal);

        num_post_recycled = scan_and_clean_hwdescriptors(hwframe_pool_tracker);
        ni_log(NI_LOG_DEBUG, "Cleanup recycled %d internal buffers\n",
                       num_post_recycled);
        ni_device_session_close(&dec_ctx, 1, NI_DEVICE_TYPE_DECODER);

        ni_rsrc_free_device_context(sdPara.p_dec_rsrc_ctx);
        rcPara.p_dec_rsrc_ctx = NULL;
        sdPara.p_dec_rsrc_ctx = NULL;

        ni_packet_buffer_free(&(in_pkt.data.packet));
        ni_frame_buffer_free(&(out_frame.data.frame));

        ni_device_session_close(&enc_ctx, 1, NI_DEVICE_TYPE_ENCODER);

        ni_rsrc_free_device_context(sdPara.p_enc_rsrc_ctx);
        rcPara.p_enc_rsrc_ctx = NULL;
        sdPara.p_enc_rsrc_ctx = NULL;

        ni_frame_buffer_free(&(enc_in_frame.data.frame));
        ni_packet_buffer_free(&(out_packet.data.packet));
    }

end:

    ni_device_session_context_clear(&dec_ctx);
    ni_device_session_context_clear(&enc_ctx);
    ni_device_session_context_clear(&upl_ctx);

    close(pfs);
    if (p_file)
    {
        fclose(p_file);
    }

    free(g_file_cache);
    g_file_cache = NULL;

    printf("All Done.\n");

    return ret;
}
