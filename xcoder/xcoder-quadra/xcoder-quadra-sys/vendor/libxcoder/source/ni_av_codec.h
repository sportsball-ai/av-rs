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
*  \file   ni_av_codec.h
*
*  \brief  NETINT audio/video related utility functions
*
*******************************************************************************/

#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include "ni_device_api.h"

// libxcoder API related definitions

#define NI_NUM_PIXEL_ASPECT_RATIO 17
static const ni_rational_t
    ni_h264_pixel_aspect_list[NI_NUM_PIXEL_ASPECT_RATIO] = {
        {0, 1},   {1, 1},    {12, 11}, {10, 11}, {16, 11}, {40, 33},
        {24, 11}, {20, 11},  {32, 11}, {80, 33}, {18, 11}, {15, 11},
        {64, 33}, {160, 99}, {4, 3},   {3, 2},   {2, 1},
};

typedef enum _ni_h264_sei_type_t
{
    NI_H264_SEI_TYPE_BUFFERING_PERIOD = 0,   // buffering period (H.264, D.1.1)
    NI_H264_SEI_TYPE_PIC_TIMING = 1,         // picture timing
    NI_H264_SEI_TYPE_PAN_SCAN_RECT = 2,      // pan-scan rectangle
    NI_H264_SEI_TYPE_FILLER_PAYLOAD = 3,     // filler data
    NI_H264_SEI_TYPE_USER_DATA_REGISTERED =
        4,   // registered user data as specified by Rec. ITU-T T.35
    NI_H264_SEI_TYPE_USER_DATA_UNREGISTERED = 5,   // unregistered user data
    NI_H264_SEI_TYPE_RECOVERY_POINT =
        6,   // recovery point (frame # to decoder sync)
    NI_H264_SEI_TYPE_FRAME_PACKING = 45,         // frame packing arrangement
    NI_H264_SEI_TYPE_DISPLAY_ORIENTATION = 47,   // display orientation
    NI_H264_SEI_TYPE_GREEN_METADATA = 56,        // GreenMPEG information
    NI_H264_SEI_TYPE_MASTERING_DISPLAY_COLOUR_VOLUME =
        137,   // mastering display properties
    NI_H264_SEI_TYPE_ALTERNATIVE_TRANSFER = 147,   // alternative transfer
} ni_h264_sei_type_t;

// pic_struct in picture timing SEI message
typedef enum _ni_h264_sei_pic_struct_t
{
    NI_H264_SEI_PIC_STRUCT_FRAME = 0,          //  0: %frame
    NI_H264_SEI_PIC_STRUCT_TOP_FIELD = 1,      //  1: top field
    NI_H264_SEI_PIC_STRUCT_BOTTOM_FIELD = 2,   //  2: bottom field
    NI_H264_SEI_PIC_STRUCT_TOP_BOTTOM =
        3,   //  3: top field, bottom field, in that order
    NI_H264_SEI_PIC_STRUCT_BOTTOM_TOP =
        4,   //  4: bottom field, top field, in that order
    NI_H264_SEI_PIC_STRUCT_TOP_BOTTOM_TOP =
        5,   //  5: top field, bottom field, top field repeated, in that order
    NI_H264_SEI_PIC_STRUCT_BOTTOM_TOP_BOTTOM =
        6,   //  6: bottom field, top field, bottom field repeated, in that order
    NI_H264_SEI_PIC_STRUCT_FRAME_DOUBLING = 7,   //  7: %frame doubling
    NI_H264_SEI_PIC_STRUCT_FRAME_TRIPLING = 8    //  8: %frame tripling
} ni_h264_sei_pic_struct_t;

/* Chromaticity coordinates of the source primaries.
 * These values match the ones defined by ISO/IEC 23001-8_2013 § 7.1.
 */
typedef enum _ni_color_primaries
{
    NI_COL_PRI_RESERVED0 = 0,
    NI_COL_PRI_BT709 =
        1,   //< also ITU-R BT1361 / IEC 61966-2-4 / SMPTE RP177 Annex B
    NI_COL_PRI_UNSPECIFIED = 2,
    NI_COL_PRI_RESERVED = 3,
    NI_COL_PRI_BT470M =
        4,   //< also FCC Title 47 Code of Federal Regulations 73.682 (a)(20)

    NI_COL_PRI_BT470BG =
        5,   //< also ITU-R BT601-6 625 / ITU-R BT1358 625 / ITU-R BT1700 625 PAL & SECAM
    NI_COL_PRI_SMPTE170M =
        6,   //< also ITU-R BT601-6 525 / ITU-R BT1358 525 / ITU-R BT1700 NTSC
    NI_COL_PRI_SMPTE240M = 7,   //< functionally identical to above
    NI_COL_PRI_FILM = 8,        //< colour filters using Illuminant C
    NI_COL_PRI_BT2020 = 9,      //< ITU-R BT2020
    NI_COL_PRI_SMPTE428 = 10,   //< SMPTE ST 428-1 (CIE 1931 XYZ)
    NI_COL_PRI_SMPTEST428_1 = NI_COL_PRI_SMPTE428,
    NI_COL_PRI_SMPTE431 = 11,    //< SMPTE ST 431-2 (2011) / DCI P3
    NI_COL_PRI_SMPTE432 = 12,    //< SMPTE ST 432-1 (2010) / P3 D65 / Display P3
    NI_COL_PRI_JEDEC_P22 = 22,   //< JEDEC P22 phosphors
    NI_COL_PRI_NB                //< Not part of ABI
} ni_color_primaries_t;

/* Color Transfer Characteristic.
 * These values match the ones defined by ISO/IEC 23001-8_2013 § 7.2.
 */
typedef enum _ni_color_transfer_characteristic
{
    NI_COL_TRC_RESERVED0 = 0,
    NI_COL_TRC_BT709 = 1,   //< also ITU-R BT1361
    NI_COL_TRC_UNSPECIFIED = 2,
    NI_COL_TRC_RESERVED = 3,
    NI_COL_TRC_GAMMA22 =
        4,   //< also ITU-R BT470M / ITU-R BT1700 625 PAL & SECAM
    NI_COL_TRC_GAMMA28 = 5,   //< also ITU-R BT470BG
    NI_COL_TRC_SMPTE170M =
        6,   //< also ITU-R BT601-6 525 or 625 / ITU-R BT1358 525 or 625 / ITU-R BT1700 NTSC
    NI_COL_TRC_SMPTE240M = 7,
    NI_COL_TRC_LINEAR = 8,   //< "Linear transfer characteristics"
    NI_COL_TRC_LOG =
        9,   //< "Logarithmic transfer characteristic (100:1 range)"
    NI_COL_TRC_LOG_SQRT =
        10,   //< "Logarithmic transfer characteristic (100 * Sqrt(10) : 1 range)"
    NI_COL_TRC_IEC61966_2_4 = 11,   //< IEC 61966-2-4
    NI_COL_TRC_BT1361_ECG = 12,     //< ITU-R BT1361 Extended Colour Gamut
    NI_COL_TRC_IEC61966_2_1 = 13,   //< IEC 61966-2-1 (sRGB or sYCC)
    NI_COL_TRC_BT2020_10 = 14,      //< ITU-R BT2020 for 10-bit system
    NI_COL_TRC_BT2020_12 = 15,      //< ITU-R BT2020 for 12-bit system
    NI_COL_TRC_SMPTE2084 =
        16,   //< SMPTE ST 2084 for 10-, 12-, 14- and 16-bit systems
    NI_COL_TRC_SMPTEST2084 = NI_COL_TRC_SMPTE2084,
    NI_COL_TRC_SMPTE428 = 17,   //< SMPTE ST 428-1
    NI_COL_TRC_SMPTEST428_1 = NI_COL_TRC_SMPTE428,
    NI_COL_TRC_ARIB_STD_B67 =
        18,         //< ARIB STD-B67, known as "Hybrid log-gamma"
    NI_COL_TRC_NB   //< Not part of ABI
} ni_color_transfer_characteristic_t;

/**
 * YUV colorspace type.
 * These values match the ones defined by ISO/IEC 23001-8_2013 § 7.3.
 */
typedef enum _ni_color_space
{
    NI_COL_SPC_RGB =
        0,   //< order of coefficients is actually GBR, also IEC 61966-2-1 (sRGB)
    NI_COL_SPC_BT709 =
        1,   //< also ITU-R BT1361 / IEC 61966-2-4 xvYCC709 / SMPTE RP177 Annex B
    NI_COL_SPC_UNSPECIFIED = 2,
    NI_COL_SPC_RESERVED = 3,
    NI_COL_SPC_FCC =
        4,   //< FCC Title 47 Code of Federal Regulations 73.682 (a)(20)
    NI_COL_SPC_BT470BG =
        5,   //< also ITU-R BT601-6 625 / ITU-R BT1358 625 / ITU-R BT1700 625 PAL & SECAM / IEC 61966-2-4 xvYCC601
    NI_COL_SPC_SMPTE170M =
        6,   //< also ITU-R BT601-6 525 / ITU-R BT1358 525 / ITU-R BT1700 NTSC
    NI_COL_SPC_SMPTE240M = 7,   //< functionally identical to above
    NI_COL_SPC_YCGCO =
        8,   //< Used by Dirac / VC-2 and H.264 FRext, see ITU-T SG16
    NI_COL_SPC_YCOCG = NI_COL_SPC_YCGCO,
    NI_COL_SPC_BT2020_NCL = 9,   //< ITU-R BT2020 non-constant luminance system
    NI_COL_SPC_BT2020_CL = 10,   //< ITU-R BT2020 constant luminance system
    NI_COL_SPC_SMPTE2085 = 11,   //< SMPTE 2085, Y'D'zD'x
    NI_COL_SPC_CHROMA_DERIVED_NCL =
        12,   //< Chromaticity-derived non-constant luminance system
    NI_COL_SPC_CHROMA_DERIVED_CL =
        13,                  //< Chromaticity-derived constant luminance system
    NI_COL_SPC_ICTCP = 14,   //< ITU-R BT.2100-0, ICtCp
    NI_COL_SPC_NB            //< Not part of ABI
} ni_color_space_t;

// HRD parameters
typedef struct _ni_hrd_params
{
    uint32_t au_cpb_removal_delay_length_minus1;
    uint32_t dpb_output_delay_length_minus1;
    uint32_t initial_cpb_removal_delay_length_minus1;
    int64_t bit_rate_unscale;
    int64_t cpb_size_unscale;
    uint32_t au_cpb_removal_delay_minus1;
} ni_hrd_params_t;

// struct describing HDR10 mastering display metadata
typedef struct _ni_mastering_display_metadata
{
    // CIE 1931 xy chromaticity coords of color primaries (r, g, b order).
    ni_rational_t display_primaries[3][2];

    // CIE 1931 xy chromaticity coords of white point.
    ni_rational_t white_point[2];

    // Min luminance of mastering display (cd/m^2).
    ni_rational_t min_luminance;

    // Max luminance of mastering display (cd/m^2).
    ni_rational_t max_luminance;

    // Flag indicating whether the display primaries (and white point) are set.
    int has_primaries;

    // Flag indicating whether the luminance (min_ and max_) have been set.
    int has_luminance;
} ni_mastering_display_metadata_t;

// struct describing HDR10 Content light level
typedef struct _ni_content_light_level
{
    // Max content light level (cd/m^2).
    uint16_t max_cll;

    // Max average light level per frame (cd/m^2).
    uint16_t max_fall;
} ni_content_light_level_t;

// struct and enum for HDR10+
// Option for overlapping elliptical pixel selectors in an image.
typedef enum _ni_hdr_plus_overlap_process_option
{
    NI_HDR_PLUS_OVERLAP_PROCESS_WEIGHTED_AVERAGING = 0,
    NI_HDR_PLUS_OVERLAP_PROCESS_LAYERING = 1,
} ni_hdr_plus_overlap_process_option_t;

// struct that represents the percentile at a specific percentage in
// a distribution.
typedef struct _ni_hdr_plus_percentile
{
    // The percentage value corresponding to a specific percentile linearized
    // RGB value in the processing window in the scene. The value shall be in
    // the range of 0 to 100, inclusive.
    uint8_t percentage;

    // The linearized maxRGB value at a specific percentile in the processing
    // window in the scene. The value shall be in the range of 0 to 1, inclusive
    // and in multiples of 0.00001.
    ni_rational_t percentile;
} ni_hdr_plus_percentile_t;

// struct describing color transform parameters at a processing window in a
// dynamic metadata for SMPTE 2094-40.
typedef struct _ni_hdr_plus_color_transform_params
{
    // The relative x coordinate of the top left pixel of the processing
    // window. The value shall be in the range of 0 and 1, inclusive and
    //  in multiples of 1/(width of Picture - 1). The value 1 corresponds
    //  to the absolute coordinate of width of Picture - 1. The value for
    //  first processing window shall be 0.
    ni_rational_t window_upper_left_corner_x;

    //  The relative y coordinate of the top left pixel of the processing
    //  window. The value shall be in the range of 0 and 1, inclusive and
    //  in multiples of 1/(height of Picture - 1). The value 1 corresponds
    //  to the absolute coordinate of height of Picture - 1. The value for
    //  first processing window shall be 0.
    ni_rational_t window_upper_left_corner_y;

    //  The relative x coordinate of the bottom right pixel of the processing
    //  window. The value shall be in the range of 0 and 1, inclusive and
    //  in multiples of 1/(width of Picture - 1). The value 1 corresponds
    //  to the absolute coordinate of width of Picture - 1. The value for
    //  first processing window shall be 1.
    ni_rational_t window_lower_right_corner_x;

    //  The relative y coordinate of the bottom right pixel of the processing
    //  window. The value shall be in the range of 0 and 1, inclusive and
    //  in multiples of 1/(height of Picture - 1). The value 1 corresponds
    //  to the absolute coordinate of height of Picture - 1. The value for
    //  first processing window shall be 1.
    ni_rational_t window_lower_right_corner_y;

    //  The x coordinate of the center position of the concentric internal and
    //  external ellipses of the elliptical pixel selector in the processing
    //  window. The value shall be in the range of 0 to (width of Picture - 1),
    //  inclusive and in multiples of 1 pixel.
    uint16_t center_of_ellipse_x;

    //  The y coordinate of the center position of the concentric internal and
    //  external ellipses of the elliptical pixel selector in the processing
    //  window. The value shall be in the range of 0 to (height of Picture - 1),
    //  inclusive and in multiples of 1 pixel.
    uint16_t center_of_ellipse_y;

    //  The clockwise rotation angle in degree of arc with respect to the
    //  positive direction of the x-axis of the concentric internal and external
    //  ellipses of the elliptical pixel selector in the processing window. The
    //  value shall be in the range of 0 to 180, inclusive and in multiples of
    //  1.
    uint8_t rotation_angle;

    //  The semi-major axis value of the internal ellipse of the elliptical
    //  pixel selector in amount of pixels in the processing window. The value
    //  shall be in the range of 1 to 65535, inclusive and in multiples of 1
    //  pixel.
    uint16_t semimajor_axis_internal_ellipse;

    //  The semi-major axis value of the external ellipse of the elliptical
    //  pixel selector in amount of pixels in the processing window. The value
    //  shall not be less than semimajor_axis_internal_ellipse of the current
    //  processing window. The value shall be in the range of 1 to 65535,
    //  inclusive and in multiples of 1 pixel.
    uint16_t semimajor_axis_external_ellipse;

    //  The semi-minor axis value of the external ellipse of the elliptical
    //  pixel selector in amount of pixels in the processing window. The value
    //  shall be in the range of 1 to 65535, inclusive and in multiples of 1
    //  pixel.
    uint16_t semiminor_axis_external_ellipse;

    //  Overlap process option indicates one of the two methods of combining
    //  rendered pixels in the processing window in an image with at least one
    //  elliptical pixel selector. For overlapping elliptical pixel selectors
    //  in an image, overlap_process_option shall have the same value.
    ni_hdr_plus_overlap_process_option_t overlap_process_option;

    //  The maximum of the color components of linearized RGB values in the
    //  processing window in the scene. The values should be in the range of 0
    //  to 1, inclusive and in multiples of 0.00001. maxscl[ 0 ], maxscl[ 1 ],
    //  and maxscl[ 2 ] are corresponding to R, G, B color components
    //  respectively.
    ni_rational_t maxscl[3];

    //  The average of linearized maxRGB values in the processing window in the
    //  scene. The value should be in the range of 0 to 1, inclusive and in
    //  multiples of 0.00001.
    ni_rational_t average_maxrgb;

    //  The number of linearized maxRGB values at given percentiles in the
    //  processing window in the scene. The maximum value shall be 15.
    uint8_t num_distribution_maxrgb_percentiles;

    //  The linearized maxRGB values at given percentiles in the
    //  processing window in the scene.
    ni_hdr_plus_percentile_t distribution_maxrgb[15];

    //  The fraction of selected pixels in the image that contains the brightest
    //  pixel in the scene. The value shall be in the range of 0 to 1, inclusive
    //  and in multiples of 0.001.
    ni_rational_t fraction_bright_pixels;

    //  This flag indicates that the metadata for the tone mapping function in
    //  the processing window is present (for value of 1).
    uint8_t tone_mapping_flag;

    //  The x coordinate of the separation point between the linear part and the
    //  curved part of the tone mapping function. The value shall be in the
    //  range of 0 to 1, excluding 0 and in multiples of 1/4095.
    ni_rational_t knee_point_x;

    //  The y coordinate of the separation point between the linear part and the
    //  curved part of the tone mapping function. The value shall be in the
    //  range of 0 to 1, excluding 0 and in multiples of 1/4095.
    ni_rational_t knee_point_y;

    //  The number of the intermediate anchor parameters of the tone mapping
    //  function in the processing window. The maximum value shall be 15.
    uint8_t num_bezier_curve_anchors;

    //  The intermediate anchor parameters of the tone mapping function in the
    //  processing window in the scene. The values should be in the range of 0
    //  to 1, inclusive and in multiples of 1/1023.
    ni_rational_t bezier_curve_anchors[15];

    //  This flag shall be equal to 0 in bitstreams conforming to this version
    //  of this Specification. Other values are reserved for future use.
    uint8_t color_saturation_mapping_flag;

    //  The color saturation gain in the processing window in the scene. The
    //  value shall be in the range of 0 to 63/8, inclusive and in multiples of
    //  1/8. The default value shall be 1.
    ni_rational_t color_saturation_weight;
} ni_hdr_plus_color_transform_params_t;

// struct representing dynamic metadata for color volume transform -
// application 4 of SMPTE 2094-40:2016 standard.
typedef struct _ni_dynamic_hdr_plus
{
    // Country code by Rec. ITU-T T.35 Annex A. The value shall be 0xB5.
    uint8_t itu_t_t35_country_code;

    //  Application version in the application defining document in ST-2094
    //  suite. The value shall be set to 0.
    uint8_t application_version;

    //  The number of processing windows. The value shall be in the range
    //  of 1 to 3, inclusive.
    uint8_t num_windows;

    //  The color transform parameters for every processing window.
    ni_hdr_plus_color_transform_params_t params[3];

    //  The nominal maximum display luminance of the targeted system display,
    //  in units of 0.0001 candelas per square metre. The value shall be in
    //  the range of 0 to 10000, inclusive.
    ni_rational_t targeted_system_display_maximum_luminance;

    //  This flag shall be equal to 0 in bit streams conforming to this version
    //  of this Specification. The value 1 is reserved for future use.
    uint8_t targeted_system_display_actual_peak_luminance_flag;

    //  The number of rows in the targeted system_display_actual_peak_luminance
    //  array. The value shall be in the range of 2 to 25, inclusive.
    uint8_t num_rows_targeted_system_display_actual_peak_luminance;

    //  The number of columns in the
    //  targeted_system_display_actual_peak_luminance array. The value shall be
    //  in the range of 2 to 25, inclusive.
    uint8_t num_cols_targeted_system_display_actual_peak_luminance;

    //  The normalized actual peak luminance of the targeted system display. The
    //  values should be in the range of 0 to 1, inclusive and in multiples of
    //  1/15.
    ni_rational_t targeted_system_display_actual_peak_luminance[25][25];

    //  This flag shall be equal to 0 in bitstreams conforming to this version
    //  of this Specification. The value 1 is reserved for future use.
    uint8_t mastering_display_actual_peak_luminance_flag;

    //  The number of rows in the mastering_display_actual_peak_luminance array.
    //  The value shall be in the range of 2 to 25, inclusive.
    uint8_t num_rows_mastering_display_actual_peak_luminance;

    //  The number of columns in the mastering_display_actual_peak_luminance
    //  array. The value shall be in the range of 2 to 25, inclusive.
    uint8_t num_cols_mastering_display_actual_peak_luminance;

    //  The normalized actual peak luminance of the mastering display used for
    //  mastering the image essence. The values should be in the range of 0 to
    //  1, inclusive and in multiples of 1/15.
    ni_rational_t mastering_display_actual_peak_luminance[25][25];
} ni_dynamic_hdr_plus_t;

/*!*****************************************************************************
 *  \brief  Whether SEI should be sent together with this frame to encoder
 *
 *  \param[in]  p_enc_ctx encoder session context
 *  \param[in]  pic_type frame type
 *  \param[in]  p_param encoder parameters
 *
 *  \return 1 if yes, 0 otherwise
 ******************************************************************************/
LIB_API int ni_should_send_sei_with_frame(ni_session_context_t *p_enc_ctx,
                                          ni_pic_type_t pic_type,
                                          ni_xcoder_params_t *p_param);

/*!*****************************************************************************
 *  \brief  Retrieve auxiliary data (close caption, various SEI) associated with
 *          this frame that is returned by decoder, convert them to appropriate
 *          format and save them in the frame's auxiliary data storage for
 *          future use by encoding. Usually they would be sent together with
 *          this frame to encoder at encoding.
 *
 *  \param[in/out]  frame that is returned by decoder
 *
 *  \return NONE
 ******************************************************************************/
LIB_API void ni_dec_retrieve_aux_data(ni_frame_t *frame);

/*!*****************************************************************************
 *  \brief  Prepare auxiliary data that should be sent together with this frame
 *          to encoder based on the auxiliary data of the decoded frame.
 *
 *  \param[in/out]  p_enc_ctx encoder session context whose various SEI type
 *                  header can be updated as the result of this function
 *  \param[out]  p_enc_frame frame to be sent to encoder
 *  \param[in]  p_dec_frame frame that is returned by decoder
 *  \param[in]  codec_format H.264 or H.265
 *  \param[in]  should_send_sei_with_frame if need to send a certain type of
 *              SEI with this frame
 *  \param[out]  mdcv_data SEI for HDR mastering display color volume info
 *  \param[out]  cll_data SEI for HDR content light level info
 *  \param[out]  cc_data SEI for close caption
 *  \param[out]  udu_data SEI for User data unregistered
 *  \param[out]  hdrp_data SEI for HDR10+
 *
 *  \return NONE
 ******************************************************************************/
LIB_API void
ni_enc_prep_aux_data(ni_session_context_t *p_enc_ctx, ni_frame_t *p_enc_frame,
                     ni_frame_t *p_dec_frame, ni_codec_format_t codec_format,
                     int should_send_sei_with_frame, uint8_t *mdcv_data,
                     uint8_t *cll_data, uint8_t *cc_data, uint8_t *udu_data,
                     uint8_t *hdrp_data);

/*!*****************************************************************************
 *  \brief  Copy auxiliary data that should be sent together with this frame
 *          to encoder.
 *
 *  \param[in]  p_enc_ctx encoder session context
 *  \param[out]  p_enc_frame frame to be sent to encoder
 *  \param[in]  p_dec_frame frame returned by decoder
 *  \param[in]  codec_format H.264 or H.265
 *  \param[in]  mdcv_data SEI for HDR mastering display color volume info
 *  \param[in]  cll_data SEI for HDR content light level info
 *  \param[in]  cc_data SEI for close caption
 *  \param[in]  udu_data SEI for User data unregistered
 *  \param[in]  hdrp_data SEI for HDR10+
 *  \param[in]  is_hwframe, must be 0 (sw frame) or 1 (hw frame)
 *
 *  \return NONE
 ******************************************************************************/
LIB_API void
ni_enc_copy_aux_data(ni_session_context_t *p_enc_ctx, ni_frame_t *p_enc_frame,
                     ni_frame_t *p_dec_frame, ni_codec_format_t codec_format,
                     const uint8_t *mdcv_data, const uint8_t *cll_data,
                     const uint8_t *cc_data, const uint8_t *udu_data,
                     const uint8_t *hdrp_data, int is_hwframe,
                     int is_nv12frame);

#ifdef __cplusplus
}
#endif
