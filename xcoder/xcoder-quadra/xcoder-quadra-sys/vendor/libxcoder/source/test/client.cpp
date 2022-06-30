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
*  \file   client.cpp
*
*  \brief  client test for NI Quadra android service 
*
*******************************************************************************/

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
using android::hardware::nidec::V1_0::INidec;

#define UNUSED(x) (void)(x)

#define CODERS_SHM_NAME "NI_SHM_CODERS"
#define STR_BUFFER_LEN 32

using namespace std;

int main()
{
    int ret;

    UNUSED(ret);

    string param = CODERS_SHM_NAME;
    int32_t shm_fd = 0;

    android::sp<INidec> service = INidec::getService();
    if (service == nullptr)
    {
        printf("Failed to get service\n");
        return -1;
    }

    service->GetAppFlag(param, [&](int32_t ret, hidl_handle handle) {
        printf("GetAppFlag: ret %d\n", ret);
        if (ret > 0)
        {
            shm_fd = dup(handle->data[0]);
            printf("vendor:GetAppFlag shm_fd:%d\n", shm_fd);
        }
    });

    if (shm_fd <= 0)
    {
        shm_fd = ashmem_create_region(CODERS_SHM_NAME, sizeof(int32_t));
        if (shm_fd >= 0)
        {
            native_handle_t *handle = native_handle_create(1, 0);
            handle->data[0] = shm_fd;
            service->SetAppFlag(param, handle);
        }
        printf("Create shm fd %d\n", shm_fd);
        int32_t *shm_data = (int32_t *)mmap(
            0, sizeof(int32_t), PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd, 0);
        if (MAP_FAILED == shm_data)
        {
            printf("Error mmap shm_data ..\n");
        }
        *shm_data = 100;
        printf("set shm data %d\n", *shm_data);

        //close(shm_fd);

        shm_data = (int32_t *)mmap(0, sizeof(int32_t), PROT_READ | PROT_WRITE,
                                   MAP_SHARED, shm_fd, 0);
        if (MAP_FAILED == shm_data)
        {
            printf("Error mmap shm_data ..\n");
        }
        printf("get shm data %d\n", *shm_data);

    } else
    {
        printf("Get shm fd %d\n", shm_fd);
        int32_t *shm_data = (int32_t *)mmap(
            0, sizeof(int32_t), PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd, 0);
        if (MAP_FAILED == shm_data)
        {
            printf("mmap failed errno = %d\n", errno);
        }
        printf("get shm data %d\n", *shm_data);
    }

    close(shm_fd);

    return 0;
}
