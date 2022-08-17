/******************************************************************************
 *
 * Copyright (C) 2015 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at:
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 *****************************************************************************
 * Originally developed and contributed by Ittiam Systems Pvt. Ltd, Bangalore
*/
/*****************************************************************************/
/*                                                                           */
/*  File Name         : main.c                                               */
/*                                                                           */
/*  Description       : Contains an application that demonstrates use of H264*/
/*                      decoder API                                          */
/*                                                                           */
/*  List of Functions :                                                      */
/*                                                                           */
/*  Issues / Problems : None                                                 */
/*                                                                           */
/*  Revision History  :                                                      */
/*                                                                           */
/*         DD MM YYYY   Author(s)       Changes                              */
/*         07 09 2012   Harish          Initial Version                      */
/*****************************************************************************/
/*****************************************************************************/
/* File Includes                                                             */
/*****************************************************************************/
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include <malloc.h>

#include "ifwh264_typedefs.h"

#include "ifwv.h"
#include "ifwvd.h"
#include "ifwh264d.h"
#include "ifwthread.h"

#include <sys/time.h>

//#define ADAPTIVE_TEST
#define ADAPTIVE_MAX_WD 4096
#define ADAPTIVE_MAX_HT 2160

#define ALIGN8(x) ((((x) + 7) >> 3) << 3)
#define NUM_DISPLAY_BUFFERS 4
#define DEFAULT_FPS 30

#define MAX_DISP_BUFFERS 64
#define EXTRA_DISP_BUFFERS 8
#define STRLENGTH 1000

#define FLUSH_FRM_CNT 100
//#define APP_EXTRA_BUFS 1

typedef WORD32 TIMER;

#define GETTIME(timer)
#define ELAPSEDTIME(s_start_timer, s_end_timer, s_elapsed_time, frequency)

/* Function declarations */
#ifndef MD5_DISABLE
void calc_md5_cksum(UWORD8 *pu1_inbuf, UWORD32 u4_stride, UWORD32 u4_width,
                    UWORD32 u4_height, UWORD8 *pu1_cksum_p);
#else
#define calc_md5_cksum(a, b, c, d, e)
#endif

typedef struct
{
    UWORD32 u4_piclen_flag;
    UWORD32 u4_file_save_flag;
    UWORD32 u4_chksum_save_flag;
    UWORD32 u4_max_frm_ts;
    IV_COLOR_FORMAT_T e_output_chroma_format;
    IVD_ARCH_T e_arch;
    IVD_SOC_T e_soc;
    UWORD32 dump_q_rd_idx;
    UWORD32 dump_q_wr_idx;
    WORD32 disp_q_wr_idx;
    WORD32 disp_q_rd_idx;

    void *cocodec_obj;
    UWORD32 u4_share_disp_buf;
    UWORD32 num_disp_buf;
    UWORD32 b_pic_present;
    UWORD32 u4_disable_dblk_level;
    WORD32 i4_degrade_type;
    WORD32 i4_degrade_pics;
    UWORD32 u4_num_cores;
    UWORD32 disp_delay;
    WORD32 trace_enable;
    CHAR ac_trace_fname[STRLENGTH];
    CHAR ac_piclen_fname[STRLENGTH];
    CHAR ac_ip_fname[STRLENGTH];
    CHAR ac_op_fname[STRLENGTH];
    CHAR ac_op_chksum_fname[STRLENGTH];
    ivd_out_bufdesc_t s_disp_buffers[MAX_DISP_BUFFERS];
    iv_yuv_buf_t s_disp_frm_queue[MAX_DISP_BUFFERS];
    UWORD32 s_disp_frm_id_queue[MAX_DISP_BUFFERS];
    UWORD32 loopback;
    UWORD32 display;
    UWORD32 full_screen;
    UWORD32 fps;

    UWORD32 u4_strd;

    /* For signalling to display thread */
    UWORD32 u4_pic_wd;
    UWORD32 u4_pic_ht;

    //UWORD32 u4_output_present;
    WORD32 quit;
    WORD32 paused;

    void *pv_disp_ctx;
    void *display_thread_handle;
    WORD32 display_thread_created;
    volatile WORD32 display_init_done;
    volatile WORD32 display_deinit_flag;

    void *(*disp_init)(UWORD32, UWORD32, WORD32, WORD32, WORD32, WORD32, WORD32,
                       WORD32 *, WORD32 *);
    void (*alloc_disp_buffers)(void *);
    void (*display_buffer)(void *, WORD32);
    void (*set_disp_buffers)(void *, WORD32, UWORD8 **, UWORD8 **, UWORD8 **);
    void (*disp_deinit)(void *);
    void (*disp_usleep)(UWORD32);
    IV_COLOR_FORMAT_T (*get_color_fmt)(void);
    UWORD32 (*get_stride)(void);
} vid_dec_ctx_t;

typedef enum
{
    INVALID,
    HELP,
    VERSION,
    INPUT_FILE,
    OUTPUT,
    CHKSUM,
    SAVE_OUTPUT,
    SAVE_CHKSUM,
    CHROMA_FORMAT,
    NUM_FRAMES,
    NUM_CORES,
    DISABLE_DEBLOCK_LEVEL,
    SHARE_DISPLAY_BUF,
    LOOPBACK,
    DISPLAY,
    FULLSCREEN,
    FPS,
    TRACE,
    CONFIG,

    DEGRADE_TYPE,
    DEGRADE_PICS,
    ARCH,
    SOC,
    PICLEN,
    PICLEN_FILE,
} ARGUMENT_T;

typedef struct
{
    CHAR argument_shortname[4];
    CHAR argument_name[128];
    ARGUMENT_T argument;
    CHAR description[512];
} argument_t;

static const argument_t argument_mapping[] = {
    {"-h", "--help", HELP, "Print this help\n"},
    {"-c", "--config", CONFIG, "config file (Default: test.cfg)\n"},

    {"-v", "--version", VERSION, "Version information\n"},
    {"-i", "--input", INPUT_FILE, "Input file\n"},
    {"-o", "--output", OUTPUT, "Output file\n"},
    {"--", "--piclen", PICLEN,
     "Flag to signal if the decoder has to use a file containing number of "
     "bytes in each picture to be fed in each call\n"},
    {"--", "--piclen_file", PICLEN_FILE,
     "File containing number of bytes in each picture - each line containing "
     "one i4_size\n"},
    {"--", "--chksum", CHKSUM, "Output MD5 Checksum file\n"},
    {"-s", "--save_output", SAVE_OUTPUT, "Save Output file\n"},
    {"--", "--save_chksum", SAVE_CHKSUM, "Save Check sum file\n"},
    {"--", "--chroma_format", CHROMA_FORMAT,
     "Output Chroma format Supported values YUV_420P, YUV_422ILE, RGB_565, "
     "YUV_420SP_UV, YUV_420SP_VU\n"},
    {"-n", "--num_frames", NUM_FRAMES, "Number of frames to be decoded\n"},
    {"--", "--num_cores", NUM_CORES, "Number of cores to be used\n"},
    {"--", "--share_display_buf", SHARE_DISPLAY_BUF,
     "Enable shared display buffer mode\n"},
    {"--", "--disable_deblock_level", DISABLE_DEBLOCK_LEVEL,
     "Disable deblocking level : 0 to 4 - 0 Enable deblocking 4 Disable "
     "deblocking completely\n"},
    {"--", "--loopback", LOOPBACK, "Enable playback in a loop\n"},
    {"--", "--display", DISPLAY, "Enable display (uses SDL)\n"},
    {"--", "--fullscreen", FULLSCREEN,
     "Enable full screen (Only for GDL and SDL)\n"},
    {"--", "--fps", FPS, "FPS to be used for display \n"},
    {"-i", "--trace", TRACE, "Trace file\n"},

    {"--", "--degrade_type", DEGRADE_TYPE,
     "Degrade type : 0: No degrade 0th bit set : Disable SAO 1st bit set : "
     "Disable deblocking 2nd bit set : Faster inter prediction filters 3rd bit "
     "set : Fastest inter prediction filters\n"},
    {"--", "--degrade_pics", DEGRADE_PICS,
     "Degrade pics : 0 : No degrade  1 : Only on non-reference frames  2 : Do "
     "not degrade every 4th or key frames  3 : All non-key frames  4 : All "
     "frames"},

    {"--", "--arch", ARCH,
     "Set Architecture. Supported values  ARM_NONEON, ARM_A9Q, ARM_A7, ARM_A5, "
     "ARM_NEONINTR,ARMV8_GENERIC, X86_GENERIC, X86_SSSE3, X86_SSE4 \n"},
    {"--", "--soc", SOC, "Set SOC. Supported values  GENERIC, HISI_37X \n"},

};

#define PEAK_WINDOW_SIZE 8
#define DEFAULT_SHARE_DISPLAY_BUF 0
#define STRIDE 1280
#define SITIDE_HEIGHT 720
#define DEFAULT_NUM_CORES 1

#define DUMP_SINGLE_BUF 0
#define IV_ISFATALERROR(x) (((x) >> IVD_FATALERROR) & 0x1)

#define ivd_api_function ifwh264d_api_function

#if ANDROID_NDK
/*****************************************************************************/
/*                                                                           */
/*  Function Name : raise                                                    */
/*                                                                           */
/*  Description   : Needed as a workaround when the application is built in  */
/*                  Android NDK. This is an exception to be called for divide*/
/*                  by zero error                                            */
/*                                                                           */
/*  Inputs        : a                                                        */
/*  Globals       :                                                          */
/*  Processing    : None                                                     */
/*                                                                           */
/*  Outputs       :                                                          */
/*  Returns       :                                                          */
/*                                                                           */
/*  Issues        :                                                          */
/*                                                                           */
/*  Revision History:                                                        */
/*                                                                           */
/*         DD MM YYYY   Author(s)       Changes                              */
/*         07 09 2012   100189          Initial Version                      */
/*                                                                           */
/*****************************************************************************/
int raise(int a)
{
    printf("Divide by zero\n");
    return 0;
}
#endif

#if (!defined(IOS)) && (!defined(_WIN32))
void *ih264a_aligned_malloc(void *pv_ctxt, WORD32 alignment, WORD32 i4_size)
{
    (void)pv_ctxt;
    return memalign(alignment, i4_size);
}

void ih264a_aligned_free(void *pv_ctxt, void *pv_buf)
{
    (void)pv_ctxt;
    free(pv_buf);
    return;
}
#endif

/*****************************************************************************/
/*                                                                           */
/*  Function Name : release_disp_frame                                       */
/*                                                                           */
/*  Description   : Calls release display control - Used to signal to the    */
/*                  decoder that this particular buffer has been displayed   */
/*                  and that the codec is now free to write to this buffer   */
/*                                                                           */
/*                                                                           */
/*  Inputs        : codec_obj : Codec Handle                                 */
/*                  buf_id    : Buffer Id of the buffer to be released       */
/*                              This id would have been returned earlier by  */
/*                              the codec                                    */
/*  Globals       :                                                          */
/*  Processing    : Calls Release Display call                               */
/*                                                                           */
/*  Outputs       :                                                          */
/*  Returns       : Status of release display call                           */
/*                                                                           */
/*  Issues        :                                                          */
/*                                                                           */
/*  Revision History:                                                        */
/*                                                                           */
/*         DD MM YYYY   Author(s)       Changes                              */
/*         07 09 2012   100189          Initial Version                      */
/*                                                                           */
/*****************************************************************************/

IV_API_CALL_STATUS_T release_disp_frame(void *codec_obj, UWORD32 buf_id)
{
    ivd_rel_display_frame_ip_t s_video_rel_disp_ip;
    ivd_rel_display_frame_op_t s_video_rel_disp_op;
    IV_API_CALL_STATUS_T e_dec_status;

    s_video_rel_disp_ip.e_cmd = IVD_CMD_REL_DISPLAY_FRAME;
    s_video_rel_disp_ip.u4_size = sizeof(ivd_rel_display_frame_ip_t);
    s_video_rel_disp_op.u4_size = sizeof(ivd_rel_display_frame_op_t);
    s_video_rel_disp_ip.u4_disp_buf_id = buf_id;

    e_dec_status =
        ivd_api_function((iv_obj_t *)codec_obj, (void *)&s_video_rel_disp_ip,
                         (void *)&s_video_rel_disp_op);
    if (IV_SUCCESS != e_dec_status)
    {
        printf("Error in Release Disp frame\n");
    }

    return (e_dec_status);
}

/*****************************************************************************/
/*                                                                           */
/*  Function Name : get_version                                      */
/*                                                                           */
/*  Description   : Control call to get codec version              */
/*                                                                           */
/*                                                                           */
/*  Inputs        : codec_obj : Codec handle                                 */
/*  Globals       :                                                          */
/*  Processing    : Calls enable skip B frames control                       */
/*                                                                           */
/*  Outputs       :                                                          */
/*  Returns       : Control call return i4_status                               */
/*                                                                           */
/*  Issues        :                                                          */
/*                                                                           */
/*  Revision History:                                                        */
/*                                                                           */
/*         DD MM YYYY   Author(s)       Changes                              */
/*         07 09 2012   100189          Initial Version                      */
/*                                                                           */
/*****************************************************************************/

IV_API_CALL_STATUS_T get_version(void *codec_obj)
{
    ivd_ctl_getversioninfo_ip_t ps_ctl_ip;
    ivd_ctl_getversioninfo_op_t ps_ctl_op;
    UWORD8 au1_buf[512];
    IV_API_CALL_STATUS_T i4_status;
    ps_ctl_ip.e_cmd = IVD_CMD_VIDEO_CTL;
    ps_ctl_ip.e_sub_cmd = IVD_CMD_CTL_GETVERSION;
    ps_ctl_ip.u4_size = sizeof(ivd_ctl_getversioninfo_ip_t);
    ps_ctl_op.u4_size = sizeof(ivd_ctl_getversioninfo_op_t);
    ps_ctl_ip.pv_version_buffer = au1_buf;
    ps_ctl_ip.u4_version_buffer_size = sizeof(au1_buf);

    printf("\n ivd_api_function IVD_CMD_CTL_GETVERSION\n");

    i4_status = ivd_api_function((iv_obj_t *)codec_obj, (void *)&(ps_ctl_ip),
                                 (void *)&(ps_ctl_op));

    if (i4_status != IV_SUCCESS)
    {
        printf("Error in Getting Version number e_dec_status = %d "
               "u4_error_code = %x\n",
               i4_status, ps_ctl_op.u4_error_code);
    } else
    {
        printf("Ittiam Decoder Version number: %s\n",
               (char *)ps_ctl_ip.pv_version_buffer);
    }
    return i4_status;
}
/*****************************************************************************/
/*                                                                           */
/*  Function Name : codec_exit                                               */
/*                                                                           */
/*  Description   : handles unrecoverable errors                             */
/*  Inputs        : Error message                                            */
/*  Globals       : None                                                     */
/*  Processing    : Prints error message to console and exits.               */
/*  Outputs       : Error mesage to the console                              */
/*  Returns       : None                                                     */
/*                                                                           */
/*  Issues        :                                                          */
/*                                                                           */
/*  Revision History:                                                        */
/*                                                                           */
/*         DD MM YYYY   Author(s)       Changes (Describe the changes made)  */
/*         07 06 2006   Sankar          Creation                             */
/*                                                                           */
/*****************************************************************************/
void codec_exit(CHAR *pc_err_message)
{
    printf("%s\n", pc_err_message);
    exit(-1);
}

/*****************************************************************************/
/*                                                                           */
/*  Function Name : dump_output                                              */
/*                                                                           */
/*  Description   : Used to dump output YUV                                  */
/*  Inputs        : App context, disp output desc, File pointer              */
/*  Globals       : None                                                     */
/*  Processing    : Dumps to a file                                          */
/*  Returns       : None                                                     */
/*                                                                           */
/*  Issues        :                                                          */
/*                                                                           */
/*  Revision History:                                                        */
/*                                                                           */
/*         DD MM YYYY   Author(s)       Changes (Describe the changes made)  */
/*         07 06 2006   Sankar          Creation                             */
/*                                                                           */
/*****************************************************************************/
void dump_output(vid_dec_ctx_t *ps_app_ctx, iv_yuv_buf_t *ps_disp_frm_buf,
                 UWORD32 u4_disp_frm_id, FILE *ps_op_file,
                 FILE *ps_op_chksum_file, WORD32 i4_op_frm_ts,
                 UWORD32 file_save, UWORD32 chksum_save)

{
    UWORD32 i;
    iv_yuv_buf_t s_dump_disp_frm_buf;
    UWORD32 u4_disp_id;

    memset(&s_dump_disp_frm_buf, 0, sizeof(iv_yuv_buf_t));

    if (ps_app_ctx->u4_share_disp_buf)
    {
        if (ps_app_ctx->dump_q_wr_idx == MAX_DISP_BUFFERS)
            ps_app_ctx->dump_q_wr_idx = 0;

        if (ps_app_ctx->dump_q_rd_idx == MAX_DISP_BUFFERS)
            ps_app_ctx->dump_q_rd_idx = 0;

        ps_app_ctx->s_disp_frm_queue[ps_app_ctx->dump_q_wr_idx] =
            *ps_disp_frm_buf;
        ps_app_ctx->s_disp_frm_id_queue[ps_app_ctx->dump_q_wr_idx] =
            u4_disp_frm_id;
        ps_app_ctx->dump_q_wr_idx++;

        if ((WORD32)i4_op_frm_ts >= (WORD32)(ps_app_ctx->disp_delay - 1))
        {
            s_dump_disp_frm_buf =
                ps_app_ctx->s_disp_frm_queue[ps_app_ctx->dump_q_rd_idx];
            u4_disp_id =
                ps_app_ctx->s_disp_frm_id_queue[ps_app_ctx->dump_q_rd_idx];
            ps_app_ctx->dump_q_rd_idx++;
        } else
        {
            return;
        }
    } else
    {
        s_dump_disp_frm_buf = *ps_disp_frm_buf;
        u4_disp_id = u4_disp_frm_id;
    }

    release_disp_frame(ps_app_ctx->cocodec_obj, u4_disp_id);

    if (0 == file_save && 0 == chksum_save)
        return;

    if (NULL == s_dump_disp_frm_buf.pv_y_buf)
        return;

    if (ps_app_ctx->e_output_chroma_format == IV_YUV_420P)
    {
#if DUMP_SINGLE_BUF
        {
            UWORD8 *buf = s_dump_disp_frm_buf.pv_y_buf - 80 -
                (s_dump_disp_frm_buf.u4_y_strd * 80);

            UWORD32 i4_size = s_dump_disp_frm_buf.u4_y_strd *
                ((s_dump_disp_frm_buf.u4_y_ht + 160) +
                 (s_dump_disp_frm_buf.u4_u_ht + 80));
            fwrite(buf, 1, i4_size, ps_op_file);
        }
#else
        if (0 != file_save)
        {
            UWORD8 *buf;

            buf = (UWORD8 *)s_dump_disp_frm_buf.pv_y_buf;
            for (i = 0; i < s_dump_disp_frm_buf.u4_y_ht; i++)
            {
                fwrite(buf, 1, s_dump_disp_frm_buf.u4_y_wd, ps_op_file);
                buf += s_dump_disp_frm_buf.u4_y_strd;
            }

            buf = (UWORD8 *)s_dump_disp_frm_buf.pv_u_buf;
            for (i = 0; i < s_dump_disp_frm_buf.u4_u_ht; i++)
            {
                fwrite(buf, 1, s_dump_disp_frm_buf.u4_u_wd, ps_op_file);
                buf += s_dump_disp_frm_buf.u4_u_strd;
            }
            buf = (UWORD8 *)s_dump_disp_frm_buf.pv_v_buf;
            for (i = 0; i < s_dump_disp_frm_buf.u4_v_ht; i++)
            {
                fwrite(buf, 1, s_dump_disp_frm_buf.u4_v_wd, ps_op_file);
                buf += s_dump_disp_frm_buf.u4_v_strd;
            }
        }

        if (0 != chksum_save)
        {
            UWORD8 au1_y_chksum[16] = {0};
            UWORD8 au1_u_chksum[16] = {0};
            UWORD8 au1_v_chksum[16] = {0};
            calc_md5_cksum((UWORD8 *)s_dump_disp_frm_buf.pv_y_buf,
                           s_dump_disp_frm_buf.u4_y_strd,
                           s_dump_disp_frm_buf.u4_y_wd,
                           s_dump_disp_frm_buf.u4_y_ht, au1_y_chksum);
            calc_md5_cksum((UWORD8 *)s_dump_disp_frm_buf.pv_u_buf,
                           s_dump_disp_frm_buf.u4_u_strd,
                           s_dump_disp_frm_buf.u4_u_wd,
                           s_dump_disp_frm_buf.u4_u_ht, au1_u_chksum);
            calc_md5_cksum((UWORD8 *)s_dump_disp_frm_buf.pv_v_buf,
                           s_dump_disp_frm_buf.u4_v_strd,
                           s_dump_disp_frm_buf.u4_v_wd,
                           s_dump_disp_frm_buf.u4_v_ht, au1_v_chksum);

            fwrite(au1_y_chksum, sizeof(UWORD8), 16, ps_op_chksum_file);
            fwrite(au1_u_chksum, sizeof(UWORD8), 16, ps_op_chksum_file);
            fwrite(au1_v_chksum, sizeof(UWORD8), 16, ps_op_chksum_file);
        }
#endif
    } else if ((ps_app_ctx->e_output_chroma_format == IV_YUV_420SP_UV) ||
               (ps_app_ctx->e_output_chroma_format == IV_YUV_420SP_VU))
    {
#if DUMP_SINGLE_BUF
        {
            UWORD8 *buf = s_dump_disp_frm_buf.pv_y_buf - 24 -
                (s_dump_disp_frm_buf.u4_y_strd * 40);

            UWORD32 i4_size = s_dump_disp_frm_buf.u4_y_strd *
                ((s_dump_disp_frm_buf.u4_y_ht + 80) +
                 (s_dump_disp_frm_buf.u4_u_ht + 40));
            fwrite(buf, 1, i4_size, ps_op_file);
        }
#else
        {
            UWORD8 *buf;

            buf = (UWORD8 *)s_dump_disp_frm_buf.pv_y_buf;
            for (i = 0; i < s_dump_disp_frm_buf.u4_y_ht; i++)
            {
                fwrite(buf, 1, s_dump_disp_frm_buf.u4_y_wd, ps_op_file);
                buf += s_dump_disp_frm_buf.u4_y_strd;
            }

            buf = (UWORD8 *)s_dump_disp_frm_buf.pv_u_buf;
            for (i = 0; i < s_dump_disp_frm_buf.u4_u_ht; i++)
            {
                fwrite(buf, 1, s_dump_disp_frm_buf.u4_u_wd, ps_op_file);
                buf += s_dump_disp_frm_buf.u4_u_strd;
            }
        }
#endif
    } else if (ps_app_ctx->e_output_chroma_format == IV_RGBA_8888)
    {
        UWORD8 *buf;

        buf = (UWORD8 *)s_dump_disp_frm_buf.pv_y_buf;
        for (i = 0; i < s_dump_disp_frm_buf.u4_y_ht; i++)
        {
            fwrite(buf, 1, s_dump_disp_frm_buf.u4_y_wd * 4, ps_op_file);
            buf += s_dump_disp_frm_buf.u4_y_strd * 4;
        }
    } else
    {
        UWORD8 *buf;

        buf = (UWORD8 *)s_dump_disp_frm_buf.pv_y_buf;
        for (i = 0; i < s_dump_disp_frm_buf.u4_y_ht; i++)
        {
            fwrite(buf, 1, s_dump_disp_frm_buf.u4_y_strd * 2, ps_op_file);
            buf += s_dump_disp_frm_buf.u4_y_strd * 2;
        }
    }

    fflush(ps_op_file);
    fflush(ps_op_chksum_file);
}

/*****************************************************************************/
/*                                                                           */
/*  Function Name : print_usage                                              */
/*                                                                           */
/*  Description   : Prints argument format                                   */
/*                                                                           */
/*                                                                           */
/*  Inputs        :                                                          */
/*  Globals       :                                                          */
/*  Processing    : Prints argument format                                   */
/*                                                                           */
/*  Outputs       :                                                          */
/*  Returns       :                                                          */
/*                                                                           */
/*  Issues        :                                                          */
/*                                                                           */
/*  Revision History:                                                        */
/*                                                                           */
/*         DD MM YYYY   Author(s)       Changes                              */
/*         07 09 2012   100189          Initial Version                      */
/*                                                                           */
/*****************************************************************************/

void print_usage(void)
{
    WORD32 i = 0;
    WORD32 num_entries = sizeof(argument_mapping) / sizeof(argument_t);
    printf("\nUsage:\n");
    while (i < num_entries)
    {
        printf("%-32s\t %s", argument_mapping[i].argument_name,
               argument_mapping[i].description);
        i++;
    }
}

/*****************************************************************************/
/*                                                                           */
/*  Function Name : get_argument                                             */
/*                                                                           */
/*  Description   : Gets argument for a given string                         */
/*                                                                           */
/*                                                                           */
/*  Inputs        : name                                                     */
/*  Globals       :                                                          */
/*  Processing    : Searches the given string in the array and returns       */
/*                  appropriate argument ID                                  */
/*                                                                           */
/*  Outputs       : Argument ID                                              */
/*  Returns       : Argument ID                                              */
/*                                                                           */
/*  Issues        :                                                          */
/*                                                                           */
/*  Revision History:                                                        */
/*                                                                           */
/*         DD MM YYYY   Author(s)       Changes                              */
/*         07 09 2012   100189          Initial Version                      */
/*                                                                           */
/*****************************************************************************/

ARGUMENT_T get_argument(CHAR *name)
{
    WORD32 i = 0;
    WORD32 num_entries = sizeof(argument_mapping) / sizeof(argument_t);
    while (i < num_entries)
    {
        if ((0 == strcmp(argument_mapping[i].argument_name, name)) ||
            ((0 == strcmp(argument_mapping[i].argument_shortname, name)) &&
             (0 != strcmp(argument_mapping[i].argument_shortname, "--"))))
        {
            return argument_mapping[i].argument;
        }
        i++;
    }
    return INVALID;
}

/*****************************************************************************/
/*                                                                           */
/*  Function Name : get_argument                                             */
/*                                                                           */
/*  Description   : Gets argument for a given string                         */
/*                                                                           */
/*                                                                           */
/*  Inputs        : name                                                     */
/*  Globals       :                                                          */
/*  Processing    : Searches the given string in the array and returns       */
/*                  appropriate argument ID                                  */
/*                                                                           */
/*  Outputs       : Argument ID                                              */
/*  Returns       : Argument ID                                              */
/*                                                                           */
/*  Issues        :                                                          */
/*                                                                           */
/*  Revision History:                                                        */
/*                                                                           */
/*         DD MM YYYY   Author(s)       Changes                              */
/*         07 09 2012   100189          Initial Version                      */
/*                                                                           */
/*****************************************************************************/

void parse_argument(vid_dec_ctx_t *ps_app_ctx, CHAR *argument, CHAR *value)
{
    ARGUMENT_T arg;

    arg = get_argument(argument);
    printf("\nparse_argument %s\n", argument);
    switch (arg)
    {
        case HELP:
            print_usage();
            exit(-1);
        case VERSION:
            break;
        case INPUT_FILE:
            sscanf(value, "%999s", ps_app_ctx->ac_ip_fname);
            //input_passed = 1;
            break;

        case OUTPUT:
            sscanf(value, "%999s", ps_app_ctx->ac_op_fname);
            break;

        case CHKSUM:
            sscanf(value, "%999s", ps_app_ctx->ac_op_chksum_fname);
            break;

        case SAVE_OUTPUT:
            sscanf(value, "%d", &ps_app_ctx->u4_file_save_flag);
            break;

        case SAVE_CHKSUM:
            sscanf(value, "%d", &ps_app_ctx->u4_chksum_save_flag);
            break;

        case CHROMA_FORMAT:
            if ((strcmp(value, "YUV_420P")) == 0)
                ps_app_ctx->e_output_chroma_format = IV_YUV_420P;
            else if ((strcmp(value, "YUV_422ILE")) == 0)
                ps_app_ctx->e_output_chroma_format = IV_YUV_422ILE;
            else if ((strcmp(value, "RGB_565")) == 0)
                ps_app_ctx->e_output_chroma_format = IV_RGB_565;
            else if ((strcmp(value, "RGBA_8888")) == 0)
                ps_app_ctx->e_output_chroma_format = IV_RGBA_8888;
            else if ((strcmp(value, "YUV_420SP_UV")) == 0)
                ps_app_ctx->e_output_chroma_format = IV_YUV_420SP_UV;
            else if ((strcmp(value, "YUV_420SP_VU")) == 0)
                ps_app_ctx->e_output_chroma_format = IV_YUV_420SP_VU;
            else
            {
                printf("\nInvalid colour format setting it to IV_YUV_420P\n");
                ps_app_ctx->e_output_chroma_format = IV_YUV_420P;
            }

            break;
        case NUM_FRAMES:
            sscanf(value, "%d", &ps_app_ctx->u4_max_frm_ts);
            break;

        case NUM_CORES:
            sscanf(value, "%d", &ps_app_ctx->u4_num_cores);
            break;
        case DEGRADE_PICS:
            sscanf(value, "%d", &ps_app_ctx->i4_degrade_pics);
            break;
        case DEGRADE_TYPE:
            sscanf(value, "%d", &ps_app_ctx->i4_degrade_type);
            break;
        case SHARE_DISPLAY_BUF:
            sscanf(value, "%d", &ps_app_ctx->u4_share_disp_buf);
            break;
        case LOOPBACK:
            sscanf(value, "%d", &ps_app_ctx->loopback);
            break;
        case DISPLAY:
            ps_app_ctx->display = 0;
            break;
        case FULLSCREEN:
            sscanf(value, "%d", &ps_app_ctx->full_screen);
            break;
        case FPS:
            sscanf(value, "%d", &ps_app_ctx->fps);
            if (ps_app_ctx->fps <= 0)
                ps_app_ctx->fps = DEFAULT_FPS;
            break;
        case ARCH:
            if ((strcmp(value, "ARM_NONEON")) == 0)
                ps_app_ctx->e_arch = ARCH_ARM_NONEON;
            else if ((strcmp(value, "ARM_A9Q")) == 0)
                ps_app_ctx->e_arch = ARCH_ARM_A9Q;
            else if ((strcmp(value, "ARM_A7")) == 0)
                ps_app_ctx->e_arch = ARCH_ARM_A7;
            else if ((strcmp(value, "ARM_A5")) == 0)
                ps_app_ctx->e_arch = ARCH_ARM_A5;
            else if ((strcmp(value, "ARM_NEONINTR")) == 0)
                ps_app_ctx->e_arch = ARCH_ARM_NEONINTR;
            else if ((strcmp(value, "X86_GENERIC")) == 0)
                ps_app_ctx->e_arch = ARCH_X86_GENERIC;
            else if ((strcmp(value, "X86_SSSE3")) == 0)
                ps_app_ctx->e_arch = ARCH_X86_SSSE3;
            else if ((strcmp(value, "X86_SSE42")) == 0)
                ps_app_ctx->e_arch = ARCH_X86_SSE42;
            else if ((strcmp(value, "X86_AVX2")) == 0)
                ps_app_ctx->e_arch = ARCH_X86_AVX2;
            else if ((strcmp(value, "MIPS_GENERIC")) == 0)
                ps_app_ctx->e_arch = ARCH_MIPS_GENERIC;
            else if ((strcmp(value, "MIPS_32")) == 0)
                ps_app_ctx->e_arch = ARCH_MIPS_32;
            else if ((strcmp(value, "ARMV8_GENERIC")) == 0)
                ps_app_ctx->e_arch = ARCH_ARMV8_GENERIC;
            else
            {
                printf("\nInvalid Arch. Setting it to ARM_A9Q\n");
                ps_app_ctx->e_arch = ARCH_ARM_A9Q;
            }

            break;
        case SOC:
            if ((strcmp(value, "GENERIC")) == 0)
                ps_app_ctx->e_soc = SOC_GENERIC;
            else if ((strcmp(value, "HISI_37X")) == 0)
                ps_app_ctx->e_soc = SOC_HISI_37X;
            else
            {
                ps_app_ctx->e_soc = atoi(value);
                /*
                printf("\nInvalid SOC. Setting it to GENERIC\n");
                ps_app_ctx->e_soc = SOC_GENERIC;
*/
            }
            break;
        case PICLEN:
            sscanf(value, "%d", &ps_app_ctx->u4_piclen_flag);
            break;

        case PICLEN_FILE:
            sscanf(value, "%999s", ps_app_ctx->ac_piclen_fname);
            break;
        case DISABLE_DEBLOCK_LEVEL:
            sscanf(value, "%d", &ps_app_ctx->u4_disable_dblk_level);
            break;

        case INVALID:
        default:
            printf("Ignoring argument :  %s\n", argument);
            break;
    }
}

/*****************************************************************************/
/*                                                                           */
/*  Function Name : read_cfg_file                                            */
/*                                                                           */
/*  Description   : Reads arguments from a configuration file                */
/*                                                                           */
/*                                                                           */
/*  Inputs        : ps_app_ctx  : Application context                        */
/*                  fp_cfg_file : Configuration file handle                  */
/*  Globals       :                                                          */
/*  Processing    : Parses the arguments and fills in the application context*/
/*                                                                           */
/*  Outputs       : Arguments parsed                                         */
/*  Returns       : None                                                     */
/*                                                                           */
/*  Issues        :                                                          */
/*                                                                           */
/*  Revision History:                                                        */
/*                                                                           */
/*         DD MM YYYY   Author(s)       Changes                              */
/*         07 09 2012   100189          Initial Version                      */
/*                                                                           */
/*****************************************************************************/

void read_cfg_file(vid_dec_ctx_t *ps_app_ctx, FILE *fp_cfg_file)
{
    CHAR line[STRLENGTH];
    CHAR description[STRLENGTH];
    CHAR value[STRLENGTH];
    CHAR argument[STRLENGTH];
    void *ret;
    while (0 == feof(fp_cfg_file))
    {
        line[0] = '\0';
        ret = fgets(line, STRLENGTH, fp_cfg_file);
        if (NULL == ret)
            break;
        argument[0] = '\0';
        /* Reading Input File Name */
        sscanf(line, "%999s %999s %999s", argument, value, description);
        if (argument[0] == '\0')
            continue;

        parse_argument(ps_app_ctx, argument, value);
    }

    printf("\read_cfg_file success\n");
}

void flush_output(iv_obj_t *codec_obj, vid_dec_ctx_t *ps_app_ctx,
                  ivd_out_bufdesc_t *ps_out_buf, UWORD8 *pu1_bs_buf,
                  UWORD32 *pu4_op_frm_ts, FILE *ps_op_file,
                  FILE *ps_op_chksum_file, UWORD32 u4_ip_frm_ts,
                  UWORD32 u4_bytes_remaining)
{
    WORD32 ret;

    do
    {
        ivd_ctl_flush_ip_t s_ctl_ip;
        ivd_ctl_flush_op_t s_ctl_op;

        if (*pu4_op_frm_ts >=
            (ps_app_ctx->u4_max_frm_ts + ps_app_ctx->disp_delay))
            break;

        s_ctl_ip.e_cmd = IVD_CMD_VIDEO_CTL;
        s_ctl_ip.e_sub_cmd = IVD_CMD_CTL_FLUSH;
        s_ctl_ip.u4_size = sizeof(ivd_ctl_flush_ip_t);
        s_ctl_op.u4_size = sizeof(ivd_ctl_flush_op_t);
        ret = ivd_api_function((iv_obj_t *)codec_obj, (void *)&s_ctl_ip,
                               (void *)&s_ctl_op);

        if (ret != IV_SUCCESS)
        {
            printf("Error in Setting the decoder in flush mode\n");
        }

        if (IV_SUCCESS == ret)
        {
            ivd_video_decode_ip_t s_video_decode_ip;
            ivd_video_decode_op_t s_video_decode_op;

            s_video_decode_ip.e_cmd = IVD_CMD_VIDEO_DECODE;
            s_video_decode_ip.u4_ts = u4_ip_frm_ts;
            s_video_decode_ip.pv_stream_buffer = pu1_bs_buf;
            s_video_decode_ip.u4_num_Bytes = u4_bytes_remaining;
            s_video_decode_ip.u4_size = sizeof(ivd_video_decode_ip_t);
            s_video_decode_ip.s_out_buffer.u4_min_out_buf_size[0] =
                ps_out_buf->u4_min_out_buf_size[0];
            s_video_decode_ip.s_out_buffer.u4_min_out_buf_size[1] =
                ps_out_buf->u4_min_out_buf_size[1];
            s_video_decode_ip.s_out_buffer.u4_min_out_buf_size[2] =
                ps_out_buf->u4_min_out_buf_size[2];

            s_video_decode_ip.s_out_buffer.pu1_bufs[0] =
                ps_out_buf->pu1_bufs[0];
            s_video_decode_ip.s_out_buffer.pu1_bufs[1] =
                ps_out_buf->pu1_bufs[1];
            s_video_decode_ip.s_out_buffer.pu1_bufs[2] =
                ps_out_buf->pu1_bufs[2];
            s_video_decode_ip.s_out_buffer.u4_num_bufs =
                ps_out_buf->u4_num_bufs;

            s_video_decode_op.u4_size = sizeof(ivd_video_decode_op_t);

            /*****************************************************************************/
            /*   API Call: Video Decode                                                  */
            /*****************************************************************************/
            ret = ivd_api_function((iv_obj_t *)codec_obj,
                                   (void *)&s_video_decode_ip,
                                   (void *)&s_video_decode_op);

            if (1 == s_video_decode_op.u4_output_present)
            {
                CHAR cur_fname[1000];
                CHAR *extn = NULL;
                /* The objective is to dump the decoded frames into separate files instead of
                 * dumping all the frames in one common file. Also, the number of dumped frames
                 * at any given instance of time cannot exceed 'frame_memory'
                 */
                if (ps_app_ctx->u4_file_save_flag)
                {
                    /* Locate the position of extension yuv */
                    extn = strstr(ps_app_ctx->ac_op_fname, "%d");
                    if (extn != NULL)
                    {
                        /* Generate output file names */
                        sprintf(cur_fname, ps_app_ctx->ac_op_fname,
                                *pu4_op_frm_ts);
                        /* Open Output file */
                        ps_op_file = fopen(cur_fname, "wb");
                        if (NULL == ps_op_file)
                        {
                            CHAR ac_error_str[STRLENGTH];
                            sprintf(ac_error_str,
                                    "Could not open output file %s", cur_fname);

                            codec_exit(ac_error_str);
                        }
                    }
                }

                dump_output(ps_app_ctx, &(s_video_decode_op.s_disp_frm_buf),
                            s_video_decode_op.u4_disp_buf_id, ps_op_file,
                            ps_op_chksum_file, *pu4_op_frm_ts,
                            ps_app_ctx->u4_file_save_flag,
                            ps_app_ctx->u4_chksum_save_flag);
                if (extn != NULL)
                    fclose(ps_op_file);
                (*pu4_op_frm_ts)++;
            }
        }
    } while (IV_SUCCESS == ret);
}

UWORD32 default_get_stride(void)
{
    return 0;
}

IV_COLOR_FORMAT_T default_get_color_fmt(void)
{
    return IV_YUV_420P;
}
/*****************************************************************************/
/*                                                                           */
/*  Function Name : main                                                     */
/*                                                                           */
/*  Description   : Application to demonstrate codec API                     */
/*                                                                           */
/*                                                                           */
/*  Inputs        : argc    - Number of arguments                            */
/*                  argv[]  - Arguments                                      */
/*  Globals       :                                                          */
/*  Processing    : Shows how to use create, process, control and delete     */
/*                                                                           */
/*  Outputs       : Codec output in a file                                   */
/*  Returns       :                                                          */
/*                                                                           */
/*  Issues        : Assumes both PROFILE_ENABLE to be                        */
/*                  defined for multithread decode-display working           */
/*                                                                           */
/*  Revision History:                                                        */
/*                                                                           */
/*         DD MM YYYY   Author(s)       Changes                              */
/*         07 09 2012   100189          Initial Version                      */
/*         09 05 2013   100578          Multithread decode-display           */
/*****************************************************************************/

int main(WORD32 argc, CHAR *argv[])
{
    CHAR ac_cfg_fname[STRLENGTH];
    FILE *fp_cfg_file = NULL;
    FILE *ps_piclen_file = NULL;
    FILE *ps_ip_file = NULL;
    FILE *ps_op_file = NULL;
    FILE *ps_op_chksum_file = NULL;
    WORD32 ret;
    CHAR ac_error_str[STRLENGTH];
    vid_dec_ctx_t s_app_ctx;
    UWORD8 *pu1_bs_buf = NULL;

    ivd_out_bufdesc_t *ps_out_buf;
    UWORD32 file_pos = 0;

    UWORD32 u4_ip_frm_ts = 0, u4_op_frm_ts = 0;

    //WORD32 u4_bytes_remaining = 0;
    UWORD32 i;
    UWORD32 u4_ip_buf_len;
    //WORD32 total_bytes_comsumed;

    //WORD32 width = 0, height = 0;
    iv_obj_t *codec_obj;
#if defined(GPU_BUILD) && !defined(X86)
//    int ioctl_init();
//    ioctl_init();
#endif

    strcpy(ac_cfg_fname, "test.cfg");

    /***********************************************************************/
    /*                  Initialize Application parameters                  */
    /***********************************************************************/

    strcpy(s_app_ctx.ac_ip_fname, "\0");
    s_app_ctx.dump_q_wr_idx = 0;
    s_app_ctx.dump_q_rd_idx = 0;
    s_app_ctx.disp_q_wr_idx = 0;
    s_app_ctx.disp_q_rd_idx = 0;
    s_app_ctx.disp_delay = 0;
    s_app_ctx.loopback = 0;
    s_app_ctx.full_screen = 0;
    s_app_ctx.u4_piclen_flag = 0;
    s_app_ctx.fps = DEFAULT_FPS;
    file_pos = 0;
    //total_bytes_comsumed = 0;
    u4_ip_frm_ts = 0;
    u4_op_frm_ts = 0;

    s_app_ctx.u4_share_disp_buf = DEFAULT_SHARE_DISPLAY_BUF;
    s_app_ctx.u4_num_cores = DEFAULT_NUM_CORES;
    s_app_ctx.i4_degrade_type = 0;
    s_app_ctx.i4_degrade_pics = 0;
    s_app_ctx.e_arch = ARCH_ARM_A9Q;
    s_app_ctx.e_soc = SOC_GENERIC;

    s_app_ctx.u4_strd = STRIDE;

    s_app_ctx.quit = 0;
    s_app_ctx.paused = 0;
    //s_app_ctx.u4_output_present = 0;

    s_app_ctx.get_stride = &default_get_stride;

    s_app_ctx.get_color_fmt = &default_get_color_fmt;

    /* Set function pointers for display */

    s_app_ctx.display_deinit_flag = 0;
    s_app_ctx.e_output_chroma_format = IV_YUV_420SP_UV;
    /*************************************************************************/
    /* Parse arguments                                                       */
    /*************************************************************************/
    /* Read command line arguments */
    if (argc > 2)
    {
        for (i = 1; i < (UWORD32)argc; i += 2)
        {
            if (CONFIG == get_argument(argv[i]))
            {
                strcpy(ac_cfg_fname, argv[i + 1]);
                if ((fp_cfg_file = fopen(ac_cfg_fname, "r")) == NULL)
                {
                    sprintf(ac_error_str,
                            "Could not open Configuration file %s",
                            ac_cfg_fname);
                    codec_exit(ac_error_str);
                }
                read_cfg_file(&s_app_ctx, fp_cfg_file);
                fclose(fp_cfg_file);
            } else
            {
                parse_argument(&s_app_ctx, argv[i], argv[i + 1]);
            }
        }
    } else
    {
        if ((fp_cfg_file = fopen(ac_cfg_fname, "r")) == NULL)
        {
            sprintf(ac_error_str, "Could not open Configuration file %s",
                    ac_cfg_fname);
            codec_exit(ac_error_str);
        }
        read_cfg_file(&s_app_ctx, fp_cfg_file);
        fclose(fp_cfg_file);
    }

    printf("\ns_app_ctx.ac_ip_fname %s\n", s_app_ctx.ac_ip_fname);

    /* If display is enabled, then turn off shared mode and get color format that is supported by display */
    if (strcmp(s_app_ctx.ac_ip_fname, "\0") == 0)
    {
        printf("\nNo input file given for decoding\n");
        exit(-1);
    }

    /***********************************************************************/
    /*          create the file object for input file                      */
    /***********************************************************************/

    ps_ip_file = fopen(s_app_ctx.ac_ip_fname, "rb");
    if (NULL == ps_ip_file)
    {
        sprintf(ac_error_str, "Could not open input file %s",
                s_app_ctx.ac_ip_fname);
        codec_exit(ac_error_str);
    }
    /***********************************************************************/
    /*          create the file object for input file                      */
    /***********************************************************************/

    if (1 == s_app_ctx.u4_piclen_flag)
    {
        ps_piclen_file = fopen(s_app_ctx.ac_piclen_fname, "rb");
        if (NULL == ps_piclen_file)
        {
            sprintf(ac_error_str, "Could not open piclen file %s",
                    s_app_ctx.ac_piclen_fname);
            codec_exit(ac_error_str);
        }
    }

    /***********************************************************************/
    /*          create the file object for output file                     */
    /***********************************************************************/

    /* If the filename does not contain %d, then output will be dumped to
       a single file and it is opened here */
    if ((1 == s_app_ctx.u4_file_save_flag) &&
        (strstr(s_app_ctx.ac_op_fname, "%d") == NULL))
    {
        ps_op_file = fopen(s_app_ctx.ac_op_fname, "wb");
        if (NULL == ps_op_file)
        {
            sprintf(ac_error_str, "Could not open output file %s",
                    s_app_ctx.ac_op_fname);
            codec_exit(ac_error_str);
        }
    }

    /***********************************************************************/
    /*          create the file object for check sum file                  */
    /***********************************************************************/
    if (1 == s_app_ctx.u4_chksum_save_flag)
    {
        ps_op_chksum_file = fopen(s_app_ctx.ac_op_chksum_fname, "wb");
        if (NULL == ps_op_chksum_file)
        {
            sprintf(ac_error_str, "Could not open check sum file %s",
                    s_app_ctx.ac_op_chksum_fname);
            codec_exit(ac_error_str);
        }
    }
    /***********************************************************************/
    /*                      Create decoder instance                        */
    /***********************************************************************/
    {
        ps_out_buf = (ivd_out_bufdesc_t *)malloc(sizeof(ivd_out_bufdesc_t));

        /*****************************************************************************/
        /*   API Call: Initialize the Decoder                                        */
        /*****************************************************************************/
        {
            ifwh264d_create_ip_t s_create_ip;
            ifwh264d_create_op_t s_create_op;
            void *fxns = &ivd_api_function;

            s_create_ip.s_ivd_create_ip_t.e_cmd = IVD_CMD_CREATE;
            s_create_ip.s_ivd_create_ip_t.u4_share_disp_buf =
                s_app_ctx.u4_share_disp_buf;
            s_create_ip.s_ivd_create_ip_t.e_output_format =
                (IV_COLOR_FORMAT_T)s_app_ctx.e_output_chroma_format;
            s_create_ip.s_ivd_create_ip_t.pf_aligned_alloc =
                ih264a_aligned_malloc;
            s_create_ip.s_ivd_create_ip_t.pf_aligned_free = ih264a_aligned_free;
            s_create_ip.s_ivd_create_ip_t.pv_mem_ctxt = NULL;
            s_create_ip.s_ivd_create_ip_t.u4_size =
                sizeof(ifwh264d_create_ip_t);
            s_create_op.s_ivd_create_op_t.u4_size =
                sizeof(ifwh264d_create_op_t);

            printf("\n ivd_api_function IVD_CMD_CREATE\n");

            ret = ivd_api_function(NULL, (void *)&s_create_ip,
                                   (void *)&s_create_op);
            if (ret != IV_SUCCESS)
            {
                sprintf(ac_error_str, "Error in Create %8x\n",
                        s_create_op.s_ivd_create_op_t.u4_error_code);
                codec_exit(ac_error_str);
            }
            codec_obj = (iv_obj_t *)s_create_op.s_ivd_create_op_t.pv_handle;
            codec_obj->pv_fxns = fxns;
            codec_obj->u4_size = sizeof(iv_obj_t);
            s_app_ctx.cocodec_obj = codec_obj;
            printf("\n ivd_api_function IVD_CMD_CREATE Success\n");

            /*****************************************************************************/
            /*  set stride                                                               */
            /*****************************************************************************/

            printf("\n ivd_api_function set stride\n");

            {
                ivd_ctl_set_config_ip_t s_ctl_ip;
                ivd_ctl_set_config_op_t s_ctl_op;

                s_ctl_ip.u4_disp_wd = STRIDE;

                s_ctl_ip.e_frm_skip_mode = IVD_SKIP_NONE;
                s_ctl_ip.e_frm_out_mode = IVD_DISPLAY_FRAME_OUT;
                s_ctl_ip.e_vid_dec_mode = IVD_DECODE_HEADER;
                s_ctl_ip.e_cmd = IVD_CMD_VIDEO_CTL;
                s_ctl_ip.e_sub_cmd = IVD_CMD_CTL_SETPARAMS;
                s_ctl_ip.u4_size = sizeof(ivd_ctl_set_config_ip_t);
                s_ctl_op.u4_size = sizeof(ivd_ctl_set_config_op_t);

                printf("\n ivd_api_function IVD_CMD_CTL_SETPARAMS\n");

                ret = ivd_api_function((iv_obj_t *)codec_obj, (void *)&s_ctl_ip,
                                       (void *)&s_ctl_op);
                if (ret != IV_SUCCESS)
                {
                    sprintf(ac_error_str, "\nError in setting the stride");
                    codec_exit(ac_error_str);
                }
            }
        }
    }

    /*************************************************************************/
    /* set num of cores                                                      */
    /*************************************************************************/
    {
        printf("\n ivd_api_function num of cores\n");

        ifwh264d_ctl_set_num_cores_ip_t s_ctl_set_cores_ip;
        ifwh264d_ctl_set_num_cores_op_t s_ctl_set_cores_op;

        s_ctl_set_cores_ip.e_cmd = IVD_CMD_VIDEO_CTL;
        s_ctl_set_cores_ip.e_sub_cmd =
            (IVD_CONTROL_API_COMMAND_TYPE_T)IFWH264D_CMD_CTL_SET_NUM_CORES;
        s_ctl_set_cores_ip.u4_num_cores = s_app_ctx.u4_num_cores;
        s_ctl_set_cores_ip.u4_size = sizeof(ifwh264d_ctl_set_num_cores_ip_t);
        s_ctl_set_cores_op.u4_size = sizeof(ifwh264d_ctl_set_num_cores_op_t);

        printf("\n ivd_api_function IFWH264D_CMD_CTL_SET_NUM_CORES %d\n",
               s_ctl_set_cores_ip.u4_num_cores);

        ret =
            ivd_api_function((iv_obj_t *)codec_obj, (void *)&s_ctl_set_cores_ip,
                             (void *)&s_ctl_set_cores_op);
        if (ret != IV_SUCCESS)
        {
            sprintf(ac_error_str, "\nError in setting number of cores");
            codec_exit(ac_error_str);
        }
    }

    /*printf("\n ivd_api_function flush_output\n");

    flush_output(codec_obj, &s_app_ctx, ps_out_buf,
                 pu1_bs_buf, &u4_op_frm_ts,
                 ps_op_file, ps_op_chksum_file,
                 u4_ip_frm_ts, u4_bytes_remaining);*/

    /*****************************************************************************/
    /*   Decode header to get width and height and buffer sizes                  */
    /*****************************************************************************/
    {
        ivd_video_decode_ip_t s_video_decode_ip;
        ivd_video_decode_op_t s_video_decode_op;

        {
            ivd_ctl_set_config_ip_t s_ctl_ip;
            ivd_ctl_set_config_op_t s_ctl_op;

            s_ctl_ip.u4_disp_wd = STRIDE;

            s_ctl_ip.e_frm_skip_mode = IVD_SKIP_NONE;
            s_ctl_ip.e_frm_out_mode = IVD_DISPLAY_FRAME_OUT;
            s_ctl_ip.e_vid_dec_mode = IVD_DECODE_HEADER;
            s_ctl_ip.e_cmd = IVD_CMD_VIDEO_CTL;
            s_ctl_ip.e_sub_cmd = IVD_CMD_CTL_SETPARAMS;
            s_ctl_ip.u4_size = sizeof(ivd_ctl_set_config_ip_t);
            s_ctl_op.u4_size = sizeof(ivd_ctl_set_config_op_t);

            printf("\n ivd_api_function IVD_CMD_CTL_SETPARAMS\n");

            ret = ivd_api_function((iv_obj_t *)codec_obj, (void *)&s_ctl_ip,
                                   (void *)&s_ctl_op);
            if (ret != IV_SUCCESS)
            {
                sprintf(ac_error_str,
                        "\nError in setting the codec in header decode mode");
                codec_exit(ac_error_str);
            }
        }

        /* Allocate input buffer for header */
        u4_ip_buf_len = 256 * 1024;
        pu1_bs_buf = (UWORD8 *)malloc(u4_ip_buf_len);

        if (pu1_bs_buf == NULL)
        {
            sprintf(ac_error_str,
                    "\nAllocation failure for input buffer of i4_size %d",
                    u4_ip_buf_len);
            codec_exit(ac_error_str);
        }

        do
        {
            WORD32 numbytes;

            if (0 == s_app_ctx.u4_piclen_flag)
            {
                fseek(ps_ip_file, file_pos, SEEK_SET);
                numbytes = u4_ip_buf_len;
            } else
            {
                WORD32 entries;
                entries = fscanf(ps_piclen_file, "%d\n", &numbytes);
                if (1 != entries)
                    numbytes = u4_ip_buf_len;
            }

            WORD32 u4_bytes_remaining =
                fread(pu1_bs_buf, sizeof(UWORD8), numbytes, ps_ip_file);

            printf("\n ivd_api_function u4_bytes_remaining %d\n",
                   u4_bytes_remaining);

            if (0 == u4_bytes_remaining)
            {
                sprintf(ac_error_str, "\nUnable to read from input file");
                codec_exit(ac_error_str);
            }

            s_video_decode_ip.e_cmd = IVD_CMD_VIDEO_DECODE;
            s_video_decode_ip.u4_ts = u4_ip_frm_ts;
            s_video_decode_ip.pv_stream_buffer = pu1_bs_buf;
            s_video_decode_ip.u4_num_Bytes = u4_bytes_remaining;
            s_video_decode_ip.u4_size = sizeof(ivd_video_decode_ip_t);
            s_video_decode_op.u4_size = sizeof(ivd_video_decode_op_t);

            /*****************************************************************************/
            /*   API Call: Header Decode                                                  */
            /*****************************************************************************/

            printf("\n ivd_api_function IVD_CMD_VIDEO_DECODE HEAD\n");

            ret = ivd_api_function((iv_obj_t *)codec_obj,
                                   (void *)&s_video_decode_ip,
                                   (void *)&s_video_decode_op);

            if (ret != IV_SUCCESS)
            {
                printf("Error in header decode 0x%x\n",
                       s_video_decode_op.u4_error_code);
                // codec_exit(ac_error_str);
            }

            UWORD32 u4_num_bytes_dec = s_video_decode_op.u4_num_bytes_consumed;

            printf("u4_num_bytes_consumed %d\n",
                   s_video_decode_op.u4_num_bytes_consumed);

            file_pos += u4_num_bytes_dec;
            //total_bytes_comsumed += u4_num_bytes_dec;
        } while (ret != IV_SUCCESS);

        /* copy pic_wd and pic_ht to initialize buffers */
        s_app_ctx.u4_pic_wd = s_video_decode_op.u4_pic_wd;
        s_app_ctx.u4_pic_ht = s_video_decode_op.u4_pic_ht;

        printf(
            "s_video_decode_op.u4_pic_wd %d, s_video_decode_op.u4_pic_ht %d\n",
            s_video_decode_op.u4_pic_wd, s_video_decode_op.u4_pic_ht);

        //free(pu1_bs_buf);
        /* Create display thread and wait for the display buffers to be initialized */
    }

    /*************************************************************************/
    /* Get VUI parameters                                                    */
    /*************************************************************************/
    {
        ih264d_ctl_get_vui_params_ip_t s_ctl_get_vui_params_ip;
        ih264d_ctl_get_vui_params_op_t s_ctl_get_vui_params_op;

        s_ctl_get_vui_params_ip.e_cmd = IVD_CMD_VIDEO_CTL;
        s_ctl_get_vui_params_ip.e_sub_cmd =
            (IVD_CONTROL_API_COMMAND_TYPE_T)IH264D_CMD_CTL_GET_VUI_PARAMS;
        s_ctl_get_vui_params_ip.u4_size =
            sizeof(ih264d_ctl_get_vui_params_ip_t);
        s_ctl_get_vui_params_op.u4_size =
            sizeof(ih264d_ctl_get_vui_params_op_t);

        printf("\n ivd_api_function IH264D_CMD_CTL_GET_VUI_PARAMS\n");

        ret = ivd_api_function((iv_obj_t *)codec_obj,
                               (void *)&s_ctl_get_vui_params_ip,
                               (void *)&s_ctl_get_vui_params_op);
        if (IV_SUCCESS != ret)
        {
            printf("Error in Get VUI params\n");
            codec_exit(ac_error_str);
        }
    }

    get_version(codec_obj);

    while (1)
    {
        /*************************************************************************/
        /* set num of cores                                                      */
        /*************************************************************************/
        /***********************************************************************/
        /*   Seek the file to start of current frame, this is equavelent of    */
        /*   having a parcer which tells the start of current frame            */
        /***********************************************************************/
        WORD32 u4_bytes_remaining = 0;
        {
            WORD32 numbytes;

            if (0 == s_app_ctx.u4_piclen_flag)
            {
                fseek(ps_ip_file, file_pos, SEEK_SET);
                numbytes = u4_ip_buf_len;
            } else
            {
                WORD32 entries;
                entries = fscanf(ps_piclen_file, "%d\n", &numbytes);
                if (1 != entries)
                    numbytes = u4_ip_buf_len;
            }

            u4_bytes_remaining =
                fread(pu1_bs_buf, sizeof(UWORD8), numbytes, ps_ip_file);

            if (u4_bytes_remaining == 0)
            {
                if (1 == s_app_ctx.loopback)
                {
                    file_pos = 0;
                    if (0 == s_app_ctx.u4_piclen_flag)
                    {
                        fseek(ps_ip_file, file_pos, SEEK_SET);
                        numbytes = u4_ip_buf_len;
                    } else
                    {
                        WORD32 entries;
                        entries = fscanf(ps_piclen_file, "%d\n", &numbytes);
                        if (1 != entries)
                            numbytes = u4_ip_buf_len;
                    }

                    u4_bytes_remaining =
                        fread(pu1_bs_buf, sizeof(UWORD8), numbytes, ps_ip_file);
                } else
                    break;
            }
        }

        /*********************************************************************/
        /* Following calls can be enabled at diffent times                   */
        /*********************************************************************/

        {
            ivd_video_decode_ip_t s_video_decode_ip;
            ivd_video_decode_op_t s_video_decode_op;

            s_video_decode_ip.e_cmd = IVD_CMD_VIDEO_DECODE;
            s_video_decode_ip.u4_ts = u4_ip_frm_ts;
            s_video_decode_ip.pv_stream_buffer = pu1_bs_buf;
            s_video_decode_ip.u4_num_Bytes = u4_bytes_remaining;
            s_video_decode_ip.u4_size = sizeof(ivd_video_decode_ip_t);
            s_video_decode_ip.s_out_buffer.u4_min_out_buf_size[0] =
                ps_out_buf->u4_min_out_buf_size[0];
            s_video_decode_ip.s_out_buffer.u4_min_out_buf_size[1] =
                ps_out_buf->u4_min_out_buf_size[1];
            s_video_decode_ip.s_out_buffer.u4_min_out_buf_size[2] =
                ps_out_buf->u4_min_out_buf_size[2];

            s_video_decode_ip.s_out_buffer.pu1_bufs[0] =
                ps_out_buf->pu1_bufs[0];
            s_video_decode_ip.s_out_buffer.pu1_bufs[1] =
                ps_out_buf->pu1_bufs[1];
            s_video_decode_ip.s_out_buffer.pu1_bufs[2] =
                ps_out_buf->pu1_bufs[2];
            s_video_decode_ip.s_out_buffer.u4_num_bufs =
                ps_out_buf->u4_num_bufs;
            s_video_decode_op.u4_size = sizeof(ivd_video_decode_op_t);

            /* Get display buffer pointers */
            /*****************************************************************************/
            /*   API Call: Video Decode                                                  */
            /*****************************************************************************/

            printf("\n ivd_api_function IVD_CMD_VIDEO_DECODE DATA before "
                   "s_video_decode_ip.u4_num_Bytes %d\n",
                   s_video_decode_ip.u4_num_Bytes);
            printf("\n ivd_api_function IVD_CMD_VIDEO_DECODE DATA before "
                   "ps_out_buf->u4_min_out_buf_size[0] %d\n",
                   ps_out_buf->u4_min_out_buf_size[0]);
            printf("\n ivd_api_function IVD_CMD_VIDEO_DECODE DATA before "
                   "ps_out_buf->u4_min_out_buf_size[1] %d\n",
                   ps_out_buf->u4_min_out_buf_size[1]);
            printf("\n ivd_api_function IVD_CMD_VIDEO_DECODE DATA before "
                   "ps_out_buf->u4_min_out_buf_size[2] %d\n",
                   ps_out_buf->u4_min_out_buf_size[2]);
            printf("\n ivd_api_function IVD_CMD_VIDEO_DECODE DATA before "
                   "ps_out_buf->u4_num_bufs %d\n",
                   ps_out_buf->u4_num_bufs);

            GETTIME(&s_start_timer);

            ret = ivd_api_function((iv_obj_t *)codec_obj,
                                   (void *)&s_video_decode_ip,
                                   (void *)&s_video_decode_op);

            GETTIME(&s_end_timer);
            ELAPSEDTIME(s_start_timer, s_end_timer, s_elapsed_time, frequency);

            printf("\n ivd_api_function IVD_CMD_VIDEO_DECODE DATA after "
                   "s_video_decode_ip.u4_num_Bytes %d\n",
                   s_video_decode_ip.u4_num_Bytes);
            printf("\n ivd_api_function IVD_CMD_VIDEO_DECODE DATA after "
                   "ps_out_buf->u4_min_out_buf_size[0] %d\n",
                   ps_out_buf->u4_min_out_buf_size[0]);
            printf("\n ivd_api_function IVD_CMD_VIDEO_DECODE DATA after "
                   "ps_out_buf->u4_min_out_buf_size[1] %d\n",
                   ps_out_buf->u4_min_out_buf_size[1]);
            printf("\n ivd_api_function IVD_CMD_VIDEO_DECODE DATA after "
                   "ps_out_buf->u4_min_out_buf_size[2] %d\n",
                   ps_out_buf->u4_min_out_buf_size[2]);
            printf("\n ivd_api_function IVD_CMD_VIDEO_DECODE DATA after "
                   "ps_out_buf->u4_num_bufs %d\n",
                   ps_out_buf->u4_num_bufs);

            printf("IVD_CMD_VIDEO_DECODE "
                   "s_video_decode_op.u4_num_bytes_consumed %d\n",
                   s_video_decode_op.u4_num_bytes_consumed);

            if (ret != IV_SUCCESS)
            {
                printf("Error in video Frame decode : ret %x Error %x\n", ret,
                       s_video_decode_op.u4_error_code);
            }

            if (IV_B_FRAME == s_video_decode_op.e_pic_type)
                s_app_ctx.b_pic_present |= 1;

            UWORD32 u4_num_bytes_dec = s_video_decode_op.u4_num_bytes_consumed;

            file_pos += u4_num_bytes_dec;
            //total_bytes_comsumed += u4_num_bytes_dec;
            u4_ip_frm_ts++;
            printf("u4_ip_frm_ts:%d\n", u4_ip_frm_ts);

            printf(
                "IVD_CMD_VIDEO_DECODE s_video_decode_op.u4_output_present %d\n",
                s_video_decode_op.u4_output_present);

            if (1 == s_video_decode_op.u4_output_present)
            {
                CHAR cur_fname[1000];
                CHAR *extn = NULL;
                /* The objective is to dump the decoded frames into separate files instead of
                 * dumping all the frames in one common file. Also, the number of dumped frames
                 * at any given instance of time cannot exceed 'frame_memory'
                 */
                if (s_app_ctx.u4_file_save_flag)
                {
                    /* Locate the position of extension yuv */
                    extn = strstr(s_app_ctx.ac_op_fname, "%d");
                    if (extn != NULL)
                    {
                        /* Generate output file names */
                        sprintf(cur_fname, s_app_ctx.ac_op_fname, u4_op_frm_ts);
                        /* Open Output file */
                        ps_op_file = fopen(cur_fname, "wb");
                        if (NULL == ps_op_file)
                        {
                            sprintf(ac_error_str,
                                    "Could not open output file %s", cur_fname);

                            codec_exit(ac_error_str);
                        }
                    }
                }

                //width = s_video_decode_op.s_disp_frm_buf.u4_y_wd;
                //height = s_video_decode_op.s_disp_frm_buf.u4_y_ht;
                dump_output(&s_app_ctx, &(s_video_decode_op.s_disp_frm_buf),
                            s_video_decode_op.u4_disp_buf_id, ps_op_file,
                            ps_op_chksum_file, u4_op_frm_ts,
                            s_app_ctx.u4_file_save_flag,
                            s_app_ctx.u4_chksum_save_flag);

                u4_op_frm_ts++;
                if (extn != NULL)
                    fclose(ps_op_file);

            } else
            {
                if ((s_video_decode_op.u4_error_code >> IVD_FATALERROR) & 1)
                {
                    printf("Fatal error\n");
                    break;
                }
            }
        }

        break;
    }

    /***********************************************************************/
    /*      To get the last decoded frames, call process with NULL input    */
    /***********************************************************************/
    /*flush_output(codec_obj, &s_app_ctx, ps_out_buf,
                 pu1_bs_buf, &u4_op_frm_ts,
                 ps_op_file, ps_op_chksum_file,
                 u4_ip_frm_ts, u4_bytes_remaining);

    s_app_ctx.quit = 1;*/

    /***********************************************************************/
    /*   Clear the decoder, close all the files, free all the memory       */
    /***********************************************************************/

    {
        ivd_delete_ip_t s_delete_dec_ip;
        ivd_delete_op_t s_delete_dec_op;

        s_delete_dec_ip.e_cmd = IVD_CMD_DELETE;
        s_delete_dec_ip.u4_size = sizeof(ivd_delete_ip_t);
        s_delete_dec_op.u4_size = sizeof(ivd_delete_op_t);

        printf("\n ivd_api_function IVD_CMD_DELETE\n");

        ret = ivd_api_function((iv_obj_t *)codec_obj, (void *)&s_delete_dec_ip,
                               (void *)&s_delete_dec_op);

        if (IV_SUCCESS != ret)
        {
            sprintf(ac_error_str, "Error in Codec delete");
            codec_exit(ac_error_str);
        }
    }
    /***********************************************************************/
    /*              Close all the files and free all the memory            */
    /***********************************************************************/
    {
        fclose(ps_ip_file);

        if ((1 == s_app_ctx.u4_file_save_flag) &&
            (strstr(s_app_ctx.ac_op_fname, "%d") == NULL))
        {
            fclose(ps_op_file);
        }
        if (1 == s_app_ctx.u4_chksum_save_flag)
        {
            fclose(ps_op_chksum_file);
        }
    }

    if (0 == s_app_ctx.u4_share_disp_buf)
    {
        free(ps_out_buf->pu1_bufs[0]);
    }

    for (i = 0; i < s_app_ctx.num_disp_buf; i++)
    {
        free(s_app_ctx.s_disp_buffers[i].pu1_bufs[0]);
    }

    free(ps_out_buf);
    free(pu1_bs_buf);

    return (0);
}
