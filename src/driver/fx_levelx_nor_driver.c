/***********************************************************************************************************************
 * File Name    : fx_levelx_nor_driver.c
 * Description  : FileX LevelX NOR Flash adapter driver for RX65N Data Flash.
 ***********************************************************************************************************************/

#include "fx_levelx_nor_driver.h"
#include "lx_nor_rx65n_dataflash.h"

/* Global LevelX NOR Flash instance */
LX_NOR_FLASH g_nor_flash;

/**
 * @brief FileX device driver entry point for LevelX NOR Flash.
 * @param media_ptr Pointer to FileX media control block.
 */
VOID fx_levelx_nor_driver(FX_MEDIA *media_ptr)
{
    UCHAR   *source_buffer;
    UCHAR   *destination_buffer;
    ULONG   logical_sector;
    ULONG   i;
    UINT    status;

    if (media_ptr == FX_NULL)
    {
        return;
    }

    switch (media_ptr->fx_media_driver_request)
    {
        case FX_DRIVER_READ:
        {
            logical_sector     = media_ptr->fx_media_driver_logical_sector;
            destination_buffer = (UCHAR *)media_ptr->fx_media_driver_buffer;

            for (i = 0; i < media_ptr->fx_media_driver_sectors; i++)
            {
                status = lx_nor_flash_sector_read(&g_nor_flash, logical_sector, destination_buffer);
                if (status != LX_SUCCESS)
                {
                    media_ptr->fx_media_driver_status = FX_IO_ERROR;
                    return;
                }
                logical_sector++;
                destination_buffer += 512;
            }

            media_ptr->fx_media_driver_status = FX_SUCCESS;
            break;
        }

        case FX_DRIVER_WRITE:
        {
            logical_sector = media_ptr->fx_media_driver_logical_sector;
            source_buffer  = (UCHAR *)media_ptr->fx_media_driver_buffer;

            for (i = 0; i < media_ptr->fx_media_driver_sectors; i++)
            {
                status = lx_nor_flash_sector_write(&g_nor_flash, logical_sector, source_buffer);
                if (status != LX_SUCCESS)
                {
                    media_ptr->fx_media_driver_status = FX_IO_ERROR;
                    return;
                }
                logical_sector++;
                source_buffer += 512;
            }

            media_ptr->fx_media_driver_status = FX_SUCCESS;
            break;
        }

        case FX_DRIVER_RELEASE_SECTORS:
        {
            logical_sector = media_ptr->fx_media_driver_logical_sector;

            for (i = 0; i < media_ptr->fx_media_driver_sectors; i++)
            {
                status = lx_nor_flash_sector_release(&g_nor_flash, logical_sector);
                if (status != LX_SUCCESS)
                {
                    media_ptr->fx_media_driver_status = FX_IO_ERROR;
                    return;
                }
                logical_sector++;
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

        case FX_DRIVER_INIT:
        {
            media_ptr->fx_media_driver_free_sector_update = FX_TRUE;

            /* Check if LevelX NOR flash is already opened */
            if (g_nor_flash.lx_nor_flash_state != LX_NOR_FLASH_OPENED)
            {
                status = lx_nor_flash_open(&g_nor_flash, "RX65N_NOR_FLASH", lx_nor_rx65n_dataflash_initialize);
                if (status != LX_SUCCESS)
                {
                    media_ptr->fx_media_driver_status = FX_IO_ERROR;
                    return;
                }
            }

            media_ptr->fx_media_driver_status = FX_SUCCESS;
            break;
        }

        case FX_DRIVER_UNINIT:
        {
            if (g_nor_flash.lx_nor_flash_state == LX_NOR_FLASH_OPENED)
            {
                status = lx_nor_flash_close(&g_nor_flash);
                if (status != LX_SUCCESS)
                {
                    media_ptr->fx_media_driver_status = FX_IO_ERROR;
                    return;
                }
            }

            media_ptr->fx_media_driver_status = FX_SUCCESS;
            break;
        }

        case FX_DRIVER_BOOT_READ:
        {
            destination_buffer = (UCHAR *)media_ptr->fx_media_driver_buffer;

            status = lx_nor_flash_sector_read(&g_nor_flash, 0, destination_buffer);
            if (status != LX_SUCCESS)
            {
                /* When sector 0 is not yet written/mapped in LevelX, return FX_MEDIA_INVALID so FileX knows format is needed */
                media_ptr->fx_media_driver_status = FX_MEDIA_INVALID;
                return;
            }

            /* Validate standard FAT boot sector signatures (0xEB ... or 0xE9 ...) */
            if (!(((destination_buffer[0] == 0xEB) && (destination_buffer[2] == 0x90)) ||
                  (destination_buffer[0] == 0xE9)) ||
                (destination_buffer[510] != 0x55) || (destination_buffer[511] != 0xAA))
            {
                media_ptr->fx_media_driver_status = FX_MEDIA_INVALID;
                return;
            }

            media_ptr->fx_media_driver_status = FX_SUCCESS;
            break;
        }

        case FX_DRIVER_BOOT_WRITE:
        {
            source_buffer = (UCHAR *)media_ptr->fx_media_driver_buffer;

            status = lx_nor_flash_sector_write(&g_nor_flash, 0, source_buffer);
            if (status != LX_SUCCESS)
            {
                media_ptr->fx_media_driver_status = FX_IO_ERROR;
                return;
            }

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
