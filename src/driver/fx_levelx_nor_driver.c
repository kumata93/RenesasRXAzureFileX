/***********************************************************************************************************************
 * File Name    : fx_levelx_nor_driver.c
 * Description  : FileX LevelX NOR Flash adapter driver for RX65N Data Flash.
 ***********************************************************************************************************************/

#include "fx_levelx_nor_driver.h"
#include "lx_nor_rx65n_dataflash.h"
#include <stdio.h>

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
                    printf("  [Driver] FX_DRIVER_READ failed: sector %lu, lx_nor_flash_sector_read=0x%02X\r\n",
                           (unsigned long)logical_sector, status);
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

            printf("  [Driver] FX_DRIVER_WRITE: start_sec=%lu, count=%lu\r\n",
                   (unsigned long)logical_sector, (unsigned long)media_ptr->fx_media_driver_sectors);

            for (i = 0; i < media_ptr->fx_media_driver_sectors; i++)
            {
                status = lx_nor_flash_sector_write(&g_nor_flash, logical_sector, source_buffer);
                if (status != LX_SUCCESS)
                {
                    printf("  [Driver] FX_DRIVER_WRITE failed: sector %lu, lx_nor_flash_sector_write=0x%02X\r\n",
                           (unsigned long)logical_sector, status);
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
                    printf("  [Driver] FX_DRIVER_RELEASE_SECTORS failed: sector %lu, status=0x%02X\r\n",
                           (unsigned long)logical_sector, status);
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
            printf("  [Driver] FX_DRIVER_INIT called...\r\n");
            media_ptr->fx_media_driver_free_sector_update = FX_TRUE;

            /* Check if LevelX NOR flash is already opened */
            if (g_nor_flash.lx_nor_flash_state != LX_NOR_FLASH_OPENED)
            {
                printf("  [Driver] Opening LevelX NOR flash instance...\r\n");
                status = lx_nor_flash_open(&g_nor_flash, "RX65N_NOR_FLASH", lx_nor_rx65n_dataflash_initialize);
                printf("  [Driver] lx_nor_flash_open returned: 0x%02X (state=0x%08lX)\r\n",
                       status, (unsigned long)g_nor_flash.lx_nor_flash_state);
                if (status != LX_SUCCESS)
                {
                    printf("  [Driver] ERROR: lx_nor_flash_open failed in FX_DRIVER_INIT! (status=0x%02X)\r\n", status);
                    media_ptr->fx_media_driver_status = FX_IO_ERROR;
                    return;
                }
            }
            else
            {
                printf("  [Driver] LevelX NOR flash is already opened (state=LX_NOR_FLASH_OPENED).\r\n");
            }

            media_ptr->fx_media_driver_status = FX_SUCCESS;
            break;
        }

        case FX_DRIVER_UNINIT:
        {
            printf("  [Driver] FX_DRIVER_UNINIT called...\r\n");
            if (g_nor_flash.lx_nor_flash_state == LX_NOR_FLASH_OPENED)
            {
                status = lx_nor_flash_close(&g_nor_flash);
                printf("  [Driver] lx_nor_flash_close returned: 0x%02X\r\n", status);
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
                printf("  [Driver] FX_DRIVER_BOOT_READ: sector 0 read returned 0x%02X -> return FX_MEDIA_INVALID\r\n",
                       status);
                media_ptr->fx_media_driver_status = FX_MEDIA_INVALID;
                return;
            }

            /* Validate standard FAT boot sector signatures (0xEB ... or 0xE9 ...) */
            if (!(((destination_buffer[0] == 0xEB) && (destination_buffer[2] == 0x90)) ||
                  (destination_buffer[0] == 0xE9)) ||
                (destination_buffer[510] != 0x55) || (destination_buffer[511] != 0xAA))
            {
                printf("  [Driver] FX_DRIVER_BOOT_READ: invalid boot signature (sig[0]=0x%02X, [510]=0x%02X, [511]=0x%02X) -> FX_MEDIA_INVALID\r\n",
                       destination_buffer[0], destination_buffer[510], destination_buffer[511]);
                media_ptr->fx_media_driver_status = FX_MEDIA_INVALID;
                return;
            }

            printf("  [Driver] FX_DRIVER_BOOT_READ: Valid boot sector found.\r\n");
            media_ptr->fx_media_driver_status = FX_SUCCESS;
            break;
        }

        case FX_DRIVER_BOOT_WRITE:
        {
            printf("  [Driver] FX_DRIVER_BOOT_WRITE called for sector 0...\r\n");
            source_buffer = (UCHAR *)media_ptr->fx_media_driver_buffer;

            status = lx_nor_flash_sector_write(&g_nor_flash, 0, source_buffer);
            printf("  [Driver] lx_nor_flash_sector_write for boot sector 0 returned: 0x%02X\r\n", status);
            if (status != LX_SUCCESS)
            {
                printf("  [Driver] ERROR: lx_nor_flash_sector_write failed in FX_DRIVER_BOOT_WRITE (status=0x%02X)\r\n",
                       status);
                media_ptr->fx_media_driver_status = FX_IO_ERROR;
                return;
            }

            media_ptr->fx_media_driver_status = FX_SUCCESS;
            break;
        }

        default:
        {
            printf("  [Driver] ERROR: Unknown driver request: %u\r\n", (unsigned int)media_ptr->fx_media_driver_request);
            media_ptr->fx_media_driver_status = FX_IO_ERROR;
            break;
        }
    }
}
