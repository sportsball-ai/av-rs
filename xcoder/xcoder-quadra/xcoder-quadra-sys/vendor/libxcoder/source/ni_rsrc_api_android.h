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
 *  \file   ni_rsrc_api_android.h
 *
 *  \brief  Public definitions for managing NETINT video processing devices on
 *          Android
 ******************************************************************************/

#ifndef ANDROID_NI_RSRC_API_H
#define ANDROID_NI_RSRC_API_H

#define LOG_TAG "SharedBufferClient"

#include <android/hardware/nidec/1.0/INidec.h>

#include <hidl/Status.h>

#include <hidl/LegacySupport.h>

#include <utils/misc.h>

#include <hidl/HidlSupport.h>

#include <stdio.h>

#include <cutils/properties.h>
#include <cutils/ashmem.h>
#include <sys/mman.h>

using android::sp;
using android::hardware::hidl_handle;
using android::hardware::hidl_string;
using android::hardware::Return;
using android::hardware::nidec::V1_0::INidec;

using namespace std;

extern android::sp<INidec> service;

/*!*****************************************************************************
 *  \brief   Init android net.int.SharedBuffer service for binder using.
 *
 *	 \param 	  none
 *
 *	 \return      service (= 0) if get service , < 0 otherwise
 *
 ******************************************************************************/
int ni_rsrc_android_init();

#endif   // ANDROID_NI_RSRC_API_H
