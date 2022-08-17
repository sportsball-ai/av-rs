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
*   \file   ni_nvme.c
*
*  \brief  Private routines related to working with NI Quadra over NVME interface
*
*******************************************************************************/

#include <stdint.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>

#ifdef _WIN32
  #include <windows.h>
#include <ntddstor.h>
#include <winioctl.h>
#include <Ntddscsi.h>
#elif __linux__ || __APPLE__
#if __linux__
#include <linux/types.h>
#endif
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
ni_retcode_t ni_nvme_check_error_code(int rc, int opcode, uint32_t xcoder_type,
                                      uint32_t hw_id, uint32_t *p_instance_id)
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
            ni_log(NI_LOG_ERROR,
                   "Error rc = 0x%x, op = %02x, %s %u.%u terminating?"
                   "\n",
                   rc, opcode, device_type_str[xcoder_type], hw_id,
                   *p_instance_id);

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
            break;   // nothing
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
int32_t
ni_nvme_send_admin_pass_through_command(ni_device_handle_t handle,
                                        ni_nvme_passthrough_cmd_t *p_cmd)
{
    ni_log(NI_LOG_TRACE, "%s: handle=%d\n", __func__, handle);
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

    return ioctl(handle, NVME_IOCTL_ADMIN_CMD, p_cmd);
}

/*!******************************************************************************
 *  \brief  Submit a nvme io passthrough command to the driver
 *
 *  \param
 *
 *  \return
 *******************************************************************************/
int32_t ni_nvme_send_io_pass_through_command(ni_device_handle_t handle,
                                             ni_nvme_passthrough_cmd_t *p_cmd)
{
    ni_log(NI_LOG_TRACE, "%s: handle=%d\n", __func__, handle);
    ni_log(NI_LOG_TRACE, "opcode:       %02x\n", p_cmd->opcode);
    ni_log(NI_LOG_TRACE, "flags:        %02x\n", p_cmd->flags);
    ni_log(NI_LOG_TRACE, "rsvd1:        %04x\n", p_cmd->rsvd1);
    ni_log(NI_LOG_TRACE, "nsid:         %08x\n", p_cmd->nsid);
    ni_log(NI_LOG_TRACE, "cdw2:         %08x\n", p_cmd->cdw2);
    ni_log(NI_LOG_TRACE, "cdw3:         %08x\n", p_cmd->cdw3);
    // ni_log(NI_LOG_TRACE, "metadata:     %"PRIx64"\n", p_cmd->metadata);
    // ni_log(NI_LOG_TRACE, "addr:         %"PRIx64"\n", p_cmd->addr);
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

    return ioctl(handle, NVME_IOCTL_IO_CMD, p_cmd);
}

#endif //__linux__ defined

#ifdef _WIN32

#define ROUND_TO_DWORD_PTR(x) ni_round_up(x,sizeof(PDWORD))

static ni_retcode_t ni_nvme_get_identity(HANDLE handle, HANDLE event_handle,
                                         PVOID p_identity)
{
    uint32_t lba = IDENTIFY_DEVICE_R;
    DWORD data_len = NI_NVME_IDENTITY_CMD_DATA_SZ;
    return ni_nvme_send_read_cmd(handle, event_handle, p_identity, data_len,
                                 lba);
}

int ni_nvme_enumerate_devices
(
    char   ni_devices[][NI_MAX_DEVICE_NAME_LEN],
    int    max_handles
)
{
    TCHAR port_name_buffer[NI_MAX_DEVICE_NAME_LEN] = {0};
    LPTSTR p_scsi_path = "\\\\.\\PHYSICALDRIVE";
    LPCTSTR p_format = "%s%d";
    PUCHAR p_buffer = NULL;
    ni_retcode_t rc;
    CHAR firmware_revision[8] = {0};
    int device_count = 0;
    int scsi_port = 0;
    DWORD retval;
    DWORD data_len = NI_NVME_IDENTITY_CMD_DATA_SZ;
    CHAR serial_num[20] = {0};
    CHAR model_name[40] = {0};
    ni_nvme_identity_t *p_ni_identity = NULL;

    printf("Searching for NETINT NVMe devices ...\n\n");

    if (ni_posix_memalign((void **)(&p_buffer), sysconf(_SC_PAGESIZE),
                          data_len))
    {
        ni_log(NI_LOG_ERROR, "ERROR %d: %s() alloc buffer failed\n", NI_ERRNO,
               __func__);
        return NI_RETCODE_ERROR_MEM_ALOC;
    }

    memset(p_buffer, 0, data_len);
    ni_event_handle_t event_handle = NULL;

    for (scsi_port = 0; scsi_port < max_handles; scsi_port++)
    {
        wsprintf(port_name_buffer, p_format, p_scsi_path, scsi_port);
        HANDLE handle =
            CreateFile(port_name_buffer, GENERIC_READ | GENERIC_WRITE,
                       FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING,
                       FILE_ATTRIBUTE_NORMAL, NULL);
        if (INVALID_HANDLE_VALUE == handle)
        {
            retval = NI_ERRNO;
            ni_log(NI_LOG_DEBUG, "Bad handle, ret=%d\n", retval);
        }
        else
        {
            ZeroMemory(p_buffer, data_len);
            rc = ni_nvme_get_identity(handle, event_handle, p_buffer);
            if (NI_RETCODE_SUCCESS == rc)
            {
                ni_nvme_identity_t *p_ni_id_data =
                    (ni_nvme_identity_t *)p_buffer;
                ni_log(NI_LOG_DEBUG,
                       "Identity information retrieved from the device "
                       "at port %s\n",
                       port_name_buffer);
                ni_log(NI_LOG_DEBUG,
                       "    VID:           0x%x \n    SSVID:         0x%x \n",
                       p_ni_id_data->ui16Vid, p_ni_id_data->ui16Ssvid);

                if (NETINT_PCI_VENDOR_ID == p_ni_id_data->ui16Vid &&
                    NETINT_PCI_VENDOR_ID == p_ni_id_data->ui16Ssvid &&
                    is_supported_xcoder(p_ni_id_data->device_is_xcoder))
                {
                    /* make the fw revision/model name/serial num to string*/
                    memset(firmware_revision, 0, sizeof(firmware_revision));
                    memcpy(firmware_revision, p_ni_id_data->ai8Fr,
                           sizeof(firmware_revision));
                    firmware_revision[sizeof(firmware_revision) - 1] = 0;

                    memset(model_name, 0, sizeof(model_name));
                    memcpy(model_name, p_ni_id_data->ai8Mn, sizeof(model_name));
                    model_name[sizeof(model_name) - 1] = 0;

                    memset(serial_num, 0, sizeof(serial_num));
                    memcpy(serial_num, p_ni_id_data->ai8Sn, sizeof(serial_num));
                    serial_num[sizeof(serial_num) - 1] = 0;
                    ni_log(NI_LOG_DEBUG, "    Device Model:  %s \n",
                           model_name);
                    ni_log(NI_LOG_DEBUG, "    Firmware Rev:  %s \n",
                           firmware_revision);
                    ni_log(NI_LOG_DEBUG, "    Serial Number: %s \n",
                           serial_num);
                    ni_log(NI_LOG_DEBUG,
                           "NETINT %s NVMe video transcoder identified "
                           "at port %s\n\n",
                           model_name, port_name_buffer);
                    strncpy(ni_devices[device_count++], port_name_buffer,
                            NI_MAX_DEVICE_NAME_LEN);
                } else
                {
                    ni_log(NI_LOG_DEBUG,
                           "Device at port %s %d is not a NETINT NVMe device\n",
                           port_name_buffer, p_ni_id_data->device_is_xcoder);
                }
                CloseHandle(handle);
            } else
            {
                retval = NI_ERRNO;
                ni_log(
                    NI_LOG_DEBUG,
                    "Device at port %s is not a NETINT NVMe device,ret=%d\n\n",
                    port_name_buffer, retval);
                CloseHandle(handle);
            }
        }
    }   // end for loop

    if (p_buffer)
    {
        free(p_buffer);
    }

    ni_log(NI_LOG_INFO,
           "Total Number of NETINT NVMe Transcoders identified: %d \n\n",
           device_count);
    return device_count;
}

#endif //_WIN32 defined

#ifndef XCODER_IO_RW_ENABLED
/*!******************************************************************************
 *  \brief  Compose a nvme admin command
 *
 *  \param
 *
 *  \return
 *******************************************************************************/
int32_t ni_nvme_send_admin_cmd(ni_nvme_admin_opcode_t opcode,
                               ni_device_handle_t handle,
                               ni_nvme_command_t *p_ni_nvme_cmd,
                               uint32_t data_len, void *p_data,
                               uint32_t *p_result)
{
    int32_t rc = -1;
#ifdef _WIN32
    //Windows inbox driver requires to send identity through different
    //api than regular pass through commands
    if (nvme_admin_cmd_identify == opcode)
    {
        rc = ni_nvme_get_identity(handle, NI_INVALID_EVENT_HANDLE, p_data);
        *p_result = NI_RETCODE_SUCCESS;
    }
#elif __APPLE__
#else
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
#endif

    ni_log(NI_LOG_DEBUG, "%s: handle=%" PRIx64 ", result=%08x, rc=%d\n",
           __func__, (int64_t)handle, (uint32_t)(*p_result), rc);

    return rc;
}

/*!******************************************************************************
 *  \brief  Compose a nvme io command
 *
 *  \param
 *
 *  \return
 *******************************************************************************/
int32_t ni_nvme_send_io_cmd(ni_nvme_opcode_t opcode, ni_device_handle_t handle,
                            ni_nvme_command_t *p_ni_nvme_cmd, uint32_t data_len,
                            void *p_data, uint32_t *p_result)
{
    int32_t rc = -1;
#ifdef _WIN32
#elif __APPLE__
#else
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
#endif

    ni_log(NI_LOG_DEBUG, "%s: handle=%" PRIx64 ", result=%08x, rc=%d\n",
           __func__, (int64_t)handle, (uint32_t)(*p_result), rc);

    return rc;
}
#endif

#ifdef __linux__

/*!******************************************************************************
 *  \brief  Compose a nvme io through admin command
 *
 *  \param
 *
 *  \return
 *******************************************************************************/
int32_t ni_nvme_send_io_cmd_thru_admin_queue(ni_nvme_admin_opcode_t opcode,
                                             ni_device_handle_t handle,
                                             ni_nvme_command_t *p_ni_nvme_cmd,
                                             uint32_t data_len, void *p_data,
                                             uint32_t *p_result)
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

    ni_log(NI_LOG_DEBUG, "%s: handle=%d, result=%08x, rc=%d\n", __func__,
           handle, *p_result, rc);

    return rc;
}
#endif //__linux__ defined

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
    uint16_t session_id = (uint16_t)(
        lba_high >> (NI_SESSION_ID_OFFSET - NI_INSTANCE_TYPE_OFFSET));
    if (device_type == NI_DEVICE_TYPE_ENCODER)
    {
        ni_log(NI_LOG_DEBUG,
               "encoder lba:0x%" PRIx64 "(4K-aligned), 0x%" PRIx64
               "(512B-aligned), session ID:%u\n",
               lba, (lba << 3), session_id);
        if (lba_low >= WR_OFFSET_IN_4K)
        {
            ni_log(NI_LOG_ERROR, "encoder send frame failed\n");
        } else if (lba_low >= RD_OFFSET_IN_4K)
        {
            ni_log(NI_LOG_ERROR, "encoder receive packet failed\n");
        } else
        {
            ni_log(NI_LOG_ERROR,
                   "encoder ctrl command failed: op-0x%x, "
                   "subtype-0x%x, option-0x%x\n",
                   (uint32_t)(
                       ((lba_low - START_OFFSET_IN_4K) >> NI_OP_BIT_OFFSET) +
                       0xD0),
                   (uint32_t)((lba_low >> NI_SUB_BIT_OFFSET) & 0xF),
                   (uint32_t)(lba_low & 0xF));
        }
    }
    else
    {
        ni_log(NI_LOG_DEBUG,
               "decoder lba:0x%" PRIx64 "(4K-aligned), 0x%" PRIx64
               "(512B-aligned), session ID:%u\n",
               lba, (lba << 3), session_id);
        if (lba_low >= WR_OFFSET_IN_4K)
        {
            ni_log(NI_LOG_ERROR, "decoder send packet failed\n");
        } else if (lba_low >= RD_OFFSET_IN_4K)
        {
            ni_log(NI_LOG_ERROR, "decoder receive frame failed\n");
        } else
        {
            ni_log(NI_LOG_ERROR,
                   "decoder ctrl command failed: op-0x%x, "
                   "subtype-0x%x, option-0x%x\n",
                   (uint32_t)(
                       ((lba_low - START_OFFSET_IN_4K) >> NI_OP_BIT_OFFSET) +
                       0xD0),
                   (uint32_t)((lba_low >> NI_SUB_BIT_OFFSET) & 0xF),
                   (uint32_t)(lba_low & 0xF));
        }
    }
}

/*!******************************************************************************
 *  \brief  Compose an io read command
 *
 *  \param
 *
 *  \return value < 0, failed, return failure code.
 *          value >= 0, success,
 *                      windows return success code,
 *                      linux return actural size
 *******************************************************************************/
int32_t ni_nvme_send_read_cmd(ni_device_handle_t handle,
                              ni_event_handle_t event_handle, void *p_data,
                              uint32_t data_len, uint32_t lba)
{
    int32_t rc;
    uint64_t offset = (uint64_t)lba << LBA_BIT_OFFSET;
#ifdef _WIN32
    uint32_t offset_l = (uint32_t)(offset & 0xFFFFFFFF);
    DWORD offset_h = (DWORD)(offset >> 32);
    OVERLAPPED overlap;
    ni_log(NI_LOG_TRACE,
           "%s: handle=%" PRIx64 ", lba=0x%x, len=%d,offset:0x%x,0x%x\n",
           __func__, (int64_t)handle, (lba << 3), data_len, offset_l, offset_h);
    memset(&overlap, 0, sizeof(overlap));
    overlap.Offset = offset_l;
    overlap.OffsetHigh = offset_h;

    DWORD data_len_;   //data real count
    rc = ReadFile(handle, p_data, data_len, &data_len_, &overlap);
    ni_log(NI_LOG_TRACE, "rc=%d,actlen=%d\n", rc, data_len_);
    if (rc == FALSE)
    {
        rc = NI_ERRNO;
        ni_log(NI_LOG_DEBUG,
               "%s() ReadFile handle=%" PRIx64 ", event_handle="
               "%" PRIx64 ", lba=0x%x, len=%d, rc=%d\n",
               __func__, (int64_t)handle, (int64_t)event_handle, (lba << 3),
               data_len, rc);
        ni_log(NI_LOG_ERROR, "ERROR %d: %s() failed\n", rc, __func__);
        rc = NI_RETCODE_ERROR_NVME_CMD_FAILED;
    } else
    {
        ni_log(NI_LOG_DEBUG, "%s() wait success\n", __func__);
        rc = NI_RETCODE_SUCCESS;
    }
#else
    if (handle != 0 && p_data != NULL)
    {
        if (((uintptr_t)p_data) % NI_MEM_PAGE_ALIGNMENT)
        {
            ni_log(NI_LOG_DEBUG,
                   "%s: Buffer not %d aligned = %p! Reading to aligned memory "
                   "and copying.\n",
                   __func__, NI_MEM_PAGE_ALIGNMENT, p_data);
            void *p_buf = NULL;
            if (ni_posix_memalign(&p_buf, sysconf(_SC_PAGESIZE), data_len))
            {
                ni_log(NI_LOG_ERROR,
                       "ERROR %d: %s() alloc data buffer failed\n", NI_ERRNO,
                       __func__);
                rc = NI_RETCODE_ERROR_MEM_ALOC;
            }
            else
            {
                rc = pread(handle, p_buf, data_len, offset);
                if (rc >= 0)//copy only if anything has been read
                {
                    memcpy(p_data, p_buf, data_len);
                }
                ni_aligned_free(p_buf);
            }
        } else
        {
            rc = pread(handle, p_data, data_len, offset);
        }
        ni_log(NI_LOG_TRACE,
               "%s: handle=%" PRIx64
               ", offset 0x%lx, lba=0x%x, len=%d, rc=%d\n",
               __func__, (int64_t)handle, offset, (lba << 3), data_len, rc);
        if (rc < 0 || rc != data_len)
        {
            ni_log(NI_LOG_ERROR,
                   "ERROR %d: %s failed, lba=0x%x, len=%u, rc=%d, error=%d\n",
                   NI_ERRNO, __func__, (lba << 3), data_len, rc, NI_ERRNO);
            ni_parse_lba(lba);
            rc = NI_RETCODE_ERROR_NVME_CMD_FAILED;
        } else
        {
            rc = NI_RETCODE_SUCCESS;
        }
    } else
    {
        rc = NI_RETCODE_ERROR_NVME_CMD_FAILED;
    }
#endif
    return rc;
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
int32_t ni_nvme_send_write_cmd(ni_device_handle_t handle,
                               ni_event_handle_t event_handle, void *p_data,
                               uint32_t data_len, uint32_t lba)
{
    int32_t rc;
    uint64_t offset = (uint64_t)lba << LBA_BIT_OFFSET;
#ifdef _WIN32
    uint32_t offset_l = (uint32_t)(offset & 0xFFFFFFFF);
    DWORD offset_h = (DWORD)(offset >> 32);
    DWORD data_len_;   //data real count
    OVERLAPPED overlap;

    ni_log(NI_LOG_TRACE,
           "%s: handle=%" PRIx64 ", lba=0x%x, len=%d,offset:0x%x,0x%x\n",
           __func__, (int64_t)handle, (lba << 3), data_len, offset_l, offset_h);

    memset(&overlap, 0, sizeof(overlap));
    overlap.Offset = offset_l;
    overlap.OffsetHigh = offset_h;

    rc = WriteFile(handle, p_data, data_len, &data_len_, &overlap);
    ni_log(NI_LOG_TRACE, "rc=%d,actlen=%d\n", rc, data_len_);
    if (rc == FALSE)
    {
        rc = NI_ERRNO;
        ni_log(NI_LOG_DEBUG,
               "%s() WriteFile handle=%" PRIx64 ", event_handle="
               "%" PRIx64 ", lba=0x%x, len=%d, rc=%d\n",
               __func__, (int64_t)handle, (int64_t)event_handle, (lba << 3),
               data_len, rc);
        ni_log(NI_LOG_ERROR, "ERROR %d: ni_nvme_send_write_cmd() failed\n", rc);
        rc = NI_RETCODE_ERROR_NVME_CMD_FAILED;
    } else
    {
        ni_log(NI_LOG_DEBUG,
               "%s() ReadFile success handle=%" PRIx64 ", event_handle="
               "%" PRIx64 ", lba=0x%x, len=%d, rc=%d\n",
               __func__, (int64_t)handle, (int64_t)event_handle, (lba << 3),
               data_len, rc);
        rc = NI_RETCODE_SUCCESS;
    }
#else
    if (handle != 0 && p_data != NULL)
    {
        if (((uintptr_t)p_data) % NI_MEM_PAGE_ALIGNMENT)
        {
            ni_log(NI_LOG_DEBUG,
                   "%s: Buffer not %d aligned = %p! Copying to aligned memory "
                   "and writing.\n",
                   __func__, NI_MEM_PAGE_ALIGNMENT, p_data);
            void *p_buf = NULL;
            if (ni_posix_memalign(&p_buf, sysconf(_SC_PAGESIZE), data_len))
            {
                ni_log(NI_LOG_ERROR,
                       "ERROR %d: %s() alloc data buffer failed\n", NI_ERRNO,
                       __func__);
                rc = NI_RETCODE_ERROR_MEM_ALOC;
            }
            else
            {
                memcpy(p_buf, p_data, data_len);
                rc = pwrite(handle, p_buf, data_len, offset);
                ni_aligned_free(p_buf);
            }
        }
        else
        {
            rc = pwrite(handle, p_data, data_len, offset);
        }
        ni_log(NI_LOG_TRACE,
               "%s: handle=%" PRIx64 ", lba=0x%x, len=%d, rc=%d\n", __func__,
               (int64_t)handle, (lba << 3), data_len, rc);
        if ((rc < 0) || (rc != data_len))
        {
            ni_log(NI_LOG_ERROR,
                   "ERROR %d: %s failed, lba=0x%x, len=%u, rc=%d, error=%d\n",
                   NI_ERRNO, __func__, (lba << 3), data_len, rc, NI_ERRNO);
            ni_parse_lba(lba);
            rc = NI_RETCODE_ERROR_NVME_CMD_FAILED;
        } else
        {
            rc = NI_RETCODE_SUCCESS;
        }
    } else
    {
        rc = NI_RETCODE_ERROR_NVME_CMD_FAILED;
    }
#endif
    return rc;
}
