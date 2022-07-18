/*!****************************************************************************
*
* Copyright (C)  2018 NETINT Technologies. 
*
* Permission to use, copy, modify, and/or distribute this software for any 
* purpose with or without fee is hereby granted.
*
*******************************************************************************/

/*!*****************************************************************************
*  \file   ISharedBuffer.h
*
*  \brief  Exported definitions related to resource management of NI T-408 
*          devices
*
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

class ISharedBuffer: public IInterface
{
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
  virtual int getFd(String8& name) = 0;
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

class BnSharedBuffer: public BnInterface<ISharedBuffer>
{
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
  virtual status_t onTransact(uint32_t code, const Parcel& data, Parcel* reply, uint32_t flags = 0);
};

#endif // ISHAREDBUFFER_H_
