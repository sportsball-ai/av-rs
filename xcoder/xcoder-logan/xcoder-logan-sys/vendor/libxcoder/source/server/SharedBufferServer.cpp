/*!****************************************************************************
*
* Copyright (C)  2018 NETINT Technologies. 
*
* Permission to use, copy, modify, and/or distribute this software for any 
* purpose with or without fee is hereby granted.
*
*******************************************************************************/

/*!*****************************************************************************
*  \file   SharedBufferServer.cpp
*
*  \brief  Exported routines related to resource management of NI T-408 devices
*
*******************************************************************************/

#define LOG_TAG "ISharedBufferServer"
#define TAG "[Server] "

#include <stdlib.h>
#include <fcntl.h>
#include <binder/IServiceManager.h>
#include <binder/IPCThreadState.h>
#include <cutils/properties.h>
#include <cutils/ashmem.h>
#include <map>

#include "../common/ISharedBuffer.h"
using namespace std;

class SharedBufferService : public BnSharedBuffer
{
public:
  SharedBufferService()
  {

  }

  virtual ~SharedBufferService()
  {
    m_fd_0 = 0;
    m_fd_1 = 0;
  }

public:

/*!*****************************************************************************
 *  \brief    android net.int.SharedBuffer service to SM.
 *
 *   \param   none
 *
 *	 \return  none
 *
 ******************************************************************************/
  static void instantiate()
  {
    defaultServiceManager ()->addService(String16(SHARED_BUFFER_SERVICE), new SharedBufferService());
  }

/*!*****************************************************************************
 *  \brief   Get file descripter by using android net.int.SharedBuffer service.
 *
 *   \param[in] name    The name of the share mem
 *
 *	 \return      Get fd (> 0) , < 0 otherwise
 *
 ******************************************************************************/
  virtual int getFd(String8& name)
  {
    map<String8, int>::iterator iter;
    iter = shmFd.find(name);
    if (iter != shmFd.end())
    {
      return iter->second;
    }
    return -1;
  }

/*!*****************************************************************************
 *  \brief   Set file descripter with the name of the share mem by using android net.int.SharedBuffer service.
 *
 *   \param[in] name    The name of the share mem
 *
 *	 \return      0
 *
 ******************************************************************************/
  virtual int setFd(String8& name, int fd)
  {
    shmFd[name] = fd;
    return 0;
  }

private:
  map<String8, int> shmFd;
  int m_fd_0;
  int m_fd_1;
};

/*!*****************************************************************************
 *  \brief   start net.int.SharedBuffer service and add it to SM.
 *
 *   \param 	  none
 *
 *	 \return      0
 *
 ******************************************************************************/
int main(int argc, char** argv)
{
  SharedBufferService::instantiate();

  ProcessState::self()->startThreadPool();
  IPCThreadState::self()->joinThreadPool();

  return 0;
}
