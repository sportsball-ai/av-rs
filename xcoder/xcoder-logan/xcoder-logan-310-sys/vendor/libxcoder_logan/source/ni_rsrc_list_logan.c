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
 *
 *   \file          ni_rsrc_list_logan.c
 *
 *   @date          April 1, 2018
 *
 *   \brief         NETINT T4XX resource listing utility program
 *
 *   @author
 *
 ******************************************************************************/

#if __linux__ || __APPLE__
#include <unistd.h>
#endif

#include <stdio.h>
#include <stdlib.h>

#include "ni_rsrc_api_logan.h"
#include "ni_util_logan.h"


/*!******************************************************************************
 *  \brief
 *
 *  \param
 *
 *  \return
 *******************************************************************************/
int32_t main(void)
{
  ni_log_set_level(NI_LOG_INFO);
  ni_logan_rsrc_print_all_devices_capability();
  return 0;
}
