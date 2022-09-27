/*!****************************************************************************
*
* Copyright (C)  2018 NETINT Technologies. 
*
* Permission to use, copy, modify, and/or distribute this software for any 
* purpose with or without fee is hereby granted.
*
*******************************************************************************/

/*!*****************************************************************************
*  \file   ni_nvme.c
*
*  \brief  Private routines related to working with NI T-408 over NVME interface
*
*******************************************************************************/

#include <stdint.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>

#ifdef _WIN32
  #ifndef MSVC_BUILD
    #include "../build/xcoder_auto_headers.h"
  #endif
  #include <windows.h>
  #ifndef WIN_NVME_CUSTOM
    #include <ntddstor.h>
    #include <nvme.h>
    #include <winioctl.h>
    #include <Ntddscsi.h>
  #else
    // These 3 files taken from OFA open source NVMe Windows driver
    // Refer to website: http://www.openfabrics.org/svnrepo/nvmewin/trunk/source/
    #include "../MSVS2019/nvme-1.5/source/nvmeIoctl.h"
    #include "../MSVS2019/nvme-1.5/source/nvmeReg.h"
    #include "../MSVS2019/nvme-1.5/source/nvme.h"
  #endif
#elif __linux__
  #include <linux/types.h>
  #include <sys/ioctl.h>
  #include <sys/stat.h>
  #include <unistd.h>
#endif

#include "ni_nvme.h"
#include "ni_util.h"

#define ROUND_TO_ULONG(x) ni_round_up(x,sizeof(uint32_t))

/*!******************************************************************************
 *  \brief  Check f/w error return code, and if it's a fatal one, terminate 
 *          application's decoding/encoding processing by sending
 *          self a SIGTERM signal. Application shall handle this gracefully.
 *
 *  \param  
 *
 *  \return 1 (or non-zero) if need to terminate, 0 otherwise
 ******************************************************************************/
ni_retcode_t ni_nvme_check_error_code(int rc, ni_nvme_admin_opcode_t opcode,
                            uint32_t xcoder_type, uint32_t hw_id,
                            int32_t *p_instance_id)
{
  switch (rc)
  {
    case NI_RETCODE_NVME_SC_RESOURCE_UNAVAILABLE:
    case NI_RETCODE_NVME_SC_RESOURCE_IS_EMPTY:
    case NI_RETCODE_NVME_SC_RESOURCE_NOT_FOUND:
    case NI_RETCODE_NVME_SC_REQUEST_NOT_COMPLETED:
    case NI_RETCODE_NVME_SC_REQUEST_IN_PROGRESS:
    case NI_RETCODE_NVME_SC_INVALID_PARAMETER:
    case NI_RETCODE_NVME_SC_VPU_RECOVERY:
    case NI_RETCODE_NVME_SC_VPU_RSRC_INSUFFICIENT:
    case NI_RETCODE_NVME_SC_VPU_GENERAL_ERROR:
    {
      ni_log(NI_LOG_ERROR, "Error rc = 0x%x, op = %02x, %s %d.%d terminating?\n",
          rc, opcode,
          xcoder_type == NI_DEVICE_TYPE_DECODER ? "decoder" : "encoder",
          hw_id, *p_instance_id);

      if (NI_RETCODE_NVME_SC_RESOURCE_IS_EMPTY == rc ||
          NI_RETCODE_NVME_SC_RESOURCE_NOT_FOUND == rc ||
          NI_RETCODE_NVME_SC_VPU_RSRC_INSUFFICIENT == rc ||
          NI_RETCODE_NVME_SC_VPU_GENERAL_ERROR == rc)
      {
        return NI_RETCODE_FAILURE;
      }
      break;
    }
    default:
    {
      break; // nothing
    }
  }
  return NI_RETCODE_SUCCESS;
}

#ifdef __linux__

/*!******************************************************************************
 *  \brief  Submit a nvme admin passthrough command to the driver
 *
 *  \param  
 *
 *  \return
 *******************************************************************************/
int32_t ni_nvme_send_admin_pass_through_command(ni_device_handle_t handle, ni_nvme_passthrough_cmd_t *p_cmd)
{
  ni_log(NI_LOG_TRACE, "ni_nvme_send_admin_pass_through_command: handle=%d\n", handle);
  ni_log(NI_LOG_TRACE, "opcode:     %02x\n", p_cmd->opcode);
  ni_log(NI_LOG_TRACE, "flags:      %02x\n", p_cmd->flags);
  ni_log(NI_LOG_TRACE, "rsvd1:      %04x\n", p_cmd->rsvd1);
  ni_log(NI_LOG_TRACE, "nsid:      %08x\n", p_cmd->nsid);
  ni_log(NI_LOG_TRACE, "cdw2:      %08x\n", p_cmd->cdw2);
  ni_log(NI_LOG_TRACE, "cdw3:      %08x\n", p_cmd->cdw3);
  //ni_log(NI_LOG_TRACE, "metadata:   %"PRIx64"\n", p_cmd->metadata);
  //ni_log(NI_LOG_TRACE, "addr:     %"PRIx64"\n", p_cmd->addr);
  ni_log(NI_LOG_TRACE, "metadata_len: %08x\n", p_cmd->metadata_len);
  ni_log(NI_LOG_TRACE, "data_len:    %08x\n", p_cmd->data_len);
  ni_log(NI_LOG_TRACE, "cdw10:      %08x\n", p_cmd->cdw10);
  ni_log(NI_LOG_TRACE, "cdw11:      %08x\n", p_cmd->cdw11);
  ni_log(NI_LOG_TRACE, "cdw12:      %08x\n", p_cmd->cdw12);
  ni_log(NI_LOG_TRACE, "cdw13:      %08x\n", p_cmd->cdw13);
  ni_log(NI_LOG_TRACE, "cdw14:      %08x\n", p_cmd->cdw14);
  ni_log(NI_LOG_TRACE, "cdw15:      %08x\n", p_cmd->cdw15);
  ni_log(NI_LOG_TRACE, "timeout_ms:   %08x\n", p_cmd->timeout_ms);
  ni_log(NI_LOG_TRACE, "result:     %08x\n", p_cmd->result);

#ifndef XCODER_SIM_ENABLED
  return ioctl(handle, NVME_IOCTL_ADMIN_CMD, p_cmd);
#else
  return 0;
#endif
}

/*!******************************************************************************
 *  \brief  Submit a nvme io passthrough command to the driver
 *
 *  \param  
 *
 *  \return
 *******************************************************************************/
int32_t ni_nvme_send_io_pass_through_command(ni_device_handle_t handle, ni_nvme_passthrough_cmd_t * p_cmd)
{
  ni_log(NI_LOG_TRACE, "ni_nvme_send_io_pass_through_command: handle=%d\n", handle);
  ni_log(NI_LOG_TRACE, "opcode:       %02x\n", p_cmd->opcode);
  ni_log(NI_LOG_TRACE, "addr:         %p\n", (void *)p_cmd->addr);
  ni_log(NI_LOG_TRACE, "flags:        %02x\n", p_cmd->flags);
  ni_log(NI_LOG_TRACE, "rsvd1:        %04x\n", p_cmd->rsvd1);
  ni_log(NI_LOG_TRACE, "nsid:         %08x\n", p_cmd->nsid);
  ni_log(NI_LOG_TRACE, "cdw2:         %08x\n", p_cmd->cdw2);
  ni_log(NI_LOG_TRACE, "cdw3:         %08x\n", p_cmd->cdw3);
  //ni_log(NI_LOG_TRACE, "metadata:     %"PRIx64"\n", p_cmd->metadata);
  //ni_log(NI_LOG_TRACE, "addr:         %"PRIx64"\n", p_cmd->addr);
  ni_log(NI_LOG_TRACE, "metadata_len: %08x\n", p_cmd->metadata_len);
  ni_log(NI_LOG_TRACE, "data_len:     %08x\n", p_cmd->data_len);
  ni_log(NI_LOG_TRACE, "cdw10:        %08x\n", p_cmd->cdw10);
  ni_log(NI_LOG_TRACE, "cdw11:        %08x\n", p_cmd->cdw11);
  ni_log(NI_LOG_TRACE, "cdw12:        %08x\n", p_cmd->cdw12);
  ni_log(NI_LOG_TRACE, "cdw13:        %08x\n", p_cmd->cdw13);
  ni_log(NI_LOG_TRACE, "cdw14:        %08x\n", p_cmd->cdw14);
  ni_log(NI_LOG_TRACE, "cdw15:        %08x\n", p_cmd->cdw15);
  ni_log(NI_LOG_TRACE, "timeout_ms:   %08x\n", p_cmd->timeout_ms);
  ni_log(NI_LOG_TRACE, "result:       %08x\n", p_cmd->result);

#ifndef XCODER_SIM_ENABLED
  return ioctl(handle, NVME_IOCTL_IO_CMD, p_cmd);
#else
  return 0;
#endif
}

#endif //__linux__ defined

#ifdef _WIN32

#ifndef WIN_NVME_CUSTOM //NOT Using the custom STORPORT Windows NVMe Driver and using standard Windows inbox driver
#define ROUND_TO_DWORD_PTR(x) ni_round_up(x,sizeof(PDWORD))

static ni_retcode_t ni_nvme_get_identity(HANDLE handle, HANDLE event_handle, PVOID p_identity)
{
#ifdef XCODER_IO_RW_ENABLED
  uint32_t                        lba = IDENTIFY_DEVICE_R;
  DWORD                           data_len = NI_NVME_IDENTITY_CMD_DATA_SZ;
  ni_retcode_t                    rc = NI_RETCODE_SUCCESS;

  rc = ni_nvme_send_read_cmd(handle, event_handle, p_identity, data_len, lba);
#else
  PUCHAR                          p_buffer = NULL;
  DWORD                           buffer_len = FIELD_OFFSET(STORAGE_PROPERTY_QUERY, AdditionalParameters) +
                                                  sizeof(STORAGE_PROTOCOL_SPECIFIC_DATA) + NVME_MAX_LOG_SIZE;
  DWORD                           retval = 0;
  ni_retcode_t                    rc = NI_RETCODE_SUCCESS;
  PSTORAGE_PROPERTY_QUERY         p_query = NULL;
  PSTORAGE_PROTOCOL_SPECIFIC_DATA p_protocol_data = NULL;
  BOOL                            b_status = FALSE;

  p_buffer = malloc(buffer_len);
  if (NULL == p_buffer)
  {
    return NI_RETCODE_ERROR_MEM_ALOC;
  }

  ZeroMemory(p_buffer, buffer_len);
  p_query = (PSTORAGE_PROPERTY_QUERY)p_buffer;
  p_protocol_data = (PSTORAGE_PROTOCOL_SPECIFIC_DATA)p_query->AdditionalParameters;

  p_query->PropertyId = StorageAdapterProtocolSpecificProperty;
  p_query->QueryType = PropertyStandardQuery;

  p_protocol_data->ProtocolType = ProtocolTypeNvme;
  p_protocol_data->DataType = NVMeDataTypeIdentify;
  p_protocol_data->ProtocolDataRequestValue = NVME_IDENTIFY_CNS_CONTROLLER;
  p_protocol_data->ProtocolDataRequestSubValue = 0;
  p_protocol_data->ProtocolDataOffset = sizeof(STORAGE_PROTOCOL_SPECIFIC_DATA);
  p_protocol_data->ProtocolDataLength = NVME_MAX_LOG_SIZE;

  b_status = DeviceIoControl(
      handle,
      IOCTL_STORAGE_QUERY_PROPERTY,
      p_buffer,
      buffer_len,
      p_buffer,
      buffer_len,
      &retval,
      NULL
  );

  if (FALSE == b_status)
  {
    retval = NI_XCODER_NUM_OF_SYSTEM_LAST_ERROR;
    rc = NI_RETCODE_ERROR_RESOURCE_UNAVAILABLE;
  }
  else
  {
    PNVME_IDENTIFY_CONTROLLER_DATA p_std_identity = (PNVME_IDENTIFY_CONTROLLER_DATA)(((PUCHAR)p_buffer) + buffer_len - NVME_MAX_LOG_SIZE);
    memcpy(p_identity, p_std_identity, sizeof(NVME_IDENTIFY_CONTROLLER_DATA));
  }
  if (p_buffer)
  {
    free(p_buffer);
  }
#endif

  return rc;
}

int ni_nvme_enumerate_devices
(
    char   ni_devices[][MAX_DEVICE_NAME_LEN],
    int    max_handles
)
{
  TCHAR                           port_name_buffer[MAX_DEVICE_NAME_LEN] = { 0 };
  LPTSTR                          p_scsi_path = "\\\\.\\PHYSICALDRIVE";
  LPCTSTR                         p_format = "%s%d";
  PUCHAR                          p_buffer = NULL;
  ni_retcode_t                    rc = NI_RETCODE_SUCCESS;
  CHAR                            firmware_revision[8] = { 0 };
  int                             device_count = 0;
  int                             scsi_port = 0;
  DWORD                           retval = 0;
#ifdef XCODER_IO_RW_ENABLED
  DWORD                           data_len = NI_NVME_IDENTITY_CMD_DATA_SZ;
  CHAR                            serial_num[20] = { 0 };
  CHAR                            model_name[40] = { 0 };  
#else
  PNVME_IDENTIFY_CONTROLLER_DATA  p_std_identity = NULL;
#endif
  ni_nvme_identity_t*             p_ni_identity = NULL;

  ni_log(NI_LOG_INFO, "Searching for NETINT NVMe devices ...\n\n");
#ifdef XCODER_IO_RW_ENABLED
  if (posix_memalign((void **)(&p_buffer), sysconf(_SC_PAGESIZE), data_len))
  {
    ni_log(NI_LOG_ERROR, "ERROR %d: ni_nvme_enumerate_devices() alloc buffer failed\n", NI_XCODER_NUM_OF_SYSTEM_LAST_ERROR);
    return NI_RETCODE_ERROR_MEM_ALOC;
  }

  memset(p_buffer, 0, data_len);
  ni_event_handle_t event_handle = ni_create_event();
  if (NI_INVALID_EVENT_HANDLE == event_handle)
  {
    ni_log(NI_LOG_ERROR, "ERROR ni_nvme_enumerate_devices(): create event failed\n");
    return NI_RETCODE_ERROR_INVALID_HANDLE;
  }
#else
  p_std_identity = (PNVME_IDENTIFY_CONTROLLER_DATA) malloc(sizeof(NVME_IDENTIFY_CONTROLLER_DATA));
  if (NULL == p_std_identity)
  {
    return NI_RETCODE_ERROR_MEM_ALOC;
  }

  p_ni_identity = (ni_nvme_identity_t*)p_std_identity;
#endif

  for (scsi_port = 0; scsi_port < max_handles; scsi_port++)
  {
    wsprintf(port_name_buffer, p_format, p_scsi_path, scsi_port);
    HANDLE handle = CreateFile(
                                port_name_buffer,
                                GENERIC_READ | GENERIC_WRITE,
                                FILE_SHARE_READ | FILE_SHARE_WRITE,
                                NULL,
                                OPEN_EXISTING,
                                FILE_ATTRIBUTE_NORMAL,
                                NULL
                              );

    if (INVALID_HANDLE_VALUE == handle)
    {
      retval = NI_XCODER_NUM_OF_SYSTEM_LAST_ERROR;
    }
    else
    {
#ifdef XCODER_IO_RW_ENABLED
      ZeroMemory(p_buffer, data_len);
      rc = ni_nvme_get_identity(handle, event_handle, p_buffer);
      if (NI_RETCODE_SUCCESS == rc)
      {
        ni_nvme_identity_t* p_ni_id_data = (ni_nvme_identity_t*)p_buffer;
        ni_log(NI_LOG_INFO, "Identity information retrieved from the device at port %s\n", port_name_buffer);
        ni_log(NI_LOG_INFO, "    VID:           0x%x \n    SSVID:         0x%x \n",
              p_ni_id_data->ui16Vid, p_ni_id_data->ui16Ssvid);
  
        if ((NETINT_PCI_VENDOR_ID == p_ni_id_data->ui16Vid) &&
            (NETINT_PCI_VENDOR_ID == p_ni_id_data->ui16Ssvid) &&
            (p_ni_id_data->device_is_xcoder))
        {
          /* make the fw revision/model name/serial num to string*/
          memset(firmware_revision, 0, sizeof(firmware_revision));
          memcpy(firmware_revision, p_ni_id_data->ai8Fr, sizeof(firmware_revision));
          firmware_revision[sizeof(firmware_revision) - 1] = 0;

          memset(model_name, 0, sizeof(model_name));
          memcpy(model_name, p_ni_id_data->ai8Mn, sizeof(model_name));
          model_name[sizeof(model_name) - 1] = 0;

          memset(serial_num, 0, sizeof(serial_num));
          memcpy(serial_num, p_ni_id_data->ai8Sn, sizeof(serial_num));
          serial_num[sizeof(serial_num) - 1] = 0;
          ni_log(NI_LOG_INFO, "    Device Model:  %s \n", model_name);
          ni_log(NI_LOG_INFO, "    Firmware Rev:  %s \n", firmware_revision);
          ni_log(NI_LOG_INFO, "    Serial Number: %s \n", serial_num);
  
          ni_log(NI_LOG_INFO, "NETINT %s NVMe video transcoder identified at port %s\n\n", model_name, port_name_buffer);
          strncpy(ni_devices[device_count++], port_name_buffer, MAX_DEVICE_NAME_LEN);
        }
        else
        {
          ni_log(NI_LOG_INFO, "Device at port %s is not a NETINT NVMe device\n", port_name_buffer);
        }
  
        CloseHandle(handle);
      }
      else
      {
        retval = NI_XCODER_NUM_OF_SYSTEM_LAST_ERROR;
        ni_log(NI_LOG_INFO, "Device at port %s is not a NETINT NVMe device,ret=%d\n\n", port_name_buffer ,retval);
        CloseHandle(handle);
      }
#else
      ZeroMemory(p_std_identity, sizeof(NVME_IDENTIFY_CONTROLLER_DATA));
      rc = ni_nvme_get_identity(handle, NI_INVALID_EVENT_HANDLE, p_std_identity);
      if (NI_RETCODE_SUCCESS == rc)
      {
        ni_log(NI_LOG_INFO, "Identity information retrieved from the device at port %s\n", port_name_buffer);
        ni_log(NI_LOG_INFO, "    VID:           0x%x \n    SSVID:         0x%x \n",
        p_std_identity->VID, p_std_identity->SSVID);

        ni_log(NI_LOG_INFO, "    Device Model:  %s \n", p_std_identity->MN);

        /* make the fw revision string null terminated */
        memset(firmware_revision, 0, sizeof(firmware_revision));
        memcpy(firmware_revision, p_std_identity->FR, sizeof(firmware_revision));
        firmware_revision[sizeof(firmware_revision) - 1] = 0;

        ni_log(NI_LOG_INFO, "    Firmware Rev:  %s \n", firmware_revision);
        ni_log(NI_LOG_INFO, "    Serial Number: %s \n", p_std_identity->SN);

        if ((NETINT_PCI_VENDOR_ID == p_std_identity->VID) &&
            (NETINT_PCI_VENDOR_ID == p_std_identity->SSVID) &&
            (p_ni_identity->device_is_xcoder))
        {
          ni_log(NI_LOG_INFO, "NETINT %s NVMe video transcoder identified at port %s\n\n", p_std_identity->MN, port_name_buffer);

          ni_devices[device_count][0] = '\0';
          strncat(ni_devices[device_count++], port_name_buffer, MAX_DEVICE_NAME_LEN);
        }
        else
        {
          ni_log(NI_LOG_INFO, "Unknown NVMe adapter identified at port %s\n", port_name_buffer);
        }

        CloseHandle(handle);
      }
#endif
    }
  } // end for loop

#ifdef XCODER_IO_RW_ENABLED
  if (p_buffer)
  {
    free(p_buffer);
  }
  ni_close_event(event_handle);
#else
  if (p_std_identity)
  {
    free(p_std_identity);
  }
#endif

  ni_log(NI_LOG_INFO, "Total Number of NETINT NVMe Transcoders indentified: %d \n\n", device_count);
  return device_count;
}

#ifndef XCODER_IO_RW_ENABLED
inline int32_t ni_nvme_send_win_io_command
(
  ni_nvme_admin_opcode_t nvme_opcode,
  ni_device_handle_t device_handle,
  ni_nvme_command_t *p_ni_nvme_cmd,
  uint32_t data_len,
  void *p_user_data,
  uint32_t* p_result,
  BOOL b_admin_command)
{
  //PNVME_PASS_THROUGH_IOCTL     p_ioctl_struct = NULL;
  //PNVMe_COMMAND                p_nvme_command = NULL;
  PCHAR             p_buffer = NULL;
  ni_nvme_command_t            *p_ni_xcoder_nvme_command = NULL;
  //PNETINT_VEND_SPEC_EX         p_vendor_spec = NULL;
  DWORD                        returned_data_len = 0;
  BOOL                         b_retval = FALSE;
  //BYTE             nvme_io_direction = NVME_NO_DATA_TX;
  DWORD                        buffer_length = 0;
  DWORD                        data_buffer_offset = 0;
  int32_t                      retval = NI_RETCODE_FAILURE;
  PSTORAGE_PROTOCOL_COMMAND    p_protocol_cmd = NULL;
  PNVME_COMMAND                p_nvme_cmd = NULL;

  if ((!data_len) && (NULL == p_user_data))
  {
    ni_log(NI_LOG_TRACE, "WARN: ni_nvme_send_win_io_command() data_len and data parameters are NULL\n");
  }
  else if (!data_len)
  {
    ni_log(NI_LOG_TRACE, "ERROR: ni_nvme_send_win_io_command() data_len is: %d, FATAL.\n", data_len);
    LRETURN;
  }
  else if (NULL == p_user_data)
  {
    ni_log(NI_LOG_TRACE, "ERROR: ni_nvme_send_win_io_command() data is: %p, FATAL.\n", p_user_data);
    LRETURN;
  }

  //Get system mem page size
  SYSTEM_INFO sys_info = { 0 };
  GetSystemInfo(&sys_info);

  buffer_length = FIELD_OFFSET(STORAGE_PROTOCOL_COMMAND, Command) +
                    STORAGE_PROTOCOL_COMMAND_LENGTH_NVME + sizeof(NVME_ERROR_INFO_LOG) + data_len;


  p_buffer = malloc(buffer_length); // _aligned_malloc(buffer_length, sys_info.dwPageSize);
  if (!p_buffer)
  {
    ni_log(NI_LOG_ERROR, "ERROR %d: ni_nvme_send_win_io_command() allocate buffer failed, FATAL.\n", NI_XCODER_NUM_OF_SYSTEM_LAST_ERROR);
    retval = NI_RETCODE_FAILURE;
    LRETURN;
  }

  ZeroMemory(p_buffer, buffer_length);

  p_protocol_cmd = (PSTORAGE_PROTOCOL_COMMAND)p_buffer;
  p_nvme_cmd     = (PNVME_COMMAND)p_protocol_cmd->Command;

  p_protocol_cmd->Version = STORAGE_PROTOCOL_STRUCTURE_VERSION;
  p_protocol_cmd->Length = sizeof(STORAGE_PROTOCOL_COMMAND);
  p_protocol_cmd->ProtocolType = ProtocolTypeNvme;
  p_protocol_cmd->Flags = STORAGE_PROTOCOL_COMMAND_FLAG_ADAPTER_REQUEST;
  p_protocol_cmd->CommandLength = STORAGE_PROTOCOL_COMMAND_LENGTH_NVME;
  p_protocol_cmd->ErrorInfoLength = sizeof(NVME_ERROR_INFO_LOG);
  p_protocol_cmd->TimeOutValue = 10;
  p_protocol_cmd->ErrorInfoOffset = FIELD_OFFSET(STORAGE_PROTOCOL_COMMAND, Command) + STORAGE_PROTOCOL_COMMAND_LENGTH_NVME;

  data_buffer_offset = p_protocol_cmd->ErrorInfoOffset + p_protocol_cmd->ErrorInfoLength;

  //p_protocol_cmd->DataFromDeviceBufferOffset = p_protocol_cmd->ErrorInfoOffset + p_protocol_cmd->ErrorInfoLength;
  //p_protocol_cmd->DataFromDeviceTransferLength = 0;

  switch (nvme_opcode)
  {
    case nvme_admin_cmd_xcoder_open:
    case nvme_admin_cmd_xcoder_close:
    case nvme_admin_cmd_xcoder_query:
    {
      if (data_len)
      {
        //p_ioctl_struct->Direction = NVME_FROM_DEV_TO_HOST;
        //p_ioctl_struct->DataBufferLen = buffer_length;
        //memcpy(&(p_ioctl_struct->DataBuffer[0]), p_user_data, data_len);
        p_protocol_cmd->DataFromDeviceBufferOffset = data_buffer_offset;
        p_protocol_cmd->DataFromDeviceTransferLength = data_len;
        memcpy(&(p_buffer[data_buffer_offset]), p_user_data, data_len);
      }
      else
      {
        //This is just the IOCTL command, no data, local allocation

        //p_ioctl_struct->Direction = NVME_NO_DATA_TX;
        //p_ioctl_struct->DataBufferLen = 0;
        p_protocol_cmd->DataFromDeviceBufferOffset = data_buffer_offset;
        p_protocol_cmd->DataFromDeviceTransferLength = 0;
      }

      break;
    }

    case nvme_admin_cmd_xcoder_read:
    case nvme_cmd_xcoder_read:
    {
      //p_ioctl_struct->Direction = NVME_FROM_DEV_TO_HOST;
      //p_ioctl_struct->DataBufferLen = 0;
      //p_vendor_spec->NvmeNDP = ROUND_TO_ULONG(data_len) / sizeof(ULONG);
      p_protocol_cmd->DataFromDeviceBufferOffset = data_buffer_offset;
      p_protocol_cmd->DataFromDeviceTransferLength = data_len;
      break;
    }

    case nvme_admin_cmd_xcoder_write:
    case nvme_cmd_xcoder_write:
    case nvme_admin_cmd_xcoder_config:
    {
      if (data_len)
      {
        //p_ioctl_struct->Direction = NVME_FROM_HOST_TO_DEV;
        //p_ioctl_struct->DataBufferLen = buffer_length;
        //memcpy(&(p_ioctl_struct->DataBuffer[0]), p_user_data, data_len);
        p_protocol_cmd->DataToDeviceBufferOffset = data_buffer_offset;
        p_protocol_cmd->DataToDeviceTransferLength = data_len;
        //memcpy(&(p_buffer[data_buffer_offset]), p_user_data, data_len);

        //p_protocol_cmd->DataFromDeviceBufferOffset = data_buffer_offset;
        //p_protocol_cmd->DataFromDeviceTransferLength = 0;
      }
      else
      {
        //This is just the IOCTL command, no data, local allocation
        //p_ioctl_struct->Direction = NVME_NO_DATA_TX;
        //p_ioctl_struct->DataBufferLen = 0;
        p_protocol_cmd->DataToDeviceBufferOffset = data_buffer_offset;
        p_protocol_cmd->DataToDeviceTransferLength = 0;
      }
      break;
    }
    default:
    {
      ni_log(NI_LOG_TRACE, "ERROR: ni_nvme_send_win_io_command() unsupported NVMe opcode: %d, isAdminOpcode: %d FATAL.\n", nvme_opcode, b_admin_command);
      LRETURN;
      break;
    }
  }

  //p_ioctl_struct->ReturnBufferLen = buffer_length;

  // Set up the SRB IO Control header
  //p_ioctl_struct->SrbIoCtrl.ControlCode = (ULONG)NVME_PASS_THROUGH_SRB_IO_CODE;
  //memcpy(p_ioctl_struct->SrbIoCtrl.Signature, NVME_SIG_STR, strlen(NVME_SIG_STR));
  //p_ioctl_struct->SrbIoCtrl.HeaderLength = (ULONG) sizeof(SRB_IO_CONTROL);
  //p_ioctl_struct->SrbIoCtrl.Timeout = 30;
  //p_ioctl_struct->SrbIoCtrl.Length = buffer_length - sizeof(SRB_IO_CONTROL);
  //p_ioctl_struct->SrbIoCtrl.ReturnCode = 0;

  // Set up the NVMe pass through IOCTL buffer
  //p_nvme_command = (PNVMe_COMMAND)p_ioctl_struct->NVMeCmd;
  //p_nvme_command->CDW0.OPC = nvme_opcode; //OPCODE
  //memcpy(&p_nvme_command->CDW10, p_ni_nvme_cmd, sizeof(ni_nvme_command_t));
  p_nvme_cmd->CDW0.OPC = nvme_opcode;
  //memcpy(&p_nvme_cmd->u.GENERAL.CDW10, p_ni_nvme_cmd, sizeof(ni_nvme_command_t));
  p_nvme_cmd->u.GENERAL.CDW10 = p_ni_nvme_cmd->cdw10;
  p_nvme_cmd->u.GENERAL.CDW11 = p_ni_nvme_cmd->cdw11;
  p_nvme_cmd->u.GENERAL.CDW12 = p_ni_nvme_cmd->cdw12;
  p_nvme_cmd->u.GENERAL.CDW13 = p_ni_nvme_cmd->cdw13;
  p_nvme_cmd->u.GENERAL.CDW14 = p_ni_nvme_cmd->cdw14;
  p_nvme_cmd->u.GENERAL.CDW15 = p_ni_nvme_cmd->cdw15;

  if (b_admin_command)
  {
    // Admin queue
    //p_ioctl_struct->QueueId = 0;
    p_protocol_cmd->CommandSpecific = STORAGE_PROTOCOL_SPECIFIC_NVME_ADMIN_COMMAND;
    //p_nvme_cmd->NSID = 1;

  }
  else
  {
    // IO queue
    //p_ioctl_struct->QueueId = 1;
    //Set the namespace Identifier to 1 which is the only namespace for non admin commands in xCoder FW
    //p_nvme_command->NSID = 1;
    p_protocol_cmd->CommandSpecific = STORAGE_PROTOCOL_SPECIFIC_NVME_NVM_COMMAND;
    p_nvme_cmd->NSID = 1;

  }

  b_retval = DeviceIoControl(
    device_handle,
    IOCTL_STORAGE_PROTOCOL_COMMAND,
    p_buffer,
    buffer_length,
    p_buffer,
    buffer_length,
    &returned_data_len,
    NULL
  );

  if (FALSE == b_retval)
  {
    DWORD error = NI_XCODER_NUM_OF_SYSTEM_LAST_ERROR;
    NVME_ERROR_INFO_LOG* ptrLog = (NVME_ERROR_INFO_LOG*)(p_buffer + FIELD_OFFSET(STORAGE_PROTOCOL_COMMAND, Command) + STORAGE_PROTOCOL_COMMAND_LENGTH_NVME);
    ni_log(NI_LOG_TRACE, "ERROR: ni_nvme_send_win_io_command() Sending Opcode %02X FAILED!, Last Return Code: %d\n", nvme_opcode, error);
  }
  else
  {
    //Copy data to user only if the out pointer is not null and there is something to copy other than the IOCTL struct
    if( (p_user_data) && (data_len) && (returned_data_len > data_buffer_offset) )
    {
      memcpy(p_user_data, &(p_buffer[data_buffer_offset]), (returned_data_len - data_buffer_offset) );
    }
    //p_ni_nvme_completion_result_t p_completion_result = (p_ni_nvme_completion_result_t)p_ioctl_struct->CplEntry;
    PNVME_ERROR_INFO_LOG p_error_info = (PNVME_ERROR_INFO_LOG) (p_buffer + FIELD_OFFSET(STORAGE_PROTOCOL_COMMAND, Command) +
                                                                                         STORAGE_PROTOCOL_COMMAND_LENGTH_NVME);
    retval = (p_error_info->Status.AsUshort >> 1); //This is not NVME compliant but this is how xCoder firmware does it :(
    *p_result = p_protocol_cmd->FixedProtocolReturnData;

    ni_log(NI_LOG_TRACE, "ni_nvme_send_win_io_command() Sending Opcode %02X SUCCESS!!\n", nvme_opcode);
  }

  END;
  if (p_buffer)
  {
    //_aligned_free(p_buffer);
    free(p_buffer);
  }
  return retval;
}
#endif //XCODER_IO_RW_ENABLED

#else  //Using the custom STORPORT Windows NVMe Driver


int ni_nvme_enumerate_devices
(
  char   ni_devices[][MAX_DEVICE_NAME_LEN],
  int    max_handles
)
{
  TCHAR                       port_name_buffer[MAX_DEVICE_NAME_LEN] = { 0 };
  LPTSTR                      p_scsi_path = "\\\\.\\Scsi";
  LPCTSTR                     p_format = "%s%d:";
  PNVMe_COMMAND               p_ni_nvme_cmd = NULL;
  UCHAR* p_buffer = NULL;
  DWORD                       ioctl_buffer_size = sizeof(NVME_PASS_THROUGH_IOCTL) + sizeof(ADMIN_IDENTIFY_CONTROLLER);
  PNVME_PASS_THROUGH_IOCTL     p_ioctl_struct = NULL;
  DWORD                        retval = 0;
  PADMIN_IDENTIFY_COMMAND_DW10 p_nvme_identity_cmd_dw10 = NULL;
  CHAR                         firmware_revision[8] = { 0 };
  int                          device_count = 0;
  int                          scsi_port = 0;

  ni_log(NI_LOG_INFO, "Searching for NETINT NVMe devices ...\n\n");

  p_buffer = malloc(ioctl_buffer_size);
  if (NULL == p_buffer)
  {
    return NI_RETCODE_ERROR_MEM_ALOC;
  }

  memset(p_buffer, 0, ioctl_buffer_size);
  p_ioctl_struct = (PNVME_PASS_THROUGH_IOCTL)p_buffer;

  for (scsi_port = 0; scsi_port < max_handles; scsi_port++)
  {
    wsprintf(port_name_buffer, p_format, p_scsi_path, scsi_port);
    HANDLE handle = CreateFile(
        port_name_buffer,
        GENERIC_READ | GENERIC_WRITE,
        FILE_SHARE_READ | FILE_SHARE_WRITE,
        NULL,
        OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL,
        NULL
    );

    if (INVALID_HANDLE_VALUE == handle)
    {
      retval = NI_XCODER_NUM_OF_SYSTEM_LAST_ERROR;
    }
    else
    {
      ni_log(NI_LOG_INFO, "Found Possible NETINT NVMe Device at port %s \n", port_name_buffer);

      p_ioctl_struct->SrbIoCtrl.HeaderLength = sizeof(SRB_IO_CONTROL);
      memcpy(p_ioctl_struct->SrbIoCtrl.Signature, NVME_SIG_STR, strlen(NVME_SIG_STR));
      p_ioctl_struct->SrbIoCtrl.Timeout = 30;
      p_ioctl_struct->SrbIoCtrl.ControlCode = (ULONG)NVME_PASS_THROUGH_SRB_IO_CODE;
      p_ioctl_struct->SrbIoCtrl.ReturnCode = 0;
      p_ioctl_struct->SrbIoCtrl.Length = ioctl_buffer_size - sizeof(SRB_IO_CONTROL);

      p_ni_nvme_cmd = (PNVMe_COMMAND)p_ioctl_struct->NVMeCmd;
      p_ni_nvme_cmd->CDW0.OPC = ADMIN_IDENTIFY;
      p_nvme_identity_cmd_dw10 = (PADMIN_IDENTIFY_COMMAND_DW10) & (p_ni_nvme_cmd->CDW10);
      p_nvme_identity_cmd_dw10->CNS = 1;

      p_ioctl_struct->QueueId = 0; // Admin queue
      p_ioctl_struct->DataBufferLen = 0;
      p_ioctl_struct->Direction = NVME_FROM_DEV_TO_HOST;
      p_ioctl_struct->ReturnBufferLen = ioctl_buffer_size;
      p_ioctl_struct->VendorSpecific[0] = (DWORD)0;
      p_ioctl_struct->VendorSpecific[1] = (DWORD)0;

      BOOL b_status = DeviceIoControl(
          handle,
          IOCTL_SCSI_MINIPORT,
          p_ioctl_struct,
          ioctl_buffer_size,
          p_ioctl_struct,
          ioctl_buffer_size,
          &retval,
          NULL
      );

      if (FALSE == b_status)
      {
        retval = NI_XCODER_NUM_OF_SYSTEM_LAST_ERROR;
        ni_log(NI_LOG_INFO, "Device at port %s is not a NETINT NVMe device ...\n\n", port_name_buffer);
        CloseHandle(handle);
      }
      else
      {
        PADMIN_IDENTIFY_CONTROLLER p_identity_data = (PADMIN_IDENTIFY_CONTROLLER)p_ioctl_struct->DataBuffer;
        ni_nvme_identity_t* p_ni_id_data = (ni_nvme_identity_t*)p_identity_data;

        ni_log(NI_LOG_INFO, "Identity information retrieved from the device at port %s\n", port_name_buffer);
        ni_log(NI_LOG_INFO, "    VID:           0x%x \n    SSVID:         0x%x \n",
            p_identity_data->VID, p_identity_data->SSVID);

        ni_log(NI_LOG_INFO, "    Device Model:  %s \n", p_identity_data->MN);

        /* make the fw revision string null terminated */
        memset(firmware_revision, 0, sizeof(firmware_revision));
        memcpy(firmware_revision, p_identity_data->FR, sizeof(firmware_revision));
        firmware_revision[sizeof(firmware_revision) - 1] = 0;

        ni_log(NI_LOG_INFO, "    Firmware Rev:  %s \n", firmware_revision);
        ni_log(NI_LOG_INFO, "    Serial Number: %s \n", p_identity_data->SN);

        if ((NETINT_PCI_VENDOR_ID == p_identity_data->VID) &&
            (NETINT_PCI_VENDOR_ID == p_identity_data->SSVID) &&
            (p_ni_id_data->device_is_xcoder))
        {
          ni_log(NI_LOG_INFO, "NETINT %s NVMe video transcoder identified at port %s\n\n", p_identity_data->MN, port_name_buffer);

          ni_devices[device_count][0] = '\0';
          strncat(ni_devices[device_count++], port_name_buffer, MAX_DEVICE_NAME_LEN);
        }
        else
        {
          ni_log(NI_LOG_INFO, "Unknown NVMe adapter identified at port %s\n", port_name_buffer);
        }

        CloseHandle(handle);
      }
    }
  } // end for loop

  if (p_buffer)
  {
    free(p_buffer);
  }

  ni_log(NI_LOG_INFO, "Total Number of NETINT NVMe Transcoders indentified: %d \n\n", device_count);
  return device_count;
}

inline int32_t ni_nvme_send_win_io_command
(
  ni_nvme_admin_opcode_t nvme_opcode,
  ni_device_handle_t device_handle,
  ni_nvme_command_t* p_ni_nvme_cmd,
  uint32_t data_len,
  void* p_user_data,
  uint32_t* p_result,
  BOOL b_admin_command)
{
  PNVME_PASS_THROUGH_IOCTL     p_ioctl_struct = NULL;
  PNVMe_COMMAND                p_nvme_command = NULL;
  PVOID             p_buffer = NULL;
  ni_nvme_command_t* p_ni_xcoder_nvme_command = NULL;
  PNETINT_VEND_SPEC_EX         p_vendor_spec = NULL;
  DWORD                        returned_data_len = 0;
  BOOL                         b_retval = FALSE;
  BYTE             nvme_io_direction = NVME_NO_DATA_TX;
  DWORD                        buffer_length = 0;
  int32_t                      retval = NI_RETCODE_FAILURE;

  if ((!data_len) && (NULL == p_user_data))
  {
    ni_log(NI_LOG_TRACE, "WARN: ni_nvme_send_win_io_command() data_len and data parameters are NULL\n");
  }
  else if (!data_len)
  {
    ni_log(NI_LOG_TRACE, "ERROR: ni_nvme_send_win_io_command() data_len is: %d, FATAL.\n", data_len);
    LRETURN;
  }
  else if (NULL == p_user_data)
  {
    ni_log(NI_LOG_TRACE, "ERROR: ni_nvme_send_win_io_command() data is: %p, FATAL.\n", p_user_data);
    LRETURN;
  }

  //Get system mem page size
  SYSTEM_INFO sys_info;
  GetSystemInfo(&sys_info);

  buffer_length = data_len + sizeof(NVME_PASS_THROUGH_IOCTL);
  p_buffer = _aligned_malloc(buffer_length, sys_info.dwPageSize);
  if (!p_buffer)
  {
    ni_log(NI_LOG_TRACE, "ERROR: ni_nvme_send_win_io_command() allocate buffer failed, FATAL.\n");
    retval = NI_RETCODE_FAILURE;
    LRETURN;
  }

  ZeroMemory(p_buffer, sizeof(NVME_PASS_THROUGH_IOCTL));

  p_ioctl_struct = (PNVME_PASS_THROUGH_IOCTL)(p_buffer);
  //Set the vendor specific field here
  p_vendor_spec = (PNETINT_VEND_SPEC_EX)p_ioctl_struct->VendorSpecific;
  p_vendor_spec->PCIID = NETINT_PCI_VENDOR_ID;
  // this must be sthe number of words!!! WORDS not bytes we are asking to return;
  p_vendor_spec->NvmeNDP = ROUND_TO_ULONG(data_len) / sizeof(ULONG);

  switch (nvme_opcode)
  {
    case nvme_admin_cmd_xcoder_open:
    case nvme_admin_cmd_xcoder_close:
    case nvme_admin_cmd_xcoder_query:
    {
      if (data_len)
      {
        p_ioctl_struct->Direction = NVME_FROM_DEV_TO_HOST;
        p_ioctl_struct->DataBufferLen = buffer_length;
        memcpy(&(p_ioctl_struct->DataBuffer[0]), p_user_data, data_len);
      }
      else
      {
        //This is just the IOCTL command, no data, local allocation
        p_ioctl_struct->Direction = NVME_NO_DATA_TX;
        p_ioctl_struct->DataBufferLen = 0;
      }
      break;
    }

    case nvme_admin_cmd_identify:
    case nvme_admin_cmd_xcoder_read:
    case nvme_cmd_xcoder_read:
    {
      p_ioctl_struct->Direction = NVME_FROM_DEV_TO_HOST;
      p_ioctl_struct->DataBufferLen = 0;
      p_vendor_spec->NvmeNDP = ROUND_TO_ULONG(data_len) / sizeof(ULONG);
      break;
    }

    case nvme_admin_cmd_xcoder_write:
    case nvme_cmd_xcoder_write:
    case nvme_admin_cmd_xcoder_config:
    {
      if (data_len)
      {
        p_ioctl_struct->Direction = NVME_FROM_HOST_TO_DEV;
        p_ioctl_struct->DataBufferLen = buffer_length;
        memcpy(&(p_ioctl_struct->DataBuffer[0]), p_user_data, data_len);
      }
      else
      {
        //This is just the IOCTL command, no data, local allocation
        p_ioctl_struct->Direction = NVME_NO_DATA_TX;
        p_ioctl_struct->DataBufferLen = 0;
      }
      break;
    }

    default:
    {
      ni_log(NI_LOG_TRACE, "ERROR: ni_nvme_send_win_io_command() unsupported NVMe opcode: %d, isAdminOpcode: %d FATAL.\n", nvme_opcode, b_admin_command);
      LRETURN;
      break;
    }
  }

  p_ioctl_struct->ReturnBufferLen = buffer_length;


  // Set up the SRB IO Control header
  p_ioctl_struct->SrbIoCtrl.ControlCode = (ULONG)NVME_PASS_THROUGH_SRB_IO_CODE;
  memcpy(p_ioctl_struct->SrbIoCtrl.Signature, NVME_SIG_STR, strlen(NVME_SIG_STR));
  p_ioctl_struct->SrbIoCtrl.HeaderLength = (ULONG) sizeof(SRB_IO_CONTROL);
  p_ioctl_struct->SrbIoCtrl.Timeout = 30;
  p_ioctl_struct->SrbIoCtrl.Length = buffer_length - sizeof(SRB_IO_CONTROL);
  p_ioctl_struct->SrbIoCtrl.ReturnCode = 0;

  // Set up the NVMe pass through IOCTL buffer
  p_nvme_command = (PNVMe_COMMAND)p_ioctl_struct->NVMeCmd;
  p_nvme_command->CDW0.OPC = nvme_opcode; //OPCODE
  memcpy(&p_nvme_command->CDW10, p_ni_nvme_cmd, sizeof(ni_nvme_command_t));

  if (b_admin_command)
  {
    // Admin queue
    p_ioctl_struct->QueueId = 0;
  }
  else
  {
    // IO queue
    p_ioctl_struct->QueueId = 1;
    //Set the namespace Identifier to 1 which is the only namespace for non admin commands in xCoder FW
    p_nvme_command->NSID = 1;
  }

  b_retval = DeviceIoControl(device_handle,
  IOCTL_SCSI_MINIPORT,
  p_ioctl_struct,
  buffer_length,
  p_ioctl_struct,
  buffer_length,
  &returned_data_len,
  NULL);

  if (FALSE == b_retval)
  {
    DWORD error = NI_XCODER_NUM_OF_SYSTEM_LAST_ERROR;
    ni_log(NI_LOG_TRACE, "ERROR: ni_nvme_send_win_io_command() Sending Opcode %02X FAILED!, Last Return Code: %d\n", nvme_opcode, error);
  }
  else
  {
    //Copy data to user only if the out pointer is not null and there is something to copy other than the IOCTL struct
    if ((p_user_data) && (data_len) && (returned_data_len > sizeof(NVME_PASS_THROUGH_IOCTL)))
    {
      memcpy(p_user_data, &(p_ioctl_struct->DataBuffer[0]), (returned_data_len - sizeof(NVME_PASS_THROUGH_IOCTL)));
    }
    p_ni_nvme_completion_result_t p_completion_result = (p_ni_nvme_completion_result_t)p_ioctl_struct->CplEntry;
    //PNVMe_COMPLETION_QUEUE_ENTRY_DWORD_3 pCplDw3 = (PNVMe_COMPLETION_QUEUE_ENTRY_DWORD_3) & (p_ioctl_struct->CplEntry[3]);
    //rc = pCplDw3->SF.SC;
    retval = (p_completion_result->ui16Status >> 1); //This is not NVME compliant but this is how xCoder firmware does it :(
    *p_result = p_completion_result->ui32Result;

    ni_log(NI_LOG_TRACE, "ni_nvme_send_win_io_command() Sending Opcode %02X SUCCESS!!\n", nvme_opcode);
  }

  END;

  if (p_buffer)
  {
    _aligned_free(p_buffer);
  }


  return retval;
}
#endif //WIN_NVME_CUSTOM defined

#endif //_WIN32 defined

#ifndef XCODER_IO_RW_ENABLED
/*!******************************************************************************
 *  \brief  Compose a nvme admin command
 *
 *  \param  
 *
 *  \return
 *******************************************************************************/
int32_t ni_nvme_send_admin_cmd(ni_nvme_admin_opcode_t opcode, ni_device_handle_t handle, ni_nvme_command_t *p_ni_nvme_cmd, uint32_t data_len, void *p_data, uint32_t *p_result)
{
  int32_t rc;
#ifdef _WIN32
    
#ifndef WIN_NVME_CUSTOM
  //Windows inbox driver requires to send identity through different
  //api than regular pass through commands
  if (nvme_admin_cmd_identify == opcode)
  {
    rc = ni_nvme_get_identity(handle, NI_INVALID_EVENT_HANDLE, p_data);
    *p_result = NI_RETCODE_SUCCESS;
  }
  else
#endif
  {
    p_ni_nvme_cmd->cdw10 = ni_htonl(p_ni_nvme_cmd->cdw10);
    p_ni_nvme_cmd->cdw11 = ni_htonl(p_ni_nvme_cmd->cdw11);
    p_ni_nvme_cmd->cdw12 = ni_htonl(p_ni_nvme_cmd->cdw12);
    p_ni_nvme_cmd->cdw13 = ni_htonl(p_ni_nvme_cmd->cdw13);
    p_ni_nvme_cmd->cdw14 = ni_htonl(p_ni_nvme_cmd->cdw14);
    p_ni_nvme_cmd->cdw15 = ni_htonl(p_ni_nvme_cmd->cdw15);
    rc = ni_nvme_send_win_io_command(opcode, handle, p_ni_nvme_cmd, data_len, p_data, p_result, TRUE);
    *p_result = ni_htonl(*p_result);
  }
#elif __linux__
  ni_nvme_passthrough_cmd_t nvme_cmd = {0};

  nvme_cmd.opcode = opcode;
  nvme_cmd.cdw10 = (__u32)ni_htonl(p_ni_nvme_cmd->cdw10);
  nvme_cmd.cdw11 = (__u32)ni_htonl(p_ni_nvme_cmd->cdw11);
  nvme_cmd.cdw12 = (__u32)ni_htonl(p_ni_nvme_cmd->cdw12);
  nvme_cmd.cdw13 = (__u32)ni_htonl(p_ni_nvme_cmd->cdw13);
  nvme_cmd.cdw14 = (__u32)ni_htonl(p_ni_nvme_cmd->cdw14);
  nvme_cmd.cdw15 = (__u32)ni_htonl(p_ni_nvme_cmd->cdw15);
  nvme_cmd.addr = (__u64)ni_htonll((uintptr_t)p_data);
  nvme_cmd.data_len = ni_htonl(data_len);

  rc = ni_nvme_send_admin_pass_through_command(handle, &nvme_cmd);

  *p_result = ni_htonl(nvme_cmd.result);
#endif //__linux__ defined

  ni_log(NI_LOG_TRACE, "ni_nvme_send_admin_cmd: handle=%" PRIx64 ", result=%08x, rc=%d\n", (int64_t)handle, (uint32_t)(*p_result), rc);

  return rc;
}

/*!******************************************************************************
 *  \brief  Compose a nvme io command
 *
 *  \param  
 *
 *  \return
 *******************************************************************************/
int32_t ni_nvme_send_io_cmd(ni_nvme_opcode_t opcode, ni_device_handle_t handle, ni_nvme_command_t *p_ni_nvme_cmd, uint32_t data_len, void *p_data, uint32_t *p_result)
{
  int32_t rc;
#ifdef _WIN32
  p_ni_nvme_cmd->cdw10 = ni_htonl(p_ni_nvme_cmd->cdw10);
  p_ni_nvme_cmd->cdw11 = ni_htonl(p_ni_nvme_cmd->cdw11);
  p_ni_nvme_cmd->cdw12 = ni_htonl(p_ni_nvme_cmd->cdw12);
  p_ni_nvme_cmd->cdw13 = ni_htonl(p_ni_nvme_cmd->cdw13);
  p_ni_nvme_cmd->cdw14 = ni_htonl(p_ni_nvme_cmd->cdw14);
  p_ni_nvme_cmd->cdw15 = ni_htonl(p_ni_nvme_cmd->cdw15);
  rc = ni_nvme_send_win_io_command(opcode, handle, p_ni_nvme_cmd, data_len, p_data, p_result, FALSE);
  *p_result = ni_htonl(*p_result);
#elif __linux__
  ni_nvme_passthrough_cmd_t nvme_cmd;
  //int32_t *p_addr = NULL;

  memset(&nvme_cmd, 0, sizeof(nvme_cmd));
  nvme_cmd.opcode = opcode;
  nvme_cmd.nsid = ni_htonl(1);
  nvme_cmd.addr = (__u64)ni_htonll((uintptr_t)p_data);
  nvme_cmd.data_len = ni_htonl(data_len);
  nvme_cmd.cdw2 = (__u32)ni_htonl(p_ni_nvme_cmd->cdw2);
  nvme_cmd.cdw3 = (__u32)ni_htonl(p_ni_nvme_cmd->cdw3);
  nvme_cmd.cdw10 = (__u32)ni_htonl(p_ni_nvme_cmd->cdw10);
  nvme_cmd.cdw11 = (__u32)ni_htonl(p_ni_nvme_cmd->cdw11);
  nvme_cmd.cdw12 = (__u32)ni_htonl(p_ni_nvme_cmd->cdw12);
  nvme_cmd.cdw13 = (__u32)ni_htonl(p_ni_nvme_cmd->cdw13);
  nvme_cmd.cdw14 = (__u32)ni_htonl(p_ni_nvme_cmd->cdw14);
  nvme_cmd.cdw15 = (__u32)ni_htonl(p_ni_nvme_cmd->cdw15);

  rc = ni_nvme_send_io_pass_through_command(handle, &nvme_cmd);

  //p_addr = &nvme_cmd.result;
  //*!pResult = *p_addr;
  *p_result = ni_htonl(nvme_cmd.result);
#endif //__linux__ defined

  ni_log(NI_LOG_TRACE, "ni_nvme_send_io_cmd: handle=%" PRIx64 ", result=%08x, rc=%d\n", (int64_t)handle, (uint32_t)(*p_result), rc);

  return rc;
}

#ifdef __linux__


/*!******************************************************************************
 *  \brief  Compose a nvme io through admin command
 *
 *  \param  
 *
 *  \return
 *******************************************************************************/
int32_t ni_nvme_send_io_cmd_thru_admin_queue(ni_nvme_admin_opcode_t opcode, ni_device_handle_t handle, ni_nvme_command_t *p_ni_nvme_cmd, uint32_t data_len, void *p_data, uint32_t *p_result)
{
  int32_t rc;
  ni_nvme_passthrough_cmd_t nvme_cmd = {0};
  int32_t *p_addr = NULL;

  nvme_cmd.opcode = opcode;
  nvme_cmd.nsid = ni_htonl(1);
  nvme_cmd.addr = (__u64)ni_htonll((uintptr_t)p_data);
  nvme_cmd.data_len = ni_htonl(data_len);
  nvme_cmd.cdw10 = (__u32)ni_htonl(p_ni_nvme_cmd->cdw10);
  nvme_cmd.cdw11 = (__u32)ni_htonl(p_ni_nvme_cmd->cdw11);
  nvme_cmd.cdw12 = (__u32)ni_htonl(p_ni_nvme_cmd->cdw12);
  nvme_cmd.cdw13 = (__u32)ni_htonl(p_ni_nvme_cmd->cdw13);
  nvme_cmd.cdw14 = (__u32)ni_htonl(p_ni_nvme_cmd->cdw14);
  nvme_cmd.cdw15 = (__u32)ni_htonl(p_ni_nvme_cmd->cdw15);

  rc = ni_nvme_send_admin_pass_through_command(handle, &nvme_cmd);

  //p_addr = &nvme_cmd.result;
  //*!pResult = *p_addr;
  *p_result = ni_htonl(nvme_cmd.result);

  ni_log(NI_LOG_TRACE, "ni_nvme_send_io_cmd_thru_admin_queue: handle=%d, result=%08x, rc=%d\n", handle, *p_result, rc);
  return rc;
}
#endif //__linux__ defined
#else //XCODER_IO_RW_ENABLED

/*!******************************************************************************
 *  \brief  parse the lba opcode, subtype, option
 *          It's called only if a I/O read/write fails,
 *          so just use the print level "NI_LOG_ERROR" now.
 *
 *  \param  lba is 4k aligned
 *
 *  \return
 *******************************************************************************/
void ni_parse_lba(uint64_t lba)
{
  uint64_t lba_high = (lba >> NI_INSTANCE_TYPE_OFFSET);
  uint64_t lba_low = (lba & 0x3FFFF);
  uint8_t device_type = (lba_high & NI_DEVICE_TYPE_ENCODER);
  uint16_t session_id = (lba_high >> (NI_SESSION_ID_OFFSET - NI_INSTANCE_TYPE_OFFSET));
  if (device_type == NI_DEVICE_TYPE_ENCODER)
  {
    ni_log(NI_LOG_ERROR, "encoder lba:0x%" PRIx64 "(4K-aligned), 0x%" PRIx64 "(512B-aligned), session ID:%u\n", lba, (lba<<3), session_id);
    if (lba_low >= WR_OFFSET_IN_4K)
    {
      ni_log(NI_LOG_ERROR, "encoder send frame failed\n");
    }
    else if(lba_low >= RD_OFFSET_IN_4K)
    {
      ni_log(NI_LOG_ERROR, "encoder receive packet failed\n");
    }
    else
    {
      ni_log(NI_LOG_ERROR, "encoder ctrl command failed: op-0x%x, subtype-0x%x, option-0x%x\n", \
                      (uint32_t)(((lba_low - START_OFFSET_IN_4K)>>NI_OP_BIT_OFFSET)+0xD0), \
                      (uint32_t)((lba_low>>NI_SUB_BIT_OFFSET) & 0xF), \
                      (uint32_t)(lba_low & 0xF));
    }
  }
  else
  {
    ni_log(NI_LOG_ERROR, "decoder lba:0x%" PRIx64 "(4K-aligned), 0x%" PRIx64 "(512B-aligned), session ID:%u\n", lba, (lba<<3), session_id);
    if (lba_low >= WR_OFFSET_IN_4K)
    {
      ni_log(NI_LOG_ERROR, "decoder send packet failed\n");
    }
    else if(lba_low >= RD_OFFSET_IN_4K)
    {
      ni_log(NI_LOG_ERROR, "decoder receive frame failed\n");
    }
    else
    {
      ni_log(NI_LOG_ERROR, "decoder ctrl command failed: op-0x%x, subtype-0x%x, option-0x%x\n", \
                      (uint32_t)(((lba_low - START_OFFSET_IN_4K)>>NI_OP_BIT_OFFSET)+0xD0), \
                      (uint32_t)((lba_low>>NI_SUB_BIT_OFFSET) & 0xF), \
                      (uint32_t)(lba_low & 0xF));
    }
  }
}

/*!******************************************************************************
 *  \brief  Compose a io read command
 *
 *  \param  
 *
 *  \return value < 0, failed, return failure code.
 *          value >= 0, success, 
 *                      windows return success code, 
 *                      linux return actural size
 *******************************************************************************/
int32_t ni_nvme_send_read_cmd(ni_device_handle_t handle, ni_event_handle_t event_handle, void *p_data, uint32_t data_len, uint32_t lba)
{
  int32_t rc;
  uint64_t offset = (uint64_t)lba << LBA_BIT_OFFSET;
#ifdef _WIN32
  uint32_t offset_l = (uint32_t)(offset & 0xFFFFFFFF);
  DWORD offset_h = (DWORD)(offset >> 32);
  OVERLAPPED overlap;
  ni_log(NI_LOG_TRACE, "ni_nvme_send_read_cmd: handle=%" PRIx64 ", lba=0x%x, len=%d,offset:0x%x,0x%x\n", 
    (int64_t)handle, (lba<<3), data_len,offset_l,offset_h);
  memset(&overlap, 0, sizeof(overlap));
  overlap.Offset = offset_l;
  overlap.OffsetHigh = offset_h;
  overlap.hEvent = event_handle;
  
  DWORD data_len_; //data real count
  rc = ReadFile(
        handle,
        p_data,
        data_len,
        &data_len_,
        &overlap
    );
  ni_log(NI_LOG_TRACE, "rc=%d,actlen=%d\n",rc,data_len_);
  if (rc == FALSE)
  {
    rc = NI_XCODER_NUM_OF_SYSTEM_LAST_ERROR;
    ni_log(NI_LOG_TRACE, "ni_nvme_send_read_cmd() ReadFile handle=%" PRIx64 ", event_handle=%" PRIx64 ", lba=0x%x, len=%d, rc=%d\n", 
        (int64_t)handle, (int64_t)event_handle, (lba<<3), data_len, rc);
    if(rc == ERROR_IO_PENDING)
    {
      rc = WaitForSingleObject(event_handle, 100);
      if (rc == WAIT_OBJECT_0)
      {
        ResetEvent(event_handle);        
        // add GetOverlappedResult to check the result, and update system last error
        rc = GetOverlappedResult(handle,
                        &overlap,
                        &data_len_,
                        FALSE);
        if (rc ==  FALSE)
        {
          rc = NI_XCODER_NUM_OF_SYSTEM_LAST_ERROR;
          ni_log(NI_LOG_ERROR, "ERROR %d: ni_nvme_send_read_cmd() failed\n",rc);
          ni_parse_lba(lba);
          rc = NI_RETCODE_ERROR_NVME_CMD_FAILED;
        }
        else
        {
          ni_log(NI_LOG_TRACE, "ni_nvme_send_read_cmd() wait success\n");
          rc = NI_RETCODE_SUCCESS;
        }
      }
      else if (rc == WAIT_TIMEOUT)
      {
        ni_log(NI_LOG_ERROR, "ni_nvme_send_read_cmd() Error: wait timeout\n");
        ni_parse_lba(lba);
        rc = NI_RETCODE_ERROR_NVME_CMD_FAILED;
      }
      else
      {
        rc = NI_XCODER_NUM_OF_SYSTEM_LAST_ERROR;
        ni_log(NI_LOG_ERROR, "ERROR %d: ni_nvme_send_read_cmd() wait Other\n",rc);
        ni_parse_lba(lba);
        rc = NI_RETCODE_ERROR_NVME_CMD_FAILED;
      }
    }
    else
    {
      ni_log(NI_LOG_ERROR, "ni_nvme_send_read_cmd() Other Error:%d\n",rc);
      ni_parse_lba(lba);
      rc = NI_RETCODE_ERROR_NVME_CMD_FAILED;
    }
  }
  else
  {
      ni_log(NI_LOG_TRACE, "ni_nvme_send_read_cmd() ReadFile success handle=%" PRIx64 ", event_handle=%" PRIx64 ", lba=0x%x, len=%d, rc=%d,0x%x\n", 
        (int64_t)handle, (int64_t)event_handle, (lba<<3), data_len, rc);
      rc = NI_RETCODE_SUCCESS;
  }

  return rc;

#else
  rc = pread(handle, p_data, data_len, offset);
  ni_log(NI_LOG_TRACE, "ni_nvme_send_read_cmd: handle=%" PRIx64 ", lba=0x%x, len=%d, rc=%d\n", (int64_t)handle, (lba<<3), data_len, rc);
  if ((rc < 0) || (rc != data_len))
  {
    ni_log(NI_LOG_ERROR, "ERROR %d: ni_nvme_send_read_cmd failed, lba=0x%x, len=%d, rc=%d, error=%d\n",
           NI_XCODER_NUM_OF_SYSTEM_LAST_ERROR,
           (lba<<3),
           data_len,
           rc,
           NI_XCODER_NUM_OF_SYSTEM_LAST_ERROR);
    ni_parse_lba(lba);
    rc = NI_RETCODE_ERROR_NVME_CMD_FAILED;
  }
  else
  {
    rc = NI_RETCODE_SUCCESS;
  }
  return rc;
#endif
}

/*!******************************************************************************
 *  \brief  Compose a io write command
 *
 *  \param  
 *
 *  \return value < 0, failed, return failure code.
 *          value >= 0, success, 
 *                      windows return success code, 
 *                      linux return actural size
 *******************************************************************************/
int32_t ni_nvme_send_write_cmd(ni_device_handle_t handle, ni_event_handle_t event_handle, void *p_data, uint32_t data_len, uint32_t lba)
{
  int32_t rc;
  uint64_t offset = (uint64_t)lba << LBA_BIT_OFFSET;
#ifdef _WIN32
  uint32_t offset_l = (uint32_t)(offset & 0xFFFFFFFF);
  DWORD offset_h = (DWORD)(offset >> 32);
  OVERLAPPED overlap;
  ni_log(NI_LOG_TRACE, "ni_nvme_send_write_cmd: handle=%" PRIx64 ", lba=0x%x, len=%d,offset:0x%x,0x%x\n", 
    (int64_t)handle, (lba<<3), data_len,offset_l,offset_h);

  memset(&overlap, 0, sizeof(overlap));
  overlap.Offset = offset_l;
  overlap.OffsetHigh = offset_h;
  overlap.hEvent = event_handle;

  DWORD data_len_; //data real count
  rc = WriteFile(
        handle,
        p_data,
        data_len,
        &data_len_,
        &overlap
    );
  ni_log(NI_LOG_TRACE, "rc=%d,actlen=%d\n",rc,data_len_);
  if (rc == FALSE)
  {
    rc = NI_XCODER_NUM_OF_SYSTEM_LAST_ERROR;
    ni_log(NI_LOG_TRACE, "ni_nvme_send_write_cmd() WriteFile handle=%" PRIx64 ", event_handle=%" PRIx64 ", lba=0x%x, len=%d, rc=%d\n", 
        (int64_t)handle, (int64_t)event_handle, (lba<<3), data_len, rc);
    if(rc == ERROR_IO_PENDING)
    {
      rc = WaitForSingleObject(event_handle, 100);
      if (rc == WAIT_OBJECT_0)
      {
        ResetEvent(event_handle);
        // add GetOverlappedResult to check the result, and update system last error
        rc = GetOverlappedResult(handle,
                        &overlap,
                        &data_len_,
                        FALSE);
        if (rc ==  FALSE)
        {
          rc = NI_XCODER_NUM_OF_SYSTEM_LAST_ERROR;
          ni_log(NI_LOG_ERROR, "ERROR %d: ni_nvme_send_write_cmd() failed\n",rc);
          ni_parse_lba(lba);
          rc = NI_RETCODE_ERROR_NVME_CMD_FAILED;
        }
        else
        {
          ni_log(NI_LOG_TRACE, "ni_nvme_send_write_cmd() wait success\n");
          rc = NI_RETCODE_SUCCESS;
        }
      }
      else if (rc == WAIT_TIMEOUT)
      {
        ni_log(NI_LOG_ERROR, "ni_nvme_send_write_cmd() Error: wait timeout\n");
        ni_parse_lba(lba);
        rc = NI_RETCODE_ERROR_NVME_CMD_FAILED;
      }
      else
      {
        rc = NI_XCODER_NUM_OF_SYSTEM_LAST_ERROR;
        ni_log(NI_LOG_ERROR, " ERROR %d: ni_nvme_send_write_cmd() wait other\n",rc);        
        ni_parse_lba(lba);
        rc = NI_RETCODE_ERROR_NVME_CMD_FAILED;
      }
    }
    else
    {
      ni_log(NI_LOG_ERROR, "ni_nvme_send_write_cmd() Other Error:%d, handle=%" PRIx64 ", Event_handel=%" PRIx64 ",\n",rc, (int64_t)handle, (int64_t)event_handle);
      ni_parse_lba(lba);
      rc = NI_RETCODE_ERROR_NVME_CMD_FAILED;
    }
  }
  else
  {
      ni_log(NI_LOG_TRACE, "ni_nvme_send_write_cmd() ReadFile success handle=%" PRIx64 ", event_handle=%" PRIx64 ", lba=0x%x, len=%d, rc=%d, 0x%x\n", 
        (int64_t)handle, (int64_t)event_handle, (lba<<3), data_len, rc);
      rc = NI_RETCODE_SUCCESS;
  }

  return rc;
#else
  rc = pwrite(handle, p_data, data_len, offset);
  ni_log(NI_LOG_TRACE, "ni_nvme_send_write_cmd: handle=%" PRIx64 ", lba=0x%x, len=%d, rc=%d\n", (int64_t)handle, (lba<<3), data_len, rc);
  if ((rc < 0) || (rc != data_len))
  {
    ni_log(NI_LOG_ERROR, "ERROR %d: ni_nvme_send_write_cmd failed, lba=0x%x, len=%d, rc=%d, error=%d\n",
           NI_XCODER_NUM_OF_SYSTEM_LAST_ERROR,
           (lba<<3),
           data_len,
           rc,
           NI_XCODER_NUM_OF_SYSTEM_LAST_ERROR);
    ni_parse_lba(lba);
    rc = NI_RETCODE_ERROR_NVME_CMD_FAILED;
  }
  else
  {
    rc = NI_RETCODE_SUCCESS;
  }

  return rc;
#endif
}
#endif //XCODER_IO_RW_ENABLED

