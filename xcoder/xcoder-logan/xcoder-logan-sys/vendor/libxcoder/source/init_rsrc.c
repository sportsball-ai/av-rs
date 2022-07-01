/*!****************************************************************************
*
* Copyright (C)  2018 NETINT Technologies. 
*
* Permission to use, copy, modify, and/or distribute this software for any 
* purpose with or without fee is hereby granted.
*
*******************************************************************************/

/*!*****************************************************************************
 *
 *   \file          init_rsrc.c
 *
 *   @date          April 1, 2018
 *
 *   \brief         NETINT T408 resource init utility program
 *
 *   @author        
 *
 ******************************************************************************/

#include <stdio.h>
#include <string.h>
#include <fcntl.h> 

#ifdef __linux__
#include <unistd.h>
#include <sys/types.h>
#elif _WIN32
#include <tchar.h>
#include "XGetopt.h"
#endif 

#include "ni_defs.h"
#include "ni_rsrc_api.h"
#include "ni_rsrc_priv.h"
#include "ni_util.h"


/*!******************************************************************************
 *  \brief  
 *
 *  \param  
 *
 *  \return
 *******************************************************************************/
int main(int argc, char *argv[])
{
  ni_retcode_t retval;
  int should_match_rev = 1;
  int opt;
  int timeout_seconds = 0;
  
  // arg handling
  while ((opt = getopt(argc, argv, "hrt:l:")) != -1)
  {
    switch (opt)
    {
    case 'h':
      // help message
      printf("-------- init_rsrc v%s --------\n"
             "Initialize NetInt transcoder resource pool\n"
             "\n-r\tInit transcoder card resource regardless of firmware release version to libxcoder release version compatibility. Default is to only init cards with compatible firmware version.\n"
             "\n-t\tSet timeout time in seconds for device polling, will exit with failure if reached. Default 0s which means no timeout.\n"
             "\n-l\tSet loglevel of libxcoder API.\n"
             "\t[none, fatal, error, info, debug, trace]\n"
             "\tDefault: info\n"
             "\n-h\tDisplay this help and exit.\n", NI_XCODER_REVISION);
      return 0;
    case 'r':
      should_match_rev = 0;
      break;
    case 't':
      timeout_seconds = atoi(optarg);
      printf("Timeout will be set %d\n", timeout_seconds);
      break;
    case 'l':
      if (!strcmp(optarg, "none")) {
        ni_log_set_level(NI_LOG_NONE);
      } else if (!strcmp(optarg, "fatal")) {
        ni_log_set_level(NI_LOG_FATAL);
      } else if (!strcmp(optarg, "error")) {
        ni_log_set_level(NI_LOG_ERROR);
      } else if (!strcmp(optarg, "info")) {
        ni_log_set_level(NI_LOG_INFO);
      } else if (!strcmp(optarg, "debug")) {
        ni_log_set_level(NI_LOG_DEBUG);
      } else if (!strcmp(optarg, "trace")) {
        ni_log_set_level(NI_LOG_TRACE);
      } else {
        fprintf(stderr, "unknown log level selected: %s", optarg);
        return 1;
      }
      break;
    default:
      fprintf(stderr, "Wrong option\n");
      return 1;
    }
  }

#ifdef __linux__
  retval = ni_rsrc_init(should_match_rev, timeout_seconds);
  return retval;
#elif _WIN32
  retval = ni_rsrc_init(should_match_rev, timeout_seconds);
  if (NI_RETCODE_SUCCESS == retval)
  {
    printf("NETINT Resources Intitialized Successfully\n");
    // keep the process only on success
    // just return on init already or failure
    while (1)
      Sleep(1000);
  }
  return retval;
#endif
}
