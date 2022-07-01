/*!****************************************************************************
*
* Copyright (C)  2018 NETINT Technologies. 
*
* Permission to use, copy, modify, and/or distribute this software for any 
* purpose with or without fee is hereby granted.
*
*******************************************************************************/

/*!*****************************************************************************
*  \file   ISharedBuffer.cpp
*
*  \brief  Exported routines related to resource management of NI T-408 devices
*
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

class BpSharedBuffer: public BpInterface<ISharedBuffer>
{
public:
  BpSharedBuffer(const sp<IBinder>& impl)
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

    if(reply.readInt32()==1)
    {
      int fd = reply.readFileDescriptor();
      int fd2 = dup(fd);
      return fd2;
    }
    else
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
status_t BnSharedBuffer::onTransact(uint32_t code, const Parcel & data, Parcel * reply, uint32_t flags)
{
  switch(code)
  {
    case GET_FD:
    {
      CHECK_INTERFACE(ISharedBuffer, data, reply);
      String8 parma = data.readString8();
      int fd = getFd(parma);
      if(fd<0)
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
      int ret = setFd(parma, fd2);
      reply->writeInt32(0);
      return NO_ERROR;
    }
    default:
    {
      return BBinder::onTransact( code, data, reply, flags);
    }
  }
}
