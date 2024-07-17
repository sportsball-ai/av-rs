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
 *  \file   ISharedBuffer.cpp
 *
 *  \brief  Public definitions related to resource management of NETINT video
 *          processing devices on Android
 *******************************************************************************/

#define LOG_TAG "ISharedBuffer"

#include <cutils/properties.h>
#include "ISharedBuffer.h"

using namespace android;
using namespace std;

enum
{
    GET_BUFFER = IBinder::FIRST_CALL_TRANSACTION,
    GET_FD,
    SET_FD,
};

class BpSharedBuffer : public BpInterface<ISharedBuffer> {
  public:
    explicit BpSharedBuffer(const sp<IBinder> &impl)
        : BpInterface<ISharedBuffer>(impl)
    {
    }

    /*!*****************************************************************************
 *  \brief   Get file descripter by using android net.int.SharedBuffer service.
 *
 *   \param[in] name    The name of the share mem
 *
 *	 \return      Get fd (> 0) , < 0 otherwise
 *
 ******************************************************************************/
    int getFd(String8 &param)
    {
        Parcel data;
        data.writeInterfaceToken(ISharedBuffer::getInterfaceDescriptor());

        Parcel reply;
        data.writeString8(param);

        remote()->transact(GET_FD, data, &reply);

        if (reply.readInt32() == 1)
        {
            int fd = reply.readFileDescriptor();
            int fd2 = dup(fd);
            return fd2;
        } else
        {
            return -1;
        }
    }

    /*!*****************************************************************************
 *  \brief   Set file descripter with the name of the share mem by using android net.int.SharedBuffer service.
 *
 *   \param[in] name    The name of the share mem
 *
 *	 \return      Get fd (> 0) , < 0 otherwise
 *
 ******************************************************************************/
    int setFd(String8 &param, int32_t fd)
    {
        Parcel data;
        data.writeInterfaceToken(ISharedBuffer::getInterfaceDescriptor());

        Parcel reply;
        data.writeString8(param);
        data.writeFileDescriptor(fd);

        remote()->transact(SET_FD, data, &reply);
        status_t res = reply.readInt32();
        return res;
    }
};

// NOLINTNEXTLINE
IMPLEMENT_META_INTERFACE(SharedBuffer, "net.int.ISharedBuffer");

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
status_t BnSharedBuffer::onTransact(uint32_t code, const Parcel &data,
                                    Parcel *reply, uint32_t flags)
{
    switch (code)
    {
        case GET_FD:
        {
            CHECK_INTERFACE(ISharedBuffer, data, reply);
            String8 parma = data.readString8();
            int fd = getFd(parma);
            if (fd < 0)
            {
                reply->writeInt32(0);
                return NO_ERROR;
            }
            reply->writeInt32(1);
            reply->writeFileDescriptor(fd);
            return NO_ERROR;
        }
        case SET_FD:
        {
            CHECK_INTERFACE(ISharedBuffer, data, reply);
            String8 parma = data.readString8();
            int32_t fd = data.readFileDescriptor();
            int fd2 = dup(fd);
            setFd(parma, fd2);
            reply->writeInt32(0);
            return NO_ERROR;
        }
        default:
        {
            return BBinder::onTransact(code, data, reply, flags);
        }
    }
}
