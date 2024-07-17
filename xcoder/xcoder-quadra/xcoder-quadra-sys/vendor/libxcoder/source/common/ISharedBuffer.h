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
 *  \file   ISharedBuffer.h
 *
 *  \brief  Public definitions related to resource management of NETINT video
 *          processing devices on Android
 *******************************************************************************/

#ifndef ISHAREDBUFFER_H_
#define ISHAREDBUFFER_H_

#include <utils/RefBase.h>
#include <binder/IInterface.h>
#include <binder/Parcel.h>
#include <utils/String8.h>

#define SHARED_BUFFER_SERVICE "net.int.SharedBuffer"
#define STR_BUFFER_LEN 32

using namespace android;
using namespace std;

class ISharedBuffer : public IInterface {
  public:
    DECLARE_META_INTERFACE(SharedBuffer);
    /*!*****************************************************************************
 *  \brief   Get file descripter by using android net.int.SharedBuffer service.
 *
 *   \param[in] name    The name of the share mem
 *
 *	 \return      Get fd (> 0) , < 0 otherwise
 *
 ******************************************************************************/
    virtual int getFd(String8 &name) = 0;
    /*!*****************************************************************************
 *  \brief   Set file descripter with the name of the share mem by using android net.int.SharedBuffer service.
 *
 *   \param[in] name    The name of the share mem
 *
 *	 \return      Get fd (> 0) , < 0 otherwise
 *
 ******************************************************************************/
    virtual int setFd(String8 &param, int32_t fd) = 0;
};

class BnSharedBuffer : public BnInterface<ISharedBuffer> {
  public:
    /*!*****************************************************************************
 *  \brief   transmit the data between binder clint and server.
 *
 *   \param[in] code    The funtion identify
 *   \param[in] data    The parcel take the data of the shm
 *   \param[in] reply    The parcel take the return data of the shm
 *   \param[in] flags    always 0
 *
 *	 \return      success status_t (> 0) , < 0 otherwise
 *
 ******************************************************************************/
    virtual status_t onTransact(uint32_t code, const Parcel &data,
                                Parcel *reply, uint32_t flags = 0);
};

#endif   // ISHAREDBUFFER_H_
