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
 *  \file   ni_libxcoder_dynamic_loading_test.cpp
 *
 *  \brief  Application to test that ni_libxcoder_dynamic_loading.h successfully
 *          loads all its exported functions
 ******************************************************************************/

#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/types.h>

#include <fcntl.h>
#include <errno.h>
#define _NETINT_LIBXCODER_DYNAMIC_LOADING_TEST_
#include "ni_libxcoder_dynamic_loading.h"

int main(int argc, char **argv)
{
    void *handle;
    char *error;
    NETINTLibxcoderAPI ni_libxcoder_api_dl;
    NETINT_LIBXCODER_API_FUNCTION_LIST functionList;
    int failed_func_ptr_check = 0;

    handle = dlopen ("libxcoder.so", RTLD_NOW);
    if (!handle) {
        fprintf(stderr, "%s\n", dlerror());
        exit(1);
    }

    // Check load error on individual function
    // functionList.niSetVui = reinterpret_cast<decltype(ni_set_vui)*>(dlsym(handle,"ni_set_vui"));
    // if ((error = dlerror()) != NULL)  {
    //     fprintf(stderr, "%s\n", error);
    //     exit(1);
    // }
    
    ni_libxcoder_api_dl.NiLibxcoderAPICreateInstance(handle, &functionList);
    if ((error = dlerror()) != NULL)  {
        fprintf(stderr, "%s\n", error);
        exit(1);
    }
    printf("Dynamically loaded functionList\n");

    for (int i = 0; i < (int)((sizeof(NETINT_LIBXCODER_API_FUNCTION_LIST) / \
                         sizeof(void *))); i++)
    {
        if (((void *) *(((void **) &functionList) + i)) == NULL)
        {
            fprintf(stderr, "Failed to load function pointer at function %d in "
                            "NETINT_LIBXCODER_API_FUNCTION_LIST\n", i);
            failed_func_ptr_check = 1;
        }
    }

    if (!failed_func_ptr_check)
    {
        printf("All functions in functionList were loaded.\nPass\n");
    }

    dlclose(handle);
    exit(failed_func_ptr_check);
}
