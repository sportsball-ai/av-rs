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
*   \file   ni_nvme.h
*
*	\brief  Definitions related to working with NI Quadra over NVME interface
*
*******************************************************************************/

#pragma once

#include "ni_defs.h"

#ifdef __cplusplus
extern "C"
{
#endif

#define NI_NVME_IDENTITY_CMD_DATA_SZ 4096

typedef struct _ni_nvme_command
{
    uint32_t cdw2;
    uint32_t cdw3;
    uint32_t cdw10;
    uint32_t cdw11;
    uint32_t cdw12;
    uint32_t cdw13;
    uint32_t cdw14;
    uint32_t cdw15;
} ni_nvme_command_t;

typedef struct _ni_nvme_id_power_state
{
  uint16_t ui16MaxPower; /*! centiwatts */
  uint8_t ui8Rsvd2;
  uint8_t ui8Flags;
  uint32_t ui32EntryLat; /*! microseconds */
  uint32_t ui32ExitLat;  /*! microseconds */
  uint8_t ui8ReadTput;
  uint8_t ui8ReadLat;
  uint8_t ui8WriteTput;
  uint8_t ui8WriteLat;
  uint16_t ui16IdlePower;
  uint8_t ui8IdleScale;
  uint8_t ui8Rsvd19;
  uint16_t ui16ActivePower;
  uint8_t ui8ActiveWorkScale;
  uint8_t aui8Rsvd23[9];
} ni_nvme_id_power_state_t;

#pragma pack(1)
typedef struct _ni_nvme_identity_xcoder_hw
{
    uint8_t hw_id;
    uint8_t hw_max_number_of_contexts;
    uint8_t hw_max_4k_fps;
    uint8_t hw_codec_format;
    uint8_t hw_codec_type;
    uint16_t hw_max_video_width;
    uint16_t hw_max_video_height;
    uint16_t hw_min_video_width;
    uint16_t hw_min_video_height;
    uint8_t hw_video_profile;
    uint8_t hw_video_level;
    uint8_t hw_reserved[9];
} ni_nvme_identity_xcoder_hw_t;
#pragma pack()

typedef struct _ni_nvme_identity
{
  // NVMe identify controller data structure, p_first 3K bytes of general
  // controller capabilities and features, copied from f/w nvme.h
  uint16_t ui16Vid;    //PCI Vendor ID
  uint16_t ui16Ssvid;  //PCI Subsystem Vendor ID
  uint8_t ai8Sn[20];   //serial Number, it is a space right filled ASCII array(not a string)
  uint8_t ai8Mn[40];   //Model number, it is a space right filled ASCII array(not a string)
  uint8_t ai8Fr[8];    //Firmware Revision, it is a space right filled ASCII array(not a string)
  uint8_t ui8Rab;      //Recommend Arbitration Burst
  uint8_t aui8Ieee[3]; //IEEE OUI Identifier
  uint8_t ui8Cmic;     //controller multi-path I/O and namespace sharing Capabilities
  uint8_t ui8Mdts;     //Maximum data transfer size
  uint16_t ui16Cntlid; //Controller ID  Savio: should be 2 bytes
  uint32_t ui32Ver;    //Version
  uint32_t ui32Rtd3r;  //RTD3 resume latency
  uint32_t ui32Rtd3e;  //RTD3 entry Latency
  uint32_t ui32Oaes;   //optional Asynchronous events supported
  uint8_t aui8Rsvd96[160];
  uint16_t ui16Oacs;        //optional Admin Command Support
  uint8_t ui8Acl;           //Abort command Limit - 0's based value
  uint8_t ui8Aerl;          //Asynchronous Event Request Limit - 0's based value
  uint8_t ui8Frmw;          //Firmware updates
  uint8_t ui8Lpa;           //Log Page Attributes
  uint8_t ui8Elpe;          //Error Log Page Entries - 0's based value
  uint8_t ui8Npss;          //number of Power states support - 0's based value
  uint8_t ui8Avscc;         //Admin Vendor Specific Command Configuration
  uint8_t ui8Apsta;         //Autonomous power state transition attributes
  uint16_t ui16Wctemp;      //Warning Composite Temperature Threshold
  uint16_t ui16Cctemp;      //Critical Composite Temperature Threshold
  uint16_t ui16Mtfa;        //Maximum Time for Firmware Activation
  uint32_t ui32Hmpre;       //Host Memory Buffer Preferred Size
  uint32_t ui32Hmmin;       //Host Memory Buffer Minimum Size
  uint8_t aui32Tnvmcap[16]; //Total NVM Capacity
  uint8_t aui8Unvmcap[16];  //unallocated NVM Capacity
  uint32_t ui32Rpmbs;       //Replay Protected Memory Block Support
  uint8_t aui8Rsvd316[196];
  uint8_t ui8Sqes; //Submission Queue Entry Size
  uint8_t ui8Cqes; //Completion Queue Entry Size
  uint8_t ui8Rsvd514[2];
  uint32_t ui32Nn;    //Number of Namespaces
  uint16_t ui16Oncs;  //Optional NVM Command Support
  uint16_t ui16Fuses; //Fused Operation Support
  uint8_t ui8Fna;     //Format NVM Attributes
  uint8_t ui8Vwc;     //Volatile write cache
  uint16_t ui16Awun;  //Atomic Write Unit Normal - 0's based value
  uint16_t ui16Awupf; //Atomic Write Unit Power Fail - 0's based value
  uint8_t ui8Nvscc;   //NVM Vendor Specific Command Configuration
  uint8_t ui8Rsvd531;
  uint16_t ui16Acwu; //Atomic Compare & write Unit - 0's based value
  uint8_t aui8Rsvd534[2];
  uint32_t ui32Sgls; //SGL Support
  uint8_t aui8Rsvd540[1508];
  ni_nvme_id_power_state_t asPsd[32]; //Power state Descriptors

  // Below are vendor-specific parameters
  uint8_t aui8TotalRawCap[8]; // total raw capacity in the number of 4K
  uint8_t ui8CurPcieLnkSpd;   // current PCIe link speed
  uint8_t ui8NegPcieLnkWid;   // negotiated PCIe link width
  uint16_t ui16ChipVer;       // chip version in binary
  uint8_t aui8FwLoaderRev[8]; // firmware loader revision in ASCII
  uint8_t ui8NbFlashChan;     // number of flash channels (1-32)
  uint8_t ui8RAIDsupport;     // RAID support. 1: supported 0: not

  // Below is xcoder part

  uint8_t device_is_xcoder;
  uint8_t sed_support;

  // xcoder HW - version 1
  uint8_t xcoder_num_hw;
  uint8_t xcoder_num_h264_decoder_hw;
  uint8_t xcoder_num_h264_encoder_hw;
  uint8_t xcoder_num_h265_decoder_hw;
  uint8_t xcoder_num_h265_encoder_hw;
  uint8_t xcoder_reserved[11];

  uint8_t hw0_id;
  uint8_t hw0_max_number_of_contexts;
  uint8_t hw0_max_1080p_fps;
  uint8_t hw0_codec_format;
  uint8_t hw0_codec_type;
  uint16_t hw0_max_video_width;
  uint16_t hw0_max_video_height;
  uint16_t hw0_min_video_width;
  uint16_t hw0_min_video_height;
  uint8_t hw0_video_profile;
  uint8_t hw0_video_level;
  uint8_t hw0_reserved[9];

  uint8_t hw1_id;
  uint8_t hw1_max_number_of_contexts;
  uint8_t hw1_max_1080p_fps;
  uint8_t hw1_codec_format;
  uint8_t hw1_codec_type;
  uint16_t hw1_max_video_width;
  uint16_t hw1_max_video_height;
  uint16_t hw1_min_video_width;
  uint16_t hw1_min_video_height;
  uint8_t hw1_video_profile;
  uint8_t hw1_video_level;
  uint8_t hw1_reserved[9];

  uint8_t hw2_id;
  uint8_t hw2_max_number_of_contexts;
  uint8_t hw2_max_1080p_fps;
  uint8_t hw2_codec_format;
  uint8_t hw2_codec_type;
  uint16_t hw2_max_video_width;
  uint16_t hw2_max_video_height;
  uint16_t hw2_min_video_width;
  uint16_t hw2_min_video_height;
  uint8_t hw2_video_profile;
  uint8_t hw2_video_level;
  uint8_t hw2_reserved[9];

  uint8_t hw3_id;
  uint8_t hw3_max_number_of_contexts;
  uint8_t hw3_max_1080p_fps;
  uint8_t hw3_codec_format;
  uint8_t hw3_codec_type;
  uint16_t hw3_max_video_width;
  uint16_t hw3_max_video_height;
  uint16_t hw3_min_video_width;
  uint16_t hw3_min_video_height;
  uint8_t hw3_video_profile;
  uint8_t hw3_video_level;
  uint8_t hw3_reserved[9];

  uint8_t fw_branch_name[256];
  uint8_t fw_commit_time[26];
  uint8_t fw_commit_hash[41];
  uint8_t fw_build_time[26];
  uint8_t fw_build_id[256];
  uint8_t fw_repo_info_padding[2];

  uint8_t memory_cfg; // 0 == DR, 1 == SR
  // byte offset 469 (=468+1 due to alignment at hw0_max_video_width)

  // xcoder HW - version 2 (replaces/deprecates version 1)
  uint8_t xcoder_num_elements;   // total element types
  uint8_t xcoder_num_devices;    // total devices
  uint8_t xcoder_cnt[14];        // device count per type
  ni_nvme_identity_xcoder_hw_t xcoder_devices[NI_MAX_DEVICES_PER_HW_INSTANCE];
} ni_nvme_identity_t;

#ifdef __linux__

typedef struct _ni_nvme_user_io
{
  __u8 opcode;
  __u8 flags;
  __u16 control;
  __u16 nblocks;
  __u16 rsvd;
  __u64 metadata;
  __u64 addr;
  __u64 slba;
  __u32 dsmgmt;
  __u32 reftag;
  __u16 apptag;
  __u16 appmask;
} ni_nvme_user_io_t;

typedef struct _ni_nvme_passthrough_cmd
{
  __u8 opcode;
  __u8 flags;
  __u16 rsvd1;
  __u32 nsid;
  __u32 cdw2;
  __u32 cdw3;
  __u64 metadata;
  __u64 addr;
  __u32 metadata_len;
  __u32 data_len;
  __u32 cdw10;
  __u32 cdw11;
  __u32 cdw12;
  __u32 cdw13;
  __u32 cdw14;
  __u32 cdw15;
  __u32 timeout_ms;
  __u32 result; //DW0
}ni_nvme_passthrough_cmd_t;

typedef ni_nvme_passthrough_cmd_t ni_nvme_admin_cmd_t;

#define NVME_IOCTL_ID _IO('N', 0x40)
#define NVME_IOCTL_ADMIN_CMD _IOWR('N', 0x41, ni_nvme_admin_cmd_t)
#define NVME_IOCTL_SUBMIT_IO _IOW('N', 0x42, ni_nvme_user_io_t)
#define NVME_IOCTL_IO_CMD _IOWR('N', 0x43, ni_nvme_passthrough_cmd_t)
#define NVME_IOCTL_RESET _IO('N', 0x44)
#define NVME_IOCTL_SUBSYS_RESET _IO('N', 0x45)
#define NVME_IOCTL_RESCAN _IO('N', 0x46)

#endif //__linux__ defined

#ifdef _WIN32
typedef struct _ni_nvme_completion_result
{
	uint32_t    ui32Result;    /*! Used by admin commands to return data */
	uint32_t    ui32Rsvd;
	uint16_t    ui16SqHead;    /*! how much of this queue may be reclaimed */
	uint16_t    ui16SqId;      /*! submission queue that generated this entry */
	uint16_t    ui16CommandId; /*! of the command which completed */
	uint16_t    ui16Status;    /*! did the command fail, and if so, why? */
}ni_nvme_completion_result_t, *p_ni_nvme_completion_result_t;
#endif

typedef enum _ni_nvme_admin_opcode
{
  nvme_admin_cmd_delete_sq = 0x00,
  nvme_admin_cmd_create_sq = 0x01,
  nvme_admin_cmd_get_log_page = 0x02,
  nvme_admin_cmd_delete_cq = 0x04,
  nvme_admin_cmd_create_cq = 0x05,
  nvme_admin_cmd_identify = 0x06,
  nvme_admin_cmd_abort_cmd = 0x08,
  nvme_admin_cmd_set_features = 0x09,
  nvme_admin_cmd_get_features = 0x0a,
  nvme_admin_cmd_async_event = 0x0c,
  nvme_admin_cmd_ns_mgmt = 0x0d,
  nvme_admin_cmd_activate_fw = 0x10,
  nvme_admin_cmd_download_fw = 0x11,
  nvme_admin_cmd_ns_attach = 0x15,
  nvme_admin_cmd_format_nvm = 0x80,
  nvme_admin_cmd_security_send = 0x81,
  nvme_admin_cmd_security_recv = 0x82,
  nvme_admin_cmd_xcoder_open    = 0xD0,
  nvme_admin_cmd_xcoder_close   = 0xD1,
  nvme_admin_cmd_xcoder_query   = 0xD2,
  nvme_admin_cmd_xcoder_connect = 0xD3,
  nvme_admin_cmd_xcoder_read   = 0xD4,
  nvme_admin_cmd_xcoder_write   = 0xD5,
  nvme_admin_cmd_xcoder_config  = 0xD6,
  nvme_admin_cmd_xcoder_recycle_buffer = 0xD7,
  nvme_admin_cmd_xcoder_init_framepool = 0xD8,
  nvme_admin_cmd_xcoder_identity = 0xD9,
} ni_nvme_admin_opcode_t;


typedef enum _nvme_open_xcoder_subtype
{
  nvme_open_xcoder_create_session = 0x0000,
  nvme_open_xcoder_add_session = 0x0001
} nvme_open_xcoder_subtype_t;

typedef enum _nvme_close_xcoder_subtype
{
  nvme_close_xcoder_destroy_session = 0x0000
} nvme_close_xcoder_subtype_t;

typedef enum _nvme_read_xcoder_subtype
{
  nvme_read_xcoder_read_instance = 0x0001
} nvme_read_xcoder_subtype_t;

typedef enum _nvme_write_xcoder_subtype
{
  nvme_write_xcoder_write_instance = 0x0001
} nvme_write_xcoder_subtype_t;

typedef enum _nvme_query_xcoder_subtype
{
  nvme_query_xcoder_query_session = 0x0000,
  nvme_query_xcoder_query_instance = 0x0001,
  nvme_query_xcoder_query_general = 0x0002
} nvme_query_xcoder_subtype_t;

typedef enum _nvme_query_xcoder_session_subtype
{
  nvme_query_xcoder_session_get_stats = 0x0000,
} nvme_query_xcoder_session_subtype_t;

typedef enum _nvme_query_xcoder_instance_subtype
{
    nvme_query_xcoder_instance_get_status = 0x0002,
    nvme_query_xcoder_instance_get_stream_info = 0x0004,
    nvme_query_xcoder_instance_get_end_of_output = 0x0005,
    nvme_query_xcoder_instance_acquire_buf = 0x0008,
    nvme_query_xcoder_instance_read_buf_size = 0x0009,
    nvme_query_xcoder_instance_write_buf_size = 0x000a,
    nvme_query_xcoder_instance_upload_idx = 0x000b,
    nvme_query_xcoder_instance_network_layer_size = 0x000b,
    nvme_query_xcoder_instance_dec_place_holder =
        0x000c,   //config_set_write_length
    nvme_query_xcoder_instance_read_network_layer = 0x000c,
    nvme_query_xcoder_instance_scl_place_holder = 0x000d,   //config_alloc_frame
    nvme_query_xcoder_instance_read_buf_size_busy = 0x000e,
    nvme_query_xcoder_instance_write_buf_size_busy = 0x000f,
} nvme_query_xcoder_instance_subtype_t;

typedef enum _nvme_query_xcoder_general_subtype
{
  nvme_query_xcoder_general_get_status = 0x0002,
} nvme_query_xcoder_general_subtype_t;

typedef enum _nvme_config_xcoder_subtype
{
  nvme_config_xcoder_config_session = 0x0000,
  nvme_config_xcoder_config_instance = 0x0001
} nvme_config_xcoder_subtype_t;

typedef enum _nvme_config_xcoder_config_session_subtype
{
    nvme_config_xcoder_config_session_keep_alive = 0x0000,
    nvme_config_xcoder_config_session_read = 0x0001,
    nvme_config_xcoder_config_session_write = 0x0002,
    nvme_config_xcoder_config_session_keep_alive_timeout = 0x0003,
    nvme_config_xcoder_config_session_sw_version = 0x0004,
} nvme_config_xcoder_config_session_subtype_t;

typedef enum _nvme_config_xcoder_config_instance_subtype
{
    nvme_config_xcoder_config_set_sos = 0x0000,
    nvme_config_xcoder_config_set_eos = 0x0001,
    nvme_config_xcoder_config_set_enc_params = 0x0005,
    nvme_config_xcoder_config_set_dec_params = 0x0005,
    nvme_config_xcoder_config_set_scaler_params = 0x0005,
    nvme_config_xcoder_config_flush = 0x0007,
    nvme_config_xcoder_config_update_enc_params = 0x0008,
    nvme_config_xcoder_config_set_network_binary = 0x0008,
    nvme_config_xcoder_config_set_enc_frame_params = 0x0009,
    nvme_config_xcoder_config_set_write_legth = 0x000c,
    nvme_config_xcoder_config_alloc_frame = 0x000d,  // scaler only
    nvme_config_xcoder_config_set_sequence_change = 0x000d,   // encoder only
    nvme_config_xcoder_instance_read_buf_size_busy_place_holder = 0x000e,   // admin type taken by busy query read
    nvme_config_xcoder_instance_write_buf_size_busy_place_holder = 0x000f,  // admin type taken by busy query write
} nvme_config_xcoder_config_instance_subtype_t;

typedef struct _ni_nvme_write_complete_dw0_t
{

	uint32_t available_space : 24;
	uint32_t frame_index : 4;
	uint32_t reserved : 4;

} ni_nvme_write_complete_dw0_t;


typedef uint32_t ni_nvme_result_t;


#if (PLATFORM_ENDIANESS == NI_BIG_ENDIAN_PLATFORM)
static inline uint64_t ni_htonll(uint64_t val)
{
  if (1 == htonl(1))
    return val;

  return ((((uint64_t)htonl((val)&0xFFFFFFFFUL)) << 32) | htonl((uint32_t)((val) >> 32)));
}

static inline uint64_t ni_ntohll(uint64_t val)
{
  if (1 == ntohl(1))
    return val;

  return ((((uint64_t)ntohl((val)&0xFFFFFFFFUL)) << 32) | ntohl((uint32_t)((val) >> 32)));
}

static inline uint32_t ni_htonl(uint32_t val)
{
  return htonl(val);
}
static inline uint16_t ni_htons(uint16_t val)
{
  return htons(val);
}
static inline uint32_t ni_ntohl(uint32_t val)
{
  return ntohl(val);
}
static inline uint16_t ni_ntohs(uint16_t val)
{
  return ntohs(val);
}
#else
static inline uint64_t ni_ntohll(uint64_t val)
{
  return (val);
}
static inline uint64_t ni_htonll(uint64_t val)
{
  return (val);
}
static inline uint32_t ni_htonl(uint32_t val)
{
  return (val);
}
static inline uint16_t ni_htons(uint16_t val)
{
  return (val);
}
static inline uint32_t ni_ntohl(uint32_t val)
{
  return (val);
}
static inline uint16_t ni_ntohs(uint16_t val)
{
  return (val);
}
#endif

#define WRITE_INSTANCE_SET_DW2_SUBFRAME_IDX(dst, size) (dst = (size & 0xFFFFFFFFUL))
#define WRITE_INSTANCE_SET_DW3_SUBFRAME_SIZE(dst, size) (dst = (size & 0xFFFFFFFFUL))

#define CREATE_SESSION_SET_DW10_SUBTYPE(dst) (dst = (nvme_open_xcoder_create_session & 0xFFFFUL))
#define CREATE_SESSION_SET_DW11_INSTANCE(dst, instance) (dst = (instance & 0xFFFFUL))
#define CREATE_SESSION_SET_DW12_DEC_CID(dst, cid) (dst = (cid & 0xFFFFUL))
#define CREATE_SESSION_SET_DW12_DEC_HWF(dst, hwf) (dst = (((hwf << 16) & 0xFFFF0000UL) | (dst & 0xFFFFUL)))

#define CREATE_SESSION_SET_DW12_SCL_OPC(dst, opc) (dst = (opc & 0xFFFFUL))

#define CREATE_SESSION_SET_DW12_ENC_CID_FRWIDTH(dst, cid, width) (dst = (((width << 16) & 0xFFFF0000UL) | (cid & 0xFFFFUL)))
#define CREATE_SESSION_SET_DW13_ENC_FRHIGHT(dst, hight) (dst = (hight & 0xFFFFUL))
#define CREATE_SESSION_SET_DW14_MODEL_LOAD(dst, load) (dst = (load & 0xFFFFFFFFUL))

#define CREATE_SESSION_SET_DW12_AI_HWF(dst, hwf)                               \
    (dst = (((hwf << 16) & 0xFFFF0000UL) | (dst & 0xFFFFUL)))

#define DESTROY_SESSION_SET_DW10_INSTANCE(dst, sid) (dst = (((sid << 16) & 0xFFFF0000UL) | (nvme_close_xcoder_destroy_session & 0xFFFFUL)))

#define READ_INSTANCE_SET_DW10_SUBTYPE(dst, sid) (dst = (((sid << 16) & 0xFFFF0000UL) | (nvme_write_xcoder_write_instance & 0xFFFFUL)))
#define READ_INSTANCE_SET_DW11_INSTANCE(dst, instance) (dst = (instance & 0xFFFFUL))
#define READ_INSTANCE_SET_DW15_SIZE(dst, size) (dst = (size & 0xFFFFFFFFUL))

#define WRITE_INSTANCE_SET_DW10_SUBTYPE(dst, sid) (dst = (((sid << 16) & 0xFFFF0000UL) | (nvme_write_xcoder_write_instance & 0xFFFFUL)))
#define WRITE_INSTANCE_SET_DW11_INSTANCE(dst, instance) (dst = (instance & 0xFFFFUL))
#define WRITE_INSTANCE_SET_DW11_PAGEOFFSET(dst, pageoffset) (dst = (((pageoffset << 16) | dst)))
#define WRITE_INSTANCE_SET_DW12_ISHWDESC(dst, ishwdesc) (dst = (ishwdesc & 0xFFFFUL))
#define WRITE_INSTANCE_SET_DW12_FRAMEINSTID(dst, fid) (dst = (0xFFFFUL & dst) | ((fid<<16) & 0xFFFF0000UL))

#define WRITE_INSTANCE_SET_DW15_SIZE(dst, size) (dst = (size & 0xFFFFFFFFUL))

#define QUERY_SESSION_SET_DW10_SUBTYPE(dst, sid) (dst = (((sid << 16) & 0xFFFF0000UL) | (nvme_query_xcoder_query_session & 0xFFFFUL)))
#define QUERY_SESSION_SET_DW11_INSTANCE(dst, instance) (dst = (instance & 0xFFFFUL))
#define QUERY_SESSION_SET_DW11_SESSION_STATS(dst) (dst = (nvme_query_xcoder_session_get_stats & 0xFFFFUL))
#define QUERY_SESSION_SET_DW15_SIZE(dst, size) (dst = (size & 0xFFFFFFFFUL))

#define QUERY_INSTANCE_SET_DW10_SUBTYPE(dst, sid) (dst = (((sid << 16) & 0xFFFF0000UL) | (nvme_query_xcoder_query_instance & 0xFFFFUL)))
#define QUERY_INSTANCE_SET_DW11_INSTANCE_STATUS(dst, instance) (dst = (((nvme_query_xcoder_instance_get_status << 16) & 0xFFFF0000UL) | (instance & 0xFFFFUL)))
#define QUERY_INSTANCE_SET_DW11_INSTANCE_STREAM_INFO(dst, instance) (dst = (((nvme_query_xcoder_instance_get_stream_info << 16) & 0xFFFF0000UL) | (instance & 0xFFFFUL)))
#define QUERY_INSTANCE_SET_DW11_INSTANCE_END_OF_OUTPUT(dst, instance) (dst = (((nvme_query_xcoder_instance_get_end_of_output << 16) & 0xFFFF0000UL) | (instance & 0xFFFFUL)))

#define QUERY_INSTANCE_SET_DW11_INSTANCE_BUF_INFO(dst, rw_type, inst_type) (dst = (((rw_type << 16) & 0xFFFF0000UL) | (inst_type & 0xFFFFUL)))

#define QUERY_INSTANCE_SET_DW15_SIZE(dst, size) (dst = (size & 0xFFFFFFFFUL))

#define QUERY_GENERAL_SET_DW10_SUBTYPE(dst) (dst = ( (nvme_query_xcoder_query_general & 0xFFFFUL)))
#define QUERY_GENERAL_SET_DW11_INSTANCE_STATUS(dst, instance) (dst = (((nvme_query_xcoder_general_get_status << 16) & 0xFFFF0000UL) | (instance & 0xFFFFUL)))
#define CONFIG_SESSION_SET_DW10_SESSION_ID(dst, sid) (dst = (((sid << 16) & 0xFFFF0000UL) | (nvme_config_xcoder_config_session & 0xFFFFUL)))
#define CONFIG_SESSION_SET_DW11_SUBTYPE(dst, subtype) (dst = (((0 << 16) & 0xFFFF0000UL) | (subtype & 0xFFFFUL)))
#define CONFIG_SESSION_SET_DW15_SIZE(dst, size) (dst = (size & 0xFFFFFFFFUL))

#define CONFIG_INSTANCE_SET_DW10_SUBTYPE(dst, sid) (dst = (((sid << 16) & 0xFFFF0000UL) | (nvme_config_xcoder_config_instance & 0xFFFFUL)))
#define CONFIG_INSTANCE_SET_DW11_SOS(dst, instance) (dst = (((nvme_config_xcoder_config_set_sos << 16) & 0xFFFF0000UL) | (instance & 0xFFFFUL)))
#define CONFIG_INSTANCE_SET_DW11_EOS(dst, instance) (dst = (((nvme_config_xcoder_config_set_eos << 16) & 0xFFFF0000UL) | (instance & 0xFFFFUL)))
#define CONFIG_INSTANCE_SET_DW11_DEC_PARAMS(dst, instance) (dst = (((nvme_config_xcoder_config_set_dec_params << 16) & 0xFFFF0000UL) | (instance & 0xFFFFUL)))
#define CONFIG_INSTANCE_SET_DW11_ENC_PARAMS(dst, instance) (dst = (((nvme_config_xcoder_config_set_enc_params << 16) & 0xFFFF0000UL) | (instance & 0xFFFFUL)))
#define CONFIG_INSTANCE_SET_DW11_ENC_FRAME_PARAMS(dst, instance) (dst = (((nvme_config_xcoder_config_set_enc_frame_params << 16) & 0xFFFF0000UL) | (instance & 0xFFFFUL)))
#define CONFIG_INSTANCE_SET_DW11_FLUSH(dst, instance) (dst = (((nvme_config_xcoder_config_flush << 16) & 0xFFFF0000UL) | (instance & 0xFFFFUL)))
#define CONFIG_INSTANCE_SET_DW11_UPDATE_PARAMS(dst, instance) (dst = (((nvme_config_xcoder_config_update_enc_params << 16) & 0xFFFF0000UL) | (instance & 0xFFFFUL)))
#define CONFIG_INSTANCE_SET_DW11_ALLOC_FRAME(dst, instance) (dst = (((nvme_config_xcoder_config_alloc_frame << 16) & 0xFFFF0000UL) | (instance & 0xFFFFUL)))
#define CONFIG_INSTANCE_SET_DW11_WRITE_LEN(dst, instance) (dst = (((nvme_config_xcoder_config_set_write_legth << 16) & 0xFFFF0000UL) | (instance & 0xFFFFUL)))

#define CONFIG_INSTANCE_SET_DW11_AI_PARAMS(dst, instance)                      \
    (dst = (((nvme_config_xcoder_config_set_network_binary << 16) &            \
             0xFFFF0000UL) |                                                   \
            (instance & 0xFFFFUL)))

#define CONFIG_INSTANCE_SET_DW15_SIZE(dst, size) (dst = (size & 0xFFFFFFFFUL))
#define RECYCLE_BUFFER_SET_DW10_BUFID(dst, bid) (dst = (bid & 0xFFFFUL))

#ifndef XCODER_IO_RW_ENABLED
int32_t ni_nvme_send_admin_cmd(ni_nvme_admin_opcode_t opcode,
                               ni_device_handle_t fd,
                               ni_nvme_command_t *p_ni_nvme_cmd,
                               uint32_t data_len, void *data,
                               uint32_t *pResult);

int32_t ni_nvme_send_io_cmd(ni_nvme_opcode_t opcode, ni_device_handle_t fd,
                            ni_nvme_command_t *p_ni_nvme_cmd, uint32_t data_len,
                            void *data, uint32_t *pResult);
#endif

int32_t ni_nvme_send_io_cmd_thru_admin_queue(ni_nvme_admin_opcode_t opcode,
                                             ni_device_handle_t fd,
                                             ni_nvme_command_t *p_ni_nvme_cmd,
                                             uint32_t data_len, void *data,
                                             uint32_t *pResult);

ni_retcode_t ni_nvme_check_error_code(int rc, int opcode, uint32_t xcoder_type,
                                      uint32_t hw_id, uint32_t *inst_id);

int ni_nvme_enumerate_devices(char ni_devices[][NI_MAX_DEVICE_NAME_LEN],
                              int max_handles);


#ifdef __linux__
int32_t ni_nvme_send_admin_pass_through_command(ni_device_handle_t fd, ni_nvme_passthrough_cmd_t* cmd);
int32_t ni_nvme_send_io_pass_through_command(ni_device_handle_t fd, ni_nvme_passthrough_cmd_t* cmd);
#endif

/********************* transcoder through io read/write command ***********************/
#define NI_DATA_BUFFER_LEN      4096
#define LBA_BIT_OFFSET          12 //logic block size = 4K

//supposed LBA 4K aligned
#define NI_SUB_BIT_OFFSET             4
#define NI_OP_BIT_OFFSET              8
#define NI_INSTANCE_TYPE_OFFSET 19
#define NI_SESSION_ID_OFFSET 22

#define NI_SESSION_ID_SHIFT_HI 3

#define MBs(xMB)            ((xMB)*1024*1024)
#define MBs_to_4k(xMB)      ((xMB)*1024*1024/4096)  // MB to 4K

#define START_OFFSET_IN_4K MBs_to_4k(512)   // 0x00 -- 0x20000

#define CTL_OFFSET_IN_4K(op,sub,subtype)    (START_OFFSET_IN_4K+(((op)<<NI_OP_BIT_OFFSET) + \
                                             ((sub)<<NI_SUB_BIT_OFFSET)+subtype))     // 0x20000 -- 0x28000, each (op,sub,subtype) has 4k bytes
#define RD_OFFSET_IN_4K                                                        \
    (START_OFFSET_IN_4K + MBs_to_4k(128))   // 0x28000 -- 0x38000
#define WR_OFFSET_IN_4K                                                        \
    (RD_OFFSET_IN_4K + MBs_to_4k(256))   // 0x38000 -- 0x48000
#define HIGH_OFFSET_IN_4K(sid, instance)                                       \
    ((((sid & 0x1FFUL) << NI_SESSION_ID_SHIFT_HI) | (instance))                \
     << NI_INSTANCE_TYPE_OFFSET)   // 1024MB +128MB from range 0x0 to 0x40000 + 0x40000 to 0x48000
#define GAP(opcode)                         ((opcode) - nvme_admin_cmd_xcoder_open)

/************read/write command macro*******************/
//write instance
#define WRITE_INSTANCE_W(sid,instance)  HIGH_OFFSET_IN_4K(sid,instance) + WR_OFFSET_IN_4K

//read instance
#define READ_INSTANCE_R(sid,instance)   HIGH_OFFSET_IN_4K(sid,instance) + RD_OFFSET_IN_4K

/************control command macro**********************/
//identify
#define IDENTIFY_DEVICE_R               HIGH_OFFSET_IN_4K(0,0) + CTL_OFFSET_IN_4K(GAP(0xD9), 1, 0)

//open
#define OPEN_GET_SID_R(instance)        HIGH_OFFSET_IN_4K(0,instance) + CTL_OFFSET_IN_4K(GAP(nvme_admin_cmd_xcoder_open),  \
                                            nvme_open_xcoder_create_session,2)
#define OPEN_SESSION_W(sid,instance)    HIGH_OFFSET_IN_4K(sid,instance) + CTL_OFFSET_IN_4K(GAP(nvme_admin_cmd_xcoder_open),  \
                                            nvme_open_xcoder_create_session,0)
#define OPEN_SESSION_CODEC(instance, codec, param)    HIGH_OFFSET_IN_4K((param & 0x3F),instance) + CTL_OFFSET_IN_4K(GAP(nvme_admin_cmd_xcoder_open),  \
                                                          nvme_open_xcoder_create_session,codec)
#define OPEN_ADD_CODEC(instance, codec, param)    HIGH_OFFSET_IN_4K(param,instance) + CTL_OFFSET_IN_4K(GAP(nvme_admin_cmd_xcoder_open),  \
                                                          nvme_open_xcoder_add_session,codec)


//close
#define CLOSE_SESSION_R(sid,instance)   HIGH_OFFSET_IN_4K(sid,instance) + CTL_OFFSET_IN_4K(GAP(nvme_admin_cmd_xcoder_close),  \
                                            nvme_close_xcoder_destroy_session,0)

//query
#define QUERY_SESSION_R(sid,instance)               HIGH_OFFSET_IN_4K(sid,instance) + CTL_OFFSET_IN_4K(GAP(nvme_admin_cmd_xcoder_query),  \
                                                        nvme_query_xcoder_query_session,0)
#define QUERY_SESSION_STATS_R(sid,instance)               HIGH_OFFSET_IN_4K(sid,instance) + CTL_OFFSET_IN_4K(GAP(nvme_admin_cmd_xcoder_query),  \
                                                          nvme_query_xcoder_query_session,nvme_query_xcoder_session_get_stats)
#define QUERY_INSTANCE_STATUS_R(sid,instance)       HIGH_OFFSET_IN_4K(sid,instance) + CTL_OFFSET_IN_4K(GAP(nvme_admin_cmd_xcoder_query),  \
                                                        nvme_query_xcoder_query_instance,nvme_query_xcoder_instance_get_status)

#define QUERY_INSTANCE_CUR_STATUS_INFO_R(sid,instance)  HIGH_OFFSET_IN_4K(sid,instance) + CTL_OFFSET_IN_4K(GAP(nvme_admin_cmd_xcoder_query),  \
                                                        nvme_query_xcoder_query_instance,nvme_query_xcoder_instance_get_current_status)

#define QUERY_INSTANCE_STREAM_INFO_R(sid,instance)  HIGH_OFFSET_IN_4K(sid,instance) + CTL_OFFSET_IN_4K(GAP(nvme_admin_cmd_xcoder_query),  \
                                                        nvme_query_xcoder_query_instance,nvme_query_xcoder_instance_get_stream_info)
#define QUERY_INSTANCE_EOS_R(sid,instance)          HIGH_OFFSET_IN_4K(sid,instance) + CTL_OFFSET_IN_4K(GAP(nvme_admin_cmd_xcoder_query),  \
                                                        nvme_query_xcoder_query_instance,nvme_query_xcoder_instance_get_end_of_output)
#define QUERY_INSTANCE_RBUFF_SIZE_R(sid, instance)  HIGH_OFFSET_IN_4K(sid,instance) + CTL_OFFSET_IN_4K(GAP(nvme_admin_cmd_xcoder_query),  \
                                                        nvme_query_xcoder_query_instance,nvme_query_xcoder_instance_read_buf_size)
#define QUERY_INSTANCE_WBUFF_SIZE_R(sid, instance)  HIGH_OFFSET_IN_4K(sid,instance) + CTL_OFFSET_IN_4K(GAP(nvme_admin_cmd_xcoder_query),  \
                                                        nvme_query_xcoder_query_instance,nvme_query_xcoder_instance_write_buf_size)
#define QUERY_INSTANCE_RBUFF_SIZE_BUSY_R(sid, instance)                        \
    HIGH_OFFSET_IN_4K(sid, instance) +                                         \
        CTL_OFFSET_IN_4K(GAP(nvme_admin_cmd_xcoder_query),                     \
                         nvme_query_xcoder_query_instance,                     \
                         nvme_query_xcoder_instance_read_buf_size_busy)
#define QUERY_INSTANCE_WBUFF_SIZE_BUSY_R(sid, instance)                        \
    HIGH_OFFSET_IN_4K(sid, instance) +                                         \
        CTL_OFFSET_IN_4K(GAP(nvme_admin_cmd_xcoder_query),                     \
                         nvme_query_xcoder_query_instance,                     \
                         nvme_query_xcoder_instance_write_buf_size_busy)
#define QUERY_INSTANCE_UPLOAD_ID_R(sid, instance)  HIGH_OFFSET_IN_4K(sid,instance) + CTL_OFFSET_IN_4K(GAP(nvme_admin_cmd_xcoder_query),  \
                                                        nvme_query_xcoder_query_instance,nvme_query_xcoder_instance_upload_idx)
#define QUERY_INSTANCE_ACQUIRE_BUF(sid, instance)                              \
    HIGH_OFFSET_IN_4K(sid, instance) +                                         \
        CTL_OFFSET_IN_4K(GAP(nvme_admin_cmd_xcoder_query),                     \
                         nvme_query_xcoder_query_instance,                     \
                         nvme_query_xcoder_instance_acquire_buf)

#define QUERY_INSTANCE_NL_SIZE_R(sid, instance)                                \
    HIGH_OFFSET_IN_4K(sid, instance) +                                         \
        CTL_OFFSET_IN_4K(GAP(nvme_admin_cmd_xcoder_query),                     \
                         nvme_query_xcoder_query_instance,                     \
                         nvme_query_xcoder_instance_network_layer_size)
#define QUERY_INSTANCE_NL_R(sid, instance)                                     \
    HIGH_OFFSET_IN_4K(sid, instance) +                                         \
        CTL_OFFSET_IN_4K(GAP(nvme_admin_cmd_xcoder_query),                     \
                         nvme_query_xcoder_query_instance,                     \
                         nvme_query_xcoder_instance_read_network_layer)

#define QUERY_GENERAL_GET_STATUS_R(instance)        HIGH_OFFSET_IN_4K(0,instance) + CTL_OFFSET_IN_4K(GAP(nvme_admin_cmd_xcoder_query),  \
                                                        nvme_query_xcoder_query_general,nvme_query_xcoder_general_get_status)

//config instance
#define CONFIG_INSTANCE_SetSOS_W(sid,instance)      HIGH_OFFSET_IN_4K(sid,instance) + CTL_OFFSET_IN_4K(GAP(nvme_admin_cmd_xcoder_config),  \
                                                        nvme_config_xcoder_config_instance,nvme_config_xcoder_config_set_sos)
#define CONFIG_INSTANCE_SetEOS_W(sid,instance)      HIGH_OFFSET_IN_4K(sid,instance) + CTL_OFFSET_IN_4K(GAP(nvme_admin_cmd_xcoder_config),  \
                                                        nvme_config_xcoder_config_instance,nvme_config_xcoder_config_set_eos)
#define CONFIG_INSTANCE_Flush_W(sid,instance)       HIGH_OFFSET_IN_4K(sid,instance) + CTL_OFFSET_IN_4K(GAP(nvme_admin_cmd_xcoder_config),  \
                                                        nvme_config_xcoder_config_instance,nvme_config_xcoder_config_flush)

#define CONFIG_INSTANCE_SetDecPara_W(sid,instance)      HIGH_OFFSET_IN_4K(sid,instance) + CTL_OFFSET_IN_4K(GAP(nvme_admin_cmd_xcoder_config),  \
                                                            nvme_config_xcoder_config_instance,nvme_config_xcoder_config_set_dec_params)
#define CONFIG_INSTANCE_SetScalerPara_W(sid, instance)                         \
    HIGH_OFFSET_IN_4K(sid, instance) +                                         \
        CTL_OFFSET_IN_4K(GAP(nvme_admin_cmd_xcoder_config),                    \
                         nvme_config_xcoder_config_instance,                   \
                         nvme_config_xcoder_config_set_scaler_params)
#define CONFIG_INSTANCE_SetScalerAlloc_W(sid, instance)                        \
    HIGH_OFFSET_IN_4K(sid, instance) +                                         \
        CTL_OFFSET_IN_4K(GAP(nvme_admin_cmd_xcoder_config),                    \
                         nvme_config_xcoder_config_instance,                   \
                         nvme_config_xcoder_config_alloc_frame)
#define CONFIG_INSTANCE_SetEncPara_W(sid,instance)      HIGH_OFFSET_IN_4K(sid,instance) + CTL_OFFSET_IN_4K(GAP(nvme_admin_cmd_xcoder_config),  \
                                                            nvme_config_xcoder_config_instance,nvme_config_xcoder_config_set_enc_params)
#define CONFIG_INSTANCE_UpdateEncPara_W(sid,instance)   HIGH_OFFSET_IN_4K(sid,instance) + CTL_OFFSET_IN_4K(GAP(nvme_admin_cmd_xcoder_config),  \
                                                            nvme_config_xcoder_config_instance,nvme_config_xcoder_config_update_enc_params)
#define CONFIG_INSTANCE_SetEncFramePara_W(sid,instance) HIGH_OFFSET_IN_4K(sid,instance) + CTL_OFFSET_IN_4K(GAP(nvme_admin_cmd_xcoder_config),  \
                                                            nvme_config_xcoder_config_instance,nvme_config_xcoder_config_set_enc_frame_params)
#define CONFIG_INSTANCE_SetPktSize_W(sid,instance)      HIGH_OFFSET_IN_4K(sid,instance) + CTL_OFFSET_IN_4K(GAP(nvme_admin_cmd_xcoder_config),  \
                                                            nvme_config_xcoder_config_instance,nvme_config_xcoder_config_set_write_legth)
#define CONFIG_INSTANCE_SetSeqChange_W(sid,instance)      HIGH_OFFSET_IN_4K(sid,instance) + CTL_OFFSET_IN_4K(GAP(nvme_admin_cmd_xcoder_config),  \
                                                            nvme_config_xcoder_config_instance,nvme_config_xcoder_config_set_sequence_change)
#define CONFIG_INSTANCE_SetAiPara_W(sid, instance)                             \
    HIGH_OFFSET_IN_4K(sid, instance) +                                         \
        CTL_OFFSET_IN_4K(GAP(nvme_admin_cmd_xcoder_config),                    \
                         nvme_config_xcoder_config_instance,                   \
                         nvme_config_xcoder_config_set_network_binary)
#define CONFIG_INSTANCE_SetAiFrm_W(sid, instance)                              \
    HIGH_OFFSET_IN_4K(sid, instance) +                                         \
        CTL_OFFSET_IN_4K(GAP(nvme_admin_cmd_xcoder_config),                    \
                         nvme_config_xcoder_config_instance,                   \
                         nvme_config_xcoder_config_alloc_frame)

//config session
#define CONFIG_SESSION_KeepAlive_W(sid)             HIGH_OFFSET_IN_4K(sid,0) + CTL_OFFSET_IN_4K(GAP(nvme_admin_cmd_xcoder_config),  \
                                                            nvme_config_xcoder_config_session,nvme_config_xcoder_config_session_keep_alive)
#define CONFIG_SESSION_Read_W(sid)                  HIGH_OFFSET_IN_4K(sid,0) + CTL_OFFSET_IN_4K(GAP(nvme_admin_cmd_xcoder_config),  \
                                                            nvme_config_xcoder_config_session,nvme_config_xcoder_config_session_read)
#define CONFIG_SESSION_Write_W(sid)                 HIGH_OFFSET_IN_4K(sid,0) + CTL_OFFSET_IN_4K(GAP(nvme_admin_cmd_xcoder_config),  \
                                                            nvme_config_xcoder_config_session,nvme_config_xcoder_config_session_write)

#define CLEAR_INSTANCE_BUF_W(frame_id)		    CTL_OFFSET_IN_4K(GAP(nvme_admin_cmd_xcoder_recycle_buffer), 0, (frame_id & 0x00FF)) + (((frame_id & 0xFF00)>>8)<<NI_INSTANCE_TYPE_OFFSET)

#define CONFIG_SESSION_KeepAliveTimeout_W(sid)                                 \
    HIGH_OFFSET_IN_4K(sid, 0) +                                                \
        CTL_OFFSET_IN_4K(GAP(nvme_admin_cmd_xcoder_config),                    \
                         nvme_config_xcoder_config_session,                    \
                         nvme_config_xcoder_config_session_keep_alive_timeout)

#define CONFIG_SESSION_SWVersion_W(sid)                                        \
    HIGH_OFFSET_IN_4K(sid, 0) +                                                \
        CTL_OFFSET_IN_4K(GAP(nvme_admin_cmd_xcoder_config),                    \
                         nvme_config_xcoder_config_session,                    \
                         nvme_config_xcoder_config_session_sw_version)

int32_t ni_nvme_send_read_cmd(ni_device_handle_t handle, ni_event_handle_t event_handle, void *p_data, uint32_t data_len, uint32_t lba);
int32_t ni_nvme_send_write_cmd(ni_device_handle_t handle, ni_event_handle_t event_handle, void *p_data, uint32_t data_len, uint32_t lba);

#ifdef __cplusplus
}
#endif
