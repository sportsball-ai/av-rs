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
*  \file   ni_libxcoder_dynamic_loading.h
*
*  \brief  Libxcoder API dynamic loading support for Linux
*
*  \author Netflix, Inc. (2022)
*
*******************************************************************************/

#pragma once

#ifndef _NETINTLIBXCODERAPI_H_
#define _NETINTLIBXCODERAPI_H_

#include <dlfcn.h>

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-variable"

#ifndef _NETINT_LIBXCODER_DYNAMIC_LOADING_TEST_
#include <ni_av_codec.h>
#include <ni_util.h>
#include <ni_device_api.h>
#else
#include "ni_av_codec.h"
#include "ni_util.h"
#include "ni_device_api.h"
#endif

#pragma GCC diagnostic pop

#define LIB_API

/*
 * Defines API function pointers
 */
//
// Function pointers for ni_av_codec.h
//
typedef int (LIB_API* PNISHOULDSENDSEIWITHFRAME) (ni_session_context_t *p_enc_ctx, ni_pic_type_t pic_type, ni_xcoder_params_t *p_param);
typedef void (LIB_API* PNIDECRETRIEVEAUXDATA) (ni_frame_t *frame);
typedef void (LIB_API* PNIENCPREPAUXDATA) (ni_session_context_t *p_enc_ctx, ni_frame_t *p_enc_frame, ni_frame_t *p_dec_frame, ni_codec_format_t codec_format, int should_send_sei_with_frame, uint8_t *mdcv_data, uint8_t *cll_data, uint8_t *cc_data, uint8_t *udu_data, uint8_t *hdrp_data);
typedef void (LIB_API* PNIENCCOPYAUXDATA) (ni_session_context_t *p_enc_ctx, ni_frame_t *p_enc_frame, ni_frame_t *p_dec_frame, ni_codec_format_t codec_format, const uint8_t *mdcv_data, const uint8_t *cll_data, const uint8_t *cc_data, const uint8_t *udu_data, const uint8_t *hdrp_data, int is_hwframe, int is_nv12frame);
//
// Function pointers for ni_util.h
//
typedef void (LIB_API* PNIGETHWYUV420PDIM) (int width, int height, int bit_depth_factor, int is_nv12, int plane_stride[NI_MAX_NUM_DATA_POINTERS], int plane_height[NI_MAX_NUM_DATA_POINTERS]);
typedef void (LIB_API* PNICOPYHWYUV420P) (uint8_t *p_dst[NI_MAX_NUM_DATA_POINTERS], uint8_t *p_src[NI_MAX_NUM_DATA_POINTERS], int width, int height, int bit_depth_factor, int is_nv12, int conf_win_right, int dst_stride[NI_MAX_NUM_DATA_POINTERS], int dst_height[NI_MAX_NUM_DATA_POINTERS], int src_stride[NI_MAX_NUM_DATA_POINTERS], int src_height[NI_MAX_NUM_DATA_POINTERS]);
typedef int (LIB_API* PNIINSERTEMULATIONPREVENTBYTES) (uint8_t *buf, int size);
typedef int (LIB_API* PNIREMOVEEMULATIONPREVENTBYTES) (uint8_t *buf, int size);
typedef int32_t (LIB_API* PNIGETTIMEOFDAY) (struct timeval *p_tp, void *p_tzp);
typedef uint64_t (LIB_API* PNIGETTIMENS) (void);
typedef void (LIB_API* PNIUSLEEP) (int64_t usec);
typedef ni_retcode_t (LIB_API* PNINETWORKLAYERCONVERTOUTPUT) (float *dst, uint32_t num, ni_packet_t *p_packet, ni_network_data_t *p_network, uint32_t layer);
typedef uint32_t (LIB_API* PNIAINETWORKLAYERSIZE) (ni_network_layer_params_t *p_param);
typedef uint32_t (LIB_API* PNIAINETWORKLAYERDIMS) (ni_network_layer_params_t *p_param);
typedef ni_retcode_t (LIB_API* PNINETWORKLAYERCONVERTTENSOR) (uint8_t *dst, uint32_t dst_len, const char *tensor_file, ni_network_layer_params_t *p_param);
typedef ni_retcode_t (LIB_API* PNINETWORKCONVERTTENSORTODATA) (uint8_t *dst, uint32_t dst_len, float *src, uint32_t src_len, ni_network_layer_params_t *p_param);
typedef ni_retcode_t (LIB_API* PNINETWORKCONVERTDATATOTENSOR) (float *dst, uint32_t dst_len, uint8_t *src, uint32_t src_len, ni_network_layer_params_t *p_param);
typedef void (LIB_API* PNICALCULATESHA256) (const uint8_t aui8Data[], size_t ui32DataLength, uint8_t aui8Hash[]);
typedef void (LIB_API* PNICOPYHWDESCRIPTORS) (uint8_t *p_dst[NI_MAX_NUM_DATA_POINTERS], uint8_t *p_src[NI_MAX_NUM_DATA_POINTERS]);
typedef char* (LIB_API* PNIGETLIBXCODERAPIVER) (void);
typedef char* (LIB_API* PNIGETCOMPATFWAPIVER) (void);
typedef char* (LIB_API* PNIGETLIBXCODERRELEASEVER) (void);
//
// Function pointers for ni_device_api.h
//
typedef ni_session_context_t * (LIB_API* PNIDEVICESESSIONCONTEXTALLOCINIT) (void);
typedef ni_retcode_t (LIB_API* PNIDEVICESESSIONCONTEXTINIT) (ni_session_context_t *p_ctx);
typedef void (LIB_API* PNIDEVICESESSIONCONTEXTCLEAR) (ni_session_context_t *p_ctx);
typedef void (LIB_API* PNIDEVICESESSIONCONTEXTFREE) (ni_session_context_t *p_ctx);
typedef ni_event_handle_t (LIB_API* PNICREATEEVENT) ();
typedef void (LIB_API* PNICLOSEEVENT) (ni_event_handle_t event_handle);
typedef ni_device_handle_t (LIB_API* PNIDEVICEOPEN) (const char *dev, uint32_t *p_max_io_size_out);
typedef void (LIB_API* PNIDEVICECLOSE) (ni_device_handle_t dev);
typedef ni_retcode_t (LIB_API* PNIDEVICECAPABILITYQUERY) (ni_device_handle_t device_handle, ni_device_capability_t *p_cap);
typedef ni_retcode_t (LIB_API* PNIDEVICESESSIONOPEN) (ni_session_context_t *p_ctx, ni_device_type_t device_type);
typedef ni_retcode_t (LIB_API* PNIDEVICESESSIONCLOSE) (ni_session_context_t *p_ctx, int eos_received, ni_device_type_t device_type);
typedef ni_retcode_t (LIB_API* PNIDEVICESESSIONFLUSH) (ni_session_context_t *p_ctx, ni_device_type_t device_type);
typedef ni_retcode_t (LIB_API* PNIDEVICEDECSESSIONSAVEHDRS) (ni_session_context_t *p_ctx, uint8_t *hdr_data, uint8_t hdr_size);
typedef ni_retcode_t (LIB_API* PNIDEVICEDECSESSIONFLUSH) (ni_session_context_t *p_ctx);
typedef int (LIB_API* PNIDEVICESESSIONWRITE) (ni_session_context_t *p_ctx, ni_session_data_io_t *p_data, ni_device_type_t device_type);
typedef int (LIB_API* PNIDEVICESESSIONREAD) (ni_session_context_t *p_ctx, ni_session_data_io_t *p_data, ni_device_type_t device_type);
typedef ni_retcode_t (LIB_API* PNIDEVICESESSIONQUERY) (ni_session_context_t *p_ctx, ni_device_type_t device_type);
typedef ni_retcode_t (LIB_API* PNIFRAMEBUFFERALLOC) (ni_frame_t *p_frame, int video_width, int video_height, int alignment, int metadata_flag, int factor, int hw_frame_count, int is_planar);
typedef ni_retcode_t (LIB_API* PNIDECODERFRAMEBUFFERALLOC) (ni_buf_pool_t *p_pool, ni_frame_t *pframe, int alloc_mem, int video_width, int video_height, int alignment, int factor, int is_planar);
typedef ni_retcode_t (LIB_API* PNIENCODERFRAMEBUFFERALLOC) (ni_frame_t *pframe, int video_width, int video_height, int linesize[], int alignment, int extra_len, bool alignment_2pass_wa);
typedef ni_retcode_t (LIB_API* PNIFRAMEBUFFERALLOCNV) (ni_frame_t *p_frame, int video_width, int video_height, int linesize[], int extra_len, bool alignment_2pass_wa);
typedef ni_retcode_t (LIB_API* PNIENCODERSWFRAMEBUFFERALLOC) (bool planar, ni_frame_t *p_frame, int video_width, int video_height, int linesize[], int alignment, int extra_len, bool alignment_2pass_wa);
typedef ni_retcode_t (LIB_API* PNIFRAMEBUFFERFREE) (ni_frame_t *pframe);
typedef ni_retcode_t (LIB_API* PNIDECODERFRAMEBUFFERFREE) (ni_frame_t *pframe);
typedef void (LIB_API* PNIDECODERFRAMEBUFFERPOOLRETURNBUF) (ni_buf_t *buf, ni_buf_pool_t *p_buffer_pool);
typedef ni_retcode_t (LIB_API* PNIPACKETBUFFERALLOC) (ni_packet_t *ppacket, int packet_size);
typedef ni_retcode_t (LIB_API* PNICUSTOMPACKETBUFFERALLOC) (void *p_buffer, ni_packet_t *p_packet, int buffer_size);
typedef ni_retcode_t (LIB_API* PNIPACKETBUFFERFREE) (ni_packet_t *ppacket);
typedef ni_retcode_t (LIB_API* PNIPACKETBUFFERFREEAV1) (ni_packet_t *ppacket);
typedef int (LIB_API* PNIPACKETCOPY) (void *p_destination, const void *const p_source, int cur_size, void *p_leftover, int *p_prev_size);
typedef ni_aux_data_t * (LIB_API* PNIFRAMENEWAUXDATA) (ni_frame_t *frame, ni_aux_data_type_t type, int data_size);
typedef ni_aux_data_t * (LIB_API* PNIFRAMENEWAUXDATAFROMRAWDATA) (ni_frame_t *frame, ni_aux_data_type_t type, const uint8_t *raw_data, int data_size);
typedef ni_aux_data_t * (LIB_API* PNIFRAMEGETAUXDATA) (const ni_frame_t *frame, ni_aux_data_type_t type);
typedef void (LIB_API* PNIFRAMEFREEAUXDATA) (ni_frame_t *frame, ni_aux_data_type_t type);
typedef void (LIB_API* PNIFRAMEWIPEAUXDATA) (ni_frame_t *frame);
typedef ni_retcode_t (LIB_API* PNIENCODERINITDEFAULTPARAMS) (ni_xcoder_params_t *p_param, int fps_num, int fps_denom, long bit_rate, int width, int height, ni_codec_format_t codec_format);
typedef ni_retcode_t (LIB_API* PNIDECODERINITDEFAULTPARAMS) (ni_xcoder_params_t *p_param, int fps_num, int fps_denom, long bit_rate, int width, int height);
typedef ni_retcode_t (LIB_API* PNIENCODERPARAMSSETVALUE) (ni_xcoder_params_t *p_params, const char *name, const char *value);
typedef ni_retcode_t (LIB_API* PNIDECODERPARAMSSETVALUE) (ni_xcoder_params_t *p_params, const char *name, char *value);
typedef ni_retcode_t (LIB_API* PNIENCODERGOPPARAMSSETVALUE) (ni_xcoder_params_t *p_params, const char *name, const char *value);
typedef ni_retcode_t (LIB_API* PNIDEVICESESSIONCOPY) (ni_session_context_t *src_p_ctx, ni_session_context_t *dst_p_ctx);
typedef int (LIB_API* PNIDEVICESESSIONINITFRAMEPOOL) (ni_session_context_t *p_ctx, uint32_t pool_size, uint32_t pool);
typedef int (LIB_API* PNIDEVICESESSIONREADHWDESC) (ni_session_context_t *p_ctx, ni_session_data_io_t *p_data, ni_device_type_t device_type);
typedef int (LIB_API* PNIDEVICESESSIONHWDL) (ni_session_context_t *p_ctx, ni_session_data_io_t *p_data, niFrameSurface1_t *hwdesc);
typedef int (LIB_API* PNIDEVICESESSIONHWUP) (ni_session_context_t* p_ctx, ni_session_data_io_t *p_src_data, niFrameSurface1_t* hwdesc);
typedef ni_retcode_t (LIB_API* PNIFRAMEBUFFERALLOCHWENC) (ni_frame_t *pframe, int video_width, int video_height, int extra_len);
typedef ni_retcode_t (LIB_API* PNIHWFRAMEBUFFERRECYCLE) (niFrameSurface1_t *surface, int32_t device_handle);
typedef ni_retcode_t (LIB_API* PNISCALERSETPARAMS) (ni_session_context_t *p_ctx, ni_scaler_params_t *p_params);
typedef ni_retcode_t (LIB_API* PNIDEVICEALLOCFRAME) (ni_session_context_t* p_ctx, int width, int height, int format, int options, int rectangle_width, int rectangle_height, int rectangle_x, int rectangle_y, int rgba_color, int frame_index, ni_device_type_t device_type);
typedef ni_retcode_t (LIB_API* PNIDEVICECONFIGFRAME) (ni_session_context_t *p_ctx, ni_frame_config_t *p_cfg);
typedef ni_retcode_t (LIB_API* PNIFRAMEBUFFERALLOCPIXFMT) (ni_frame_t *pframe, int pixel_format, int video_width, int video_height, int linesize[], int alignment, int extra_len);
typedef ni_retcode_t (LIB_API* PNIAICONFIGNETWORKBINARY) (ni_session_context_t *p_ctx, ni_network_data_t *p_network, const char *file);
typedef ni_retcode_t (LIB_API* PNIAIFRAMEBUFFERALLOC) (ni_frame_t *p_frame, ni_network_data_t *p_network);
typedef ni_retcode_t (LIB_API* PNIAIPACKETBUFFERALLOC) (ni_packet_t *p_packet, ni_network_data_t *p_network);
typedef ni_retcode_t (LIB_API* PNIRECONFIGBITRATE) (ni_session_context_t *p_ctx, int32_t bitrate);
typedef ni_retcode_t (LIB_API* PNIFORCEIDRFRAMETYPE) (ni_session_context_t *p_ctx);
typedef ni_retcode_t (LIB_API* PNISETLTR) (ni_session_context_t *p_ctx, ni_long_term_ref_t *ltr);
typedef ni_retcode_t (LIB_API* PNISETLTRINTERVAL) (ni_session_context_t *p_ctx, int32_t ltr_interval);
typedef ni_retcode_t (LIB_API* PNISETFRAMEREFINVALID) (ni_session_context_t *p_ctx, int32_t frame_num);
typedef ni_retcode_t (LIB_API* PNIRECONFIGFRAMERATE) (ni_session_context_t *p_ctx, ni_framerate_t *framerate);
typedef int (LIB_API* PNIDEVICESESSIONACQUIRE) (ni_session_context_t *p_upl_ctx, ni_frame_t *p_frame);
typedef ni_retcode_t (LIB_API* PNIUPLOADERFRAMEBUFFERLOCK) (ni_session_context_t *p_upl_ctx, ni_frame_t *p_frame);
typedef ni_retcode_t (LIB_API* PNIUPLOADERFRAMEBUFFERUNLOCK) (ni_session_context_t *p_upl_ctx, ni_frame_t *p_frame);
typedef ni_retcode_t (LIB_API* PNIUPLOADERP2PTESTSEND) (ni_session_context_t *p_upl_ctx, uint8_t *p_data, uint32_t len, ni_frame_t *p_hwframe);
typedef ni_retcode_t (LIB_API* PNIENCODERSETINPUTFRAMEFORMAT) (ni_session_context_t *p_enc_ctx, ni_xcoder_params_t *p_enc_params, int width, int height, int bit_depth, int src_endian, int planar);
typedef ni_retcode_t (LIB_API* PNIUPLOADERSETFRAMEFORMAT) (ni_session_context_t *p_upl_ctx, int width, int height, ni_pix_fmt_t pixel_format, int isP2P);
typedef ni_retcode_t (LIB_API* PNIHWFRAMEP2PBUFFERRECYCLE) (ni_frame_t *p_frame);
typedef int (LIB_API* PNIENCODERSESSIONREADSTREAMHEADER) (ni_session_context_t *p_ctx, ni_session_data_io_t *p_data);
typedef int32_t (LIB_API* PNIGETDMABUFFILEDESCRIPTOR) (const ni_frame_t* p_frame);
typedef ni_retcode_t (LIB_API* PNIDEVICESESSIONSEQUENCECHANGE) (ni_session_context_t *p_ctx, int width, int height, int bit_depth_factor, ni_device_type_t device_type);
 
/* End API function pointers */
 
 
 
/*
 * Definition of _NETINT_LIBXCODER_API_FUNCTION_LIST
 */
typedef struct _NETINT_LIBXCODER_API_FUNCTION_LIST
{
    //
    // API function list for ni_av_codec.h
    //
    PNISHOULDSENDSEIWITHFRAME            niShouldSendSeiWithFrame;             /** Client should access ::ni_should_send_sei_with_frame API through this pointer */
    PNIDECRETRIEVEAUXDATA                niDecRetrieveAuxData;                 /** Client should access ::ni_dec_retrieve_aux_data API through this pointer */
    PNIENCPREPAUXDATA                    niEncPrepAuxData;                     /** Client should access ::ni_enc_prep_aux_data API through this pointer */
    PNIENCCOPYAUXDATA                    niEncCopyAuxData;                     /** Client should access ::ni_enc_copy_aux_data API through this pointer */
    //
    // API function list for ni_util.h
    //
    PNIGETHWYUV420PDIM                   niGetHwYuv420PDim;                    /** Client should access ::ni_get_hw_yuv420p_dim API through this pointer */
    PNICOPYHWYUV420P                     niCopyHwYuv420P;                      /** Client should access ::ni_copy_hw_yuv420p API through this pointer */
    PNIINSERTEMULATIONPREVENTBYTES       niInsertEmulationPreventBytes;        /** Client should access ::ni_insert_emulation_prevent_bytes API through this pointer */
    PNIREMOVEEMULATIONPREVENTBYTES       niRemoveEmulationPreventBytes;        /** Client should access ::ni_remove_emulation_prevent_bytes API through this pointer */
    PNIGETTIMEOFDAY                      niGettimeofday;                       /** Client should access ::ni_gettimeofday API through this pointer */
    PNIGETTIMENS                         niGettimeNs;                          /** Client should access ::ni_gettime_ns API through this pointer */
    PNIUSLEEP                            niUsleep;                             /** Client should access ::ni_usleep API through this pointer */
    PNINETWORKLAYERCONVERTOUTPUT         niNetworkLayerConvertOutput;          /** Client should access ::ni_network_layer_convert_output API through this pointer */
    PNIAINETWORKLAYERSIZE                niAiNetworkLayerSize;                 /** Client should access ::ni_ai_network_layer_size API through this pointer */
    PNIAINETWORKLAYERDIMS                niAiNetworkLayerDims;                 /** Client should access ::ni_ai_network_layer_dims API through this pointer */
    PNINETWORKLAYERCONVERTTENSOR         niNetworkLayerConvertTensor;          /** Client should access ::ni_network_layer_convert_tensor API through this pointer */
    PNINETWORKCONVERTTENSORTODATA        niNetworkConvertTensorToData;         /** Client should access ::ni_network_convert_tensor_to_data API through this pointer */
    PNINETWORKCONVERTDATATOTENSOR        niNetworkConvertDataToTensor;         /** Client should access ::ni_network_convert_data_to_tensor API through this pointer */
    PNICALCULATESHA256                   niCalculateSha256;                    /** Client should access ::ni_calculate_sha256 API through this pointer */
    PNICOPYHWDESCRIPTORS                 niCopyHwDescriptors;                  /** Client should access ::ni_copy_hw_descriptors API through this pointer */
    PNIGETLIBXCODERAPIVER                niGetLibxcoderApiVer;                 /** Client should access ::ni_get_libxcoder_api_ver API through this pointer */
    PNIGETCOMPATFWAPIVER                 niGetCompatFwApiVer;                  /** Client should access ::ni_get_compat_fw_api_ver API through this pointer */
    PNIGETLIBXCODERRELEASEVER            niGetLibxcoderReleaseVer;             /** Client should access ::ni_get_libxcoder_release_ver API through this pointer */
    //
    // API function list for ni_device_api.h
    //
    PNIDEVICESESSIONCONTEXTALLOCINIT     niDeviceSessionContextAllocInit;      /** Client should access ::ni_device_session_context_alloc_init API through this pointer */
    PNIDEVICESESSIONCONTEXTINIT          niDeviceSessionContextInit;           /** Client should access ::ni_device_session_context_init API through this pointer */
    PNIDEVICESESSIONCONTEXTCLEAR         niDeviceSessionContextClear;          /** Client should access ::ni_device_session_context_clear API through this pointer */
    PNIDEVICESESSIONCONTEXTFREE          niDeviceSessionContextFree;           /** Client should access ::ni_device_session_context_free API through this pointer */
    PNICREATEEVENT                       niCreateEvent;                        /** Client should access ::ni_create_event API through this pointer */
    PNICLOSEEVENT                        niCloseEvent;                         /** Client should access ::ni_close_event API through this pointer */
    PNIDEVICEOPEN                        niDeviceOpen;                         /** Client should access ::ni_device_open API through this pointer */
    PNIDEVICECLOSE                       niDeviceClose;                        /** Client should access ::ni_device_close API through this pointer */
    PNIDEVICECAPABILITYQUERY             niDeviceCapabilityQuery;              /** Client should access ::ni_device_capability_query API through this pointer */
    PNIDEVICESESSIONOPEN                 niDeviceSessionOpen;                  /** Client should access ::ni_device_session_open API through this pointer */
    PNIDEVICESESSIONCLOSE                niDeviceSessionClose;                 /** Client should access ::ni_device_session_close API through this pointer */
    PNIDEVICESESSIONFLUSH                niDeviceSessionFlush;                 /** Client should access ::ni_device_session_flush API through this pointer */
    PNIDEVICEDECSESSIONSAVEHDRS          niDeviceDecSessionSaveHdrs;           /** Client should access ::ni_device_dec_session_save_hdrs API through this pointer */
    PNIDEVICEDECSESSIONFLUSH             niDeviceDecSessionFlush;              /** Client should access ::ni_device_dec_session_flush API through this pointer */
    PNIDEVICESESSIONWRITE                niDeviceSessionWrite;                 /** Client should access ::ni_device_session_write API through this pointer */
    PNIDEVICESESSIONREAD                 niDeviceSessionRead;                  /** Client should access ::ni_device_session_read API through this pointer */
    PNIDEVICESESSIONQUERY                niDeviceSessionQuery;                 /** Client should access ::ni_device_session_query API through this pointer */
    PNIFRAMEBUFFERALLOC                  niFrameBufferAlloc;                   /** Client should access ::ni_frame_buffer_alloc API through this pointer */
    PNIDECODERFRAMEBUFFERALLOC           niDecoderFrameBufferAlloc;            /** Client should access ::ni_decoder_frame_buffer_alloc API through this pointer */
    PNIENCODERFRAMEBUFFERALLOC           niEncoderFrameBufferAlloc;            /** Client should access ::ni_encoder_frame_buffer_alloc API through this pointer */
    PNIFRAMEBUFFERALLOCNV                niFrameBufferAllocNv;                 /** Client should access ::ni_frame_buffer_alloc_nv API through this pointer */
    PNIENCODERSWFRAMEBUFFERALLOC         niEncoderSwFrameBufferAlloc;          /** Client should access ::ni_encoder_sw_frame_buffer_alloc API through this pointer */
    PNIFRAMEBUFFERFREE                   niFrameBufferFree;                    /** Client should access ::ni_frame_buffer_free API through this pointer */
    PNIDECODERFRAMEBUFFERFREE            niDecoderFrameBufferFree;             /** Client should access ::ni_decoder_frame_buffer_free API through this pointer */
    PNIDECODERFRAMEBUFFERPOOLRETURNBUF   niDecoderFrameBufferPoolReturnBuf;    /** Client should access ::ni_decoder_frame_buffer_pool_return_buf API through this pointer */
    PNIPACKETBUFFERALLOC                 niPacketBufferAlloc;                  /** Client should access ::ni_packet_buffer_alloc API through this pointer */
    PNICUSTOMPACKETBUFFERALLOC           niCustomPacketBufferAlloc;            /** Client should access ::ni_custom_packet_buffer_alloc API through this pointer */
    PNIPACKETBUFFERFREE                  niPacketBufferFree;                   /** Client should access ::ni_packet_buffer_free API through this pointer */
    PNIPACKETBUFFERFREEAV1               niPacketBufferFreeAv1;                /** Client should access ::ni_packet_buffer_free_av1 API through this pointer */
    PNIPACKETCOPY                        niPacketCopy;                         /** Client should access ::ni_packet_copy API through this pointer */
    PNIFRAMENEWAUXDATA                   niFrameNewAuxData;                    /** Client should access ::ni_frame_new_aux_data API through this pointer */
    PNIFRAMENEWAUXDATAFROMRAWDATA        niFrameNewAuxDataFromRawData;         /** Client should access ::ni_frame_new_aux_data_from_raw_data API through this pointer */
    PNIFRAMEGETAUXDATA                   niFrameGetAuxData;                    /** Client should access ::ni_frame_get_aux_data API through this pointer */
    PNIFRAMEFREEAUXDATA                  niFrameFreeAuxData;                   /** Client should access ::ni_frame_free_aux_data API through this pointer */
    PNIFRAMEWIPEAUXDATA                  niFrameWipeAuxData;                   /** Client should access ::ni_frame_wipe_aux_data API through this pointer */
    PNIENCODERINITDEFAULTPARAMS          niEncoderInitDefaultParams;           /** Client should access ::ni_encoder_init_default_params API through this pointer */
    PNIDECODERINITDEFAULTPARAMS          niDecoderInitDefaultParams;           /** Client should access ::ni_decoder_init_default_params API through this pointer */
    PNIENCODERPARAMSSETVALUE             niEncoderParamsSetValue;              /** Client should access ::ni_encoder_params_set_value API through this pointer */
    PNIDECODERPARAMSSETVALUE             niDecoderParamsSetValue;              /** Client should access ::ni_decoder_params_set_value API through this pointer */
    PNIENCODERGOPPARAMSSETVALUE          niEncoderGopParamsSetValue;           /** Client should access ::ni_encoder_gop_params_set_value API through this pointer */
    PNIDEVICESESSIONCOPY                 niDeviceSessionCopy;                  /** Client should access ::ni_device_session_copy API through this pointer */
    PNIDEVICESESSIONINITFRAMEPOOL        niDeviceSessionInitFramepool;         /** Client should access ::ni_device_session_init_framepool API through this pointer */
    PNIDEVICESESSIONREADHWDESC           niDeviceSessionReadHwdesc;            /** Client should access ::ni_device_session_read_hwdesc API through this pointer */
    PNIDEVICESESSIONHWDL                 niDeviceSessionHwdl;                  /** Client should access ::ni_device_session_hwdl API through this pointer */
    PNIDEVICESESSIONHWUP                 niDeviceSessionHwup;                  /** Client should access ::ni_device_session_hwup API through this pointer */
    PNIFRAMEBUFFERALLOCHWENC             niFrameBufferAllocHwenc;              /** Client should access ::ni_frame_buffer_alloc_hwenc API through this pointer */
    PNIHWFRAMEBUFFERRECYCLE              niHwframeBufferRecycle;               /** Client should access ::ni_hwframe_buffer_recycle API through this pointer */
    PNISCALERSETPARAMS                   niScalerSetParams;                    /** Client should access ::ni_scaler_set_params API through this pointer */
    PNIDEVICEALLOCFRAME                  niDeviceAllocFrame;                   /** Client should access ::ni_device_alloc_frame API through this pointer */
    PNIDEVICECONFIGFRAME                 niDeviceConfigFrame;                  /** Client should access ::ni_device_config_frame API through this pointer */
    PNIFRAMEBUFFERALLOCPIXFMT            niFrameBufferAllocPixfmt;             /** Client should access ::ni_frame_buffer_alloc_pixfmt API through this pointer */
    PNIAICONFIGNETWORKBINARY             niAiConfigNetworkBinary;              /** Client should access ::ni_ai_config_network_binary API through this pointer */
    PNIAIFRAMEBUFFERALLOC                niAiFrameBufferAlloc;                 /** Client should access ::ni_ai_frame_buffer_alloc API through this pointer */
    PNIAIPACKETBUFFERALLOC               niAiPacketBufferAlloc;                /** Client should access ::ni_ai_packet_buffer_alloc API through this pointer */
    PNIRECONFIGBITRATE                   niReconfigBitrate;                    /** Client should access ::ni_reconfig_bitrate API through this pointer */
    PNIFORCEIDRFRAMETYPE                 niForceIdrFrameType;                  /** Client should access ::ni_force_idr_frame_type API through this pointer */
    PNISETLTR                            niSetLtr;                             /** Client should access ::ni_set_ltr API through this pointer */
    PNISETLTRINTERVAL                    niSetLtrInterval;                     /** Client should access ::ni_set_ltr_interval API through this pointer */
    PNISETFRAMEREFINVALID                niSetFrameRefInvalid;                 /** Client should access ::ni_set_frame_ref_invalid API through this pointer */
    PNIRECONFIGFRAMERATE                 niReconfigFramerate;                  /** Client should access ::ni_reconfig_framerate API through this pointer */
    PNIDEVICESESSIONACQUIRE              niDeviceSessionAcquire;               /** Client should access ::ni_device_session_acquire API through this pointer */
    PNIUPLOADERFRAMEBUFFERLOCK           niUploaderFrameBufferLock;            /** Client should access ::ni_uploader_frame_buffer_lock API through this pointer */
    PNIUPLOADERFRAMEBUFFERUNLOCK         niUploaderFrameBufferUnlock;          /** Client should access ::ni_uploader_frame_buffer_unlock API through this pointer */
    PNIUPLOADERP2PTESTSEND               niUploaderP2PTestSend;                /** Client should access ::ni_uploader_p2p_test_send API through this pointer */
    PNIENCODERSETINPUTFRAMEFORMAT        niEncoderSetInputFrameFormat;         /** Client should access ::ni_encoder_set_input_frame_format API through this pointer */
    PNIUPLOADERSETFRAMEFORMAT            niUploaderSetFrameFormat;             /** Client should access ::ni_uploader_set_frame_format API through this pointer */
    PNIHWFRAMEP2PBUFFERRECYCLE           niHwframeP2PBufferRecycle;            /** Client should access ::ni_hwframe_p2p_buffer_recycle API through this pointer */
    PNIENCODERSESSIONREADSTREAMHEADER    niEncoderSessionReadStreamHeader;     /** Client should access ::ni_encoder_session_read_stream_header API through this pointer */
    PNIGETDMABUFFILEDESCRIPTOR           niGetDmaBufFileDescriptor;            /** Client should access ::ni_get_dma_buf_file_descriptor API through this pointer */
    PNIDEVICESESSIONSEQUENCECHANGE       niDeviceSessionSequenceChange;        /** Client should access ::ni_device_session_sequence_change API through this pointer */
} NETINT_LIBXCODER_API_FUNCTION_LIST;

class NETINTLibxcoderAPI {
public:
    // NiLibxcoderAPICreateInstance
    /**
     * Creates an instance of the NiLibxcoderAPI interface, and populates the
     * pFunctionList with function pointers to the API routines implemented by the
     * NiLibxcoderAPI interface.
     */
    static void NiLibxcoderAPICreateInstance(void *lib, NETINT_LIBXCODER_API_FUNCTION_LIST *functionList)
    {
        //
        // Function/symbol loading for ni_av_codec.h
        //
        functionList->niShouldSendSeiWithFrame = reinterpret_cast<decltype(ni_should_send_sei_with_frame)*>(dlsym(lib,"ni_should_send_sei_with_frame"));
        functionList->niDecRetrieveAuxData = reinterpret_cast<decltype(ni_dec_retrieve_aux_data)*>(dlsym(lib,"ni_dec_retrieve_aux_data"));
        functionList->niEncPrepAuxData = reinterpret_cast<decltype(ni_enc_prep_aux_data)*>(dlsym(lib,"ni_enc_prep_aux_data"));
        functionList->niEncCopyAuxData = reinterpret_cast<decltype(ni_enc_copy_aux_data)*>(dlsym(lib,"ni_enc_copy_aux_data"));
        //
        // Function/symbol loading for ni_util.h
        //
        functionList->niGetHwYuv420PDim = reinterpret_cast<decltype(ni_get_hw_yuv420p_dim)*>(dlsym(lib,"ni_get_hw_yuv420p_dim"));
        functionList->niCopyHwYuv420P = reinterpret_cast<decltype(ni_copy_hw_yuv420p)*>(dlsym(lib,"ni_copy_hw_yuv420p"));
        functionList->niInsertEmulationPreventBytes = reinterpret_cast<decltype(ni_insert_emulation_prevent_bytes)*>(dlsym(lib,"ni_insert_emulation_prevent_bytes"));
        functionList->niRemoveEmulationPreventBytes = reinterpret_cast<decltype(ni_remove_emulation_prevent_bytes)*>(dlsym(lib,"ni_remove_emulation_prevent_bytes"));
        functionList->niGettimeofday = reinterpret_cast<decltype(ni_gettimeofday)*>(dlsym(lib,"ni_gettimeofday"));
        functionList->niGettimeNs = reinterpret_cast<decltype(ni_gettime_ns)*>(dlsym(lib,"ni_gettime_ns"));
        functionList->niUsleep = reinterpret_cast<decltype(ni_usleep)*>(dlsym(lib,"ni_usleep"));
        functionList->niNetworkLayerConvertOutput = reinterpret_cast<decltype(ni_network_layer_convert_output)*>(dlsym(lib,"ni_network_layer_convert_output"));
        functionList->niAiNetworkLayerSize = reinterpret_cast<decltype(ni_ai_network_layer_size)*>(dlsym(lib,"ni_ai_network_layer_size"));
        functionList->niAiNetworkLayerDims = reinterpret_cast<decltype(ni_ai_network_layer_dims)*>(dlsym(lib,"ni_ai_network_layer_dims"));
        functionList->niNetworkLayerConvertTensor = reinterpret_cast<decltype(ni_network_layer_convert_tensor)*>(dlsym(lib,"ni_network_layer_convert_tensor"));
        functionList->niNetworkConvertTensorToData = reinterpret_cast<decltype(ni_network_convert_tensor_to_data)*>(dlsym(lib,"ni_network_convert_tensor_to_data"));
        functionList->niNetworkConvertDataToTensor = reinterpret_cast<decltype(ni_network_convert_data_to_tensor)*>(dlsym(lib,"ni_network_convert_data_to_tensor"));
        functionList->niCalculateSha256 = reinterpret_cast<decltype(ni_calculate_sha256)*>(dlsym(lib,"ni_calculate_sha256"));
        functionList->niCopyHwDescriptors = reinterpret_cast<decltype(ni_copy_hw_descriptors)*>(dlsym(lib,"ni_copy_hw_descriptors"));
        functionList->niGetLibxcoderApiVer = reinterpret_cast<decltype(ni_get_libxcoder_api_ver)*>(dlsym(lib,"ni_get_libxcoder_api_ver"));
        functionList->niGetCompatFwApiVer = reinterpret_cast<decltype(ni_get_compat_fw_api_ver)*>(dlsym(lib,"ni_get_compat_fw_api_ver"));
        functionList->niGetLibxcoderReleaseVer = reinterpret_cast<decltype(ni_get_libxcoder_release_ver)*>(dlsym(lib,"ni_get_libxcoder_release_ver"));
        //
        // Function/symbol loading for ni_device_api.h
        //
        functionList->niDeviceSessionContextAllocInit = reinterpret_cast<decltype(ni_device_session_context_alloc_init)*>(dlsym(lib,"ni_device_session_context_alloc_init"));
        functionList->niDeviceSessionContextInit = reinterpret_cast<decltype(ni_device_session_context_init)*>(dlsym(lib,"ni_device_session_context_init"));
        functionList->niDeviceSessionContextClear = reinterpret_cast<decltype(ni_device_session_context_clear)*>(dlsym(lib,"ni_device_session_context_clear"));
        functionList->niDeviceSessionContextFree = reinterpret_cast<decltype(ni_device_session_context_free)*>(dlsym(lib,"ni_device_session_context_free"));
        functionList->niCreateEvent = reinterpret_cast<decltype(ni_create_event)*>(dlsym(lib,"ni_create_event"));
        functionList->niCloseEvent = reinterpret_cast<decltype(ni_close_event)*>(dlsym(lib,"ni_close_event"));
        functionList->niDeviceOpen = reinterpret_cast<decltype(ni_device_open)*>(dlsym(lib,"ni_device_open"));
        functionList->niDeviceClose = reinterpret_cast<decltype(ni_device_close)*>(dlsym(lib,"ni_device_close"));
        functionList->niDeviceCapabilityQuery = reinterpret_cast<decltype(ni_device_capability_query)*>(dlsym(lib,"ni_device_capability_query"));
        functionList->niDeviceSessionOpen = reinterpret_cast<decltype(ni_device_session_open)*>(dlsym(lib,"ni_device_session_open"));
        functionList->niDeviceSessionClose = reinterpret_cast<decltype(ni_device_session_close)*>(dlsym(lib,"ni_device_session_close"));
        functionList->niDeviceSessionFlush = reinterpret_cast<decltype(ni_device_session_flush)*>(dlsym(lib,"ni_device_session_flush"));
        functionList->niDeviceDecSessionSaveHdrs = reinterpret_cast<decltype(ni_device_dec_session_save_hdrs)*>(dlsym(lib,"ni_device_dec_session_save_hdrs"));
        functionList->niDeviceDecSessionFlush = reinterpret_cast<decltype(ni_device_dec_session_flush)*>(dlsym(lib,"ni_device_dec_session_flush"));
        functionList->niDeviceSessionWrite = reinterpret_cast<decltype(ni_device_session_write)*>(dlsym(lib,"ni_device_session_write"));
        functionList->niDeviceSessionRead = reinterpret_cast<decltype(ni_device_session_read)*>(dlsym(lib,"ni_device_session_read"));
        functionList->niDeviceSessionQuery = reinterpret_cast<decltype(ni_device_session_query)*>(dlsym(lib,"ni_device_session_query"));
        functionList->niFrameBufferAlloc = reinterpret_cast<decltype(ni_frame_buffer_alloc)*>(dlsym(lib,"ni_frame_buffer_alloc"));
        functionList->niDecoderFrameBufferAlloc = reinterpret_cast<decltype(ni_decoder_frame_buffer_alloc)*>(dlsym(lib,"ni_decoder_frame_buffer_alloc"));
        functionList->niEncoderFrameBufferAlloc = reinterpret_cast<decltype(ni_encoder_frame_buffer_alloc)*>(dlsym(lib,"ni_encoder_frame_buffer_alloc"));
        functionList->niFrameBufferAllocNv = reinterpret_cast<decltype(ni_frame_buffer_alloc_nv)*>(dlsym(lib,"ni_frame_buffer_alloc_nv"));
        functionList->niEncoderSwFrameBufferAlloc = reinterpret_cast<decltype(ni_encoder_sw_frame_buffer_alloc)*>(dlsym(lib,"ni_encoder_sw_frame_buffer_alloc"));
        functionList->niFrameBufferFree = reinterpret_cast<decltype(ni_frame_buffer_free)*>(dlsym(lib,"ni_frame_buffer_free"));
        functionList->niDecoderFrameBufferFree = reinterpret_cast<decltype(ni_decoder_frame_buffer_free)*>(dlsym(lib,"ni_decoder_frame_buffer_free"));
        functionList->niDecoderFrameBufferPoolReturnBuf = reinterpret_cast<decltype(ni_decoder_frame_buffer_pool_return_buf)*>(dlsym(lib,"ni_decoder_frame_buffer_pool_return_buf"));
        functionList->niPacketBufferAlloc = reinterpret_cast<decltype(ni_packet_buffer_alloc)*>(dlsym(lib,"ni_packet_buffer_alloc"));
        functionList->niCustomPacketBufferAlloc = reinterpret_cast<decltype(ni_custom_packet_buffer_alloc)*>(dlsym(lib,"ni_custom_packet_buffer_alloc"));
        functionList->niPacketBufferFree = reinterpret_cast<decltype(ni_packet_buffer_free)*>(dlsym(lib,"ni_packet_buffer_free"));
        functionList->niPacketBufferFreeAv1 = reinterpret_cast<decltype(ni_packet_buffer_free_av1)*>(dlsym(lib,"ni_packet_buffer_free_av1"));
        functionList->niPacketCopy = reinterpret_cast<decltype(ni_packet_copy)*>(dlsym(lib,"ni_packet_copy"));
        functionList->niFrameNewAuxData = reinterpret_cast<decltype(ni_frame_new_aux_data)*>(dlsym(lib,"ni_frame_new_aux_data"));
        functionList->niFrameNewAuxDataFromRawData = reinterpret_cast<decltype(ni_frame_new_aux_data_from_raw_data)*>(dlsym(lib,"ni_frame_new_aux_data_from_raw_data"));
        functionList->niFrameGetAuxData = reinterpret_cast<decltype(ni_frame_get_aux_data)*>(dlsym(lib,"ni_frame_get_aux_data"));
        functionList->niFrameFreeAuxData = reinterpret_cast<decltype(ni_frame_free_aux_data)*>(dlsym(lib,"ni_frame_free_aux_data"));
        functionList->niFrameWipeAuxData = reinterpret_cast<decltype(ni_frame_wipe_aux_data)*>(dlsym(lib,"ni_frame_wipe_aux_data"));
        functionList->niEncoderInitDefaultParams = reinterpret_cast<decltype(ni_encoder_init_default_params)*>(dlsym(lib,"ni_encoder_init_default_params"));
        functionList->niDecoderInitDefaultParams = reinterpret_cast<decltype(ni_decoder_init_default_params)*>(dlsym(lib,"ni_decoder_init_default_params"));
        functionList->niEncoderParamsSetValue = reinterpret_cast<decltype(ni_encoder_params_set_value)*>(dlsym(lib,"ni_encoder_params_set_value"));
        functionList->niDecoderParamsSetValue = reinterpret_cast<decltype(ni_decoder_params_set_value)*>(dlsym(lib,"ni_decoder_params_set_value"));
        functionList->niEncoderGopParamsSetValue = reinterpret_cast<decltype(ni_encoder_gop_params_set_value)*>(dlsym(lib,"ni_encoder_gop_params_set_value"));
        functionList->niDeviceSessionCopy = reinterpret_cast<decltype(ni_device_session_copy)*>(dlsym(lib,"ni_device_session_copy"));
        functionList->niDeviceSessionInitFramepool = reinterpret_cast<decltype(ni_device_session_init_framepool)*>(dlsym(lib,"ni_device_session_init_framepool"));
        functionList->niDeviceSessionReadHwdesc = reinterpret_cast<decltype(ni_device_session_read_hwdesc)*>(dlsym(lib,"ni_device_session_read_hwdesc"));
        functionList->niDeviceSessionHwdl = reinterpret_cast<decltype(ni_device_session_hwdl)*>(dlsym(lib,"ni_device_session_hwdl"));
        functionList->niDeviceSessionHwup = reinterpret_cast<decltype(ni_device_session_hwup)*>(dlsym(lib,"ni_device_session_hwup"));
        functionList->niFrameBufferAllocHwenc = reinterpret_cast<decltype(ni_frame_buffer_alloc_hwenc)*>(dlsym(lib,"ni_frame_buffer_alloc_hwenc"));
        functionList->niHwframeBufferRecycle = reinterpret_cast<decltype(ni_hwframe_buffer_recycle)*>(dlsym(lib,"ni_hwframe_buffer_recycle"));
        functionList->niScalerSetParams = reinterpret_cast<decltype(ni_scaler_set_params)*>(dlsym(lib,"ni_scaler_set_params"));
        functionList->niDeviceAllocFrame = reinterpret_cast<decltype(ni_device_alloc_frame)*>(dlsym(lib,"ni_device_alloc_frame"));
        functionList->niDeviceConfigFrame = reinterpret_cast<decltype(ni_device_config_frame)*>(dlsym(lib,"ni_device_config_frame"));
        functionList->niFrameBufferAllocPixfmt = reinterpret_cast<decltype(ni_frame_buffer_alloc_pixfmt)*>(dlsym(lib,"ni_frame_buffer_alloc_pixfmt"));
        functionList->niAiConfigNetworkBinary = reinterpret_cast<decltype(ni_ai_config_network_binary)*>(dlsym(lib,"ni_ai_config_network_binary"));
        functionList->niAiFrameBufferAlloc = reinterpret_cast<decltype(ni_ai_frame_buffer_alloc)*>(dlsym(lib,"ni_ai_frame_buffer_alloc"));
        functionList->niAiPacketBufferAlloc = reinterpret_cast<decltype(ni_ai_packet_buffer_alloc)*>(dlsym(lib,"ni_ai_packet_buffer_alloc"));
        functionList->niReconfigBitrate = reinterpret_cast<decltype(ni_reconfig_bitrate)*>(dlsym(lib,"ni_reconfig_bitrate"));
        functionList->niForceIdrFrameType = reinterpret_cast<decltype(ni_force_idr_frame_type)*>(dlsym(lib,"ni_force_idr_frame_type"));
        functionList->niSetLtr = reinterpret_cast<decltype(ni_set_ltr)*>(dlsym(lib,"ni_set_ltr"));
        functionList->niSetLtrInterval = reinterpret_cast<decltype(ni_set_ltr_interval)*>(dlsym(lib,"ni_set_ltr_interval"));
        functionList->niSetFrameRefInvalid = reinterpret_cast<decltype(ni_set_frame_ref_invalid)*>(dlsym(lib,"ni_set_frame_ref_invalid"));
        functionList->niReconfigFramerate = reinterpret_cast<decltype(ni_reconfig_framerate)*>(dlsym(lib,"ni_reconfig_framerate"));
        functionList->niDeviceSessionAcquire = reinterpret_cast<decltype(ni_device_session_acquire)*>(dlsym(lib,"ni_device_session_acquire"));
        functionList->niUploaderFrameBufferLock = reinterpret_cast<decltype(ni_uploader_frame_buffer_lock)*>(dlsym(lib,"ni_uploader_frame_buffer_lock"));
        functionList->niUploaderFrameBufferUnlock = reinterpret_cast<decltype(ni_uploader_frame_buffer_unlock)*>(dlsym(lib,"ni_uploader_frame_buffer_unlock"));
        functionList->niUploaderP2PTestSend = reinterpret_cast<decltype(ni_uploader_p2p_test_send)*>(dlsym(lib,"ni_uploader_p2p_test_send"));
        functionList->niEncoderSetInputFrameFormat = reinterpret_cast<decltype(ni_encoder_set_input_frame_format)*>(dlsym(lib,"ni_encoder_set_input_frame_format"));
        functionList->niUploaderSetFrameFormat = reinterpret_cast<decltype(ni_uploader_set_frame_format)*>(dlsym(lib,"ni_uploader_set_frame_format"));
        functionList->niHwframeP2PBufferRecycle = reinterpret_cast<decltype(ni_hwframe_p2p_buffer_recycle)*>(dlsym(lib,"ni_hwframe_p2p_buffer_recycle"));
        functionList->niEncoderSessionReadStreamHeader = reinterpret_cast<decltype(ni_encoder_session_read_stream_header)*>(dlsym(lib,"ni_encoder_session_read_stream_header"));
        functionList->niGetDmaBufFileDescriptor = reinterpret_cast<decltype(ni_get_dma_buf_file_descriptor)*>(dlsym(lib,"ni_get_dma_buf_file_descriptor"));
        functionList->niDeviceSessionSequenceChange = reinterpret_cast<decltype(ni_device_session_sequence_change)*>(dlsym(lib,"ni_device_session_sequence_change"));
    }
};


#endif // _NETINTLIBXCODERAPI_H_

