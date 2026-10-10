/***********************************************************************************************************************
 * File Name    : fx_driver_rx_dataflash.c
 * Description  : FileX I/O driver implementation for Renesas RX65N on-chip Data Flash (32KB).
 ***********************************************************************************************************************/

#include "fx_driver_rx_dataflash.h"
#include "rx65n_dataflash.h"

/***********************************************************************************************************************
 * Function Name: fx_driver_rx_dataflash
 * Description  : FileX media driver entry point for RX65N Data Flash.
 * Arguments    : media_ptr - Pointer to FileX media structure
 * Return Value : None (Status returned in media_ptr->fx_media_driver_status)
 ***********************************************************************************************************************/
VOID fx_driver_rx_dataflash(FX_MEDIA *media_ptr)
{
    ULONG  sector;
    ULONG  num_sectors;
    ULONG  flash_addr;
    ULONG  num_blocks;
    ULONG  num_bytes;
    UINT   bytes_per_sector;
    UCHAR *buffer;

    if (FX_NULL == media_ptr)
    {
        return;
    }

    switch (media_ptr->fx_media_driver_request)
    {
    case FX_DRIVER_INIT:
    {
        /* Initialize low-level Data Flash hardware */
        if (rx65n_dataflash_init() != RX65N_DF_SUCCESS)
        {
            media_ptr->fx_media_driver_status = FX_IO_ERROR;
            break;
        }

        media_ptr->fx_media_driver_free_sector_update = FX_FALSE;
        media_ptr->fx_media_driver_write_protect      = FX_FALSE;
        media_ptr->fx_media_driver_status             = FX_SUCCESS;
        break;
    }

    case FX_DRIVER_UNINIT:
    {
        media_ptr->fx_media_driver_status = FX_SUCCESS;
        break;
    }

    case FX_DRIVER_READ:
    {
        sector      = media_ptr->fx_media_driver_logical_sector;
        num_sectors = media_ptr->fx_media_driver_sectors;
        buffer      = media_ptr->fx_media_driver_buffer;

        /* Range check */
        if (((sector + num_sectors) > RX65N_DF_TOTAL_SECTORS) || (FX_NULL == buffer))
        {
            media_ptr->fx_media_driver_status = FX_IO_ERROR;
            break;
        }

        flash_addr = RX65N_DF_START_ADDR + (sector * RX65N_DF_SECTOR_SIZE);
        num_bytes  = num_sectors * RX65N_DF_SECTOR_SIZE;

        if (rx65n_dataflash_read(flash_addr, buffer, num_bytes) != RX65N_DF_SUCCESS)
        {
            media_ptr->fx_media_driver_status = FX_IO_ERROR;
            break;
        }

        media_ptr->fx_media_driver_status = FX_SUCCESS;
        break;
    }

    case FX_DRIVER_BOOT_READ:
    {
        buffer = media_ptr->fx_media_driver_buffer;
        if (FX_NULL == buffer)
        {
            media_ptr->fx_media_driver_status = FX_IO_ERROR;
            break;
        }

        flash_addr = RX65N_DF_START_ADDR;

        /* Check boot record signature in sector 0 */
        UCHAR boot_header[3];
        if (rx65n_dataflash_read(flash_addr, boot_header, 3) != RX65N_DF_SUCCESS)
        {
            media_ptr->fx_media_driver_status = FX_IO_ERROR;
            break;
        }

        /* Check for FAT standard jump instruction (0xEB ... 0x90 or 0xE9 ...) */
        if (((boot_header[0] != (UCHAR)0xEB) && (boot_header[0] != (UCHAR)0xE9)) ||
            ((boot_header[0] == (UCHAR)0xEB) && (boot_header[2] != (UCHAR)0x90) &&
             (boot_header[1] != (UCHAR)0x34) && (boot_header[1] != (UCHAR)0x76)))
        {
            /* Data Flash is unformatted or boot sector is invalid */
            media_ptr->fx_media_driver_status = FX_MEDIA_INVALID;
            break;
        }

        /* Read bytes per sector field (offset FX_BYTES_SECTOR = 11) */
        UCHAR bpb_bytes[2];
        rx65n_dataflash_read(flash_addr + FX_BYTES_SECTOR, bpb_bytes, 2);
        bytes_per_sector = (UINT)bpb_bytes[0] | ((UINT)bpb_bytes[1] << 8);

        if ((bytes_per_sector == 0) || (bytes_per_sector > media_ptr->fx_media_memory_size))
        {
            media_ptr->fx_media_driver_status = FX_BUFFER_ERROR;
            break;
        }

        /* Copy boot sector to FileX buffer */
        if (rx65n_dataflash_read(flash_addr, buffer, bytes_per_sector) != RX65N_DF_SUCCESS)
        {
            media_ptr->fx_media_driver_status = FX_IO_ERROR;
            break;
        }

        media_ptr->fx_media_driver_status = FX_SUCCESS;
        break;
    }

    case FX_DRIVER_WRITE:
    {
        sector      = media_ptr->fx_media_driver_logical_sector;
        num_sectors = media_ptr->fx_media_driver_sectors;
        buffer      = media_ptr->fx_media_driver_buffer;

        /* Range check */
        if (((sector + num_sectors) > RX65N_DF_TOTAL_SECTORS) || (FX_NULL == buffer))
        {
            media_ptr->fx_media_driver_status = FX_IO_ERROR;
            break;
        }

        flash_addr = RX65N_DF_START_ADDR + (sector * RX65N_DF_SECTOR_SIZE);
        num_blocks = num_sectors * RX65N_DF_BLOCKS_PER_SECTOR;
        num_bytes  = num_sectors * RX65N_DF_SECTOR_SIZE;

        /* Erase target 64-byte blocks (8 blocks per 512B sector) */
        if (rx65n_dataflash_erase(flash_addr, num_blocks) != RX65N_DF_SUCCESS)
        {
            media_ptr->fx_media_driver_status = FX_IO_ERROR;
            break;
        }

        /* Program data to Flash */
        if (rx65n_dataflash_write(buffer, flash_addr, num_bytes) != RX65N_DF_SUCCESS)
        {
            media_ptr->fx_media_driver_status = FX_IO_ERROR;
            break;
        }

        media_ptr->fx_media_driver_status = FX_SUCCESS;
        break;
    }

    case FX_DRIVER_BOOT_WRITE:
    {
        buffer = media_ptr->fx_media_driver_buffer;
        if (FX_NULL == buffer)
        {
            media_ptr->fx_media_driver_status = FX_IO_ERROR;
            break;
        }

        bytes_per_sector = media_ptr->fx_media_bytes_per_sector;
        if (bytes_per_sector == 0)
        {
            bytes_per_sector = RX65N_DF_SECTOR_SIZE;
        }

        flash_addr = RX65N_DF_START_ADDR;
        num_blocks = RX65N_DF_BLOCKS_PER_SECTOR; /* Sector 0 = 8 blocks = 512 bytes */

        /* Erase sector 0 */
        if (rx65n_dataflash_erase(flash_addr, num_blocks) != RX65N_DF_SUCCESS)
        {
            media_ptr->fx_media_driver_status = FX_IO_ERROR;
            break;
        }

        /* Write boot sector */
        if (rx65n_dataflash_write(buffer, flash_addr, bytes_per_sector) != RX65N_DF_SUCCESS)
        {
            media_ptr->fx_media_driver_status = FX_IO_ERROR;
            break;
        }

        media_ptr->fx_media_driver_status = FX_SUCCESS;
        break;
    }

    case FX_DRIVER_FLUSH:
    {
        media_ptr->fx_media_driver_status = FX_SUCCESS;
        break;
    }

    case FX_DRIVER_ABORT:
    {
        media_ptr->fx_media_driver_status = FX_SUCCESS;
        break;
    }

    default:
    {
        media_ptr->fx_media_driver_status = FX_IO_ERROR;
        break;
    }
    }
}
