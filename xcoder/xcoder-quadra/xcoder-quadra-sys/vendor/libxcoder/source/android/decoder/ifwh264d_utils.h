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
#ifndef _IFWH264D_UTILS_H_
#define _IFWH264D_UTILS_H_
/*!
**************************************************************************
* \file ih264d_utils.h
*
* \brief
*    Contains declaration of routines
*    that handle of start and end of pic processing
*
* \date
*    19/12/2002
*
* \author  AI
**************************************************************************
*/
#include "ifwh264d_defs.h"
#include "ifwh264d_structs.h"

#define PS_DEC_ALIGNED_FREE(ps_dec, y)                                         \
    if (y)                                                                     \
    {                                                                          \
        ps_dec->pf_aligned_free(ps_dec->pv_mem_ctxt, ((void *)y));             \
        (y) = NULL;                                                            \
    }

#endif /* _IFWH264D_UTILS_H_ */
