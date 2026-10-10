/***********************************************************************************************************************
 * File Name    : sample_filex_levelx.c
 * Description  : Sample application running FileX with LevelX wear-leveling on Renesas RX65N Data Flash (32KB).
 ***********************************************************************************************************************/

#include <stdio.h>
#include <string.h>
#include "hardware_setup.h"
#include "sample_filex_levelx.h"
#include "fx_levelx_nor_driver.h"
#include "lx_nor_rx65n_dataflash.h"
#include "rx65n_dataflash.h"

#define DEMO_STACK_SIZE         (2048)
#define DEMO_SECTOR_BUFFER_SIZE (512)

/* ThreadX control block & stack */
static TX_THREAD demo_thread;
static ULONG     demo_thread_stack[DEMO_STACK_SIZE / sizeof(ULONG)];
static ULONG     demo_thread_counter = 0;

/* FileX media, file and sector buffer */
static FX_MEDIA  levelx_media;
static FX_FILE   test_file;
static UCHAR     media_memory[DEMO_SECTOR_BUFFER_SIZE];

/***********************************************************************************************************************
 * Function Name: main
 * Description  : System entry point. Initializes hardware clocks and starts ThreadX kernel.
 ***********************************************************************************************************************/
void main(void)
{
    /* Setup MCU clocks, CMT timer for ThreadX ticks, and peripherals */
    platform_setup();

    /* Enter ThreadX kernel */
    tx_kernel_enter();
}

/***********************************************************************************************************************
 * Function Name: tx_application_define
 * Description  : System definition. Creates the demo thread and initializes LevelX NOR Flash and FileX subsystems.
 ***********************************************************************************************************************/
void tx_application_define(void *first_unused_memory)
{
    (void)first_unused_memory;

    /* Initialize LevelX NOR flash subsystem */
    lx_nor_flash_initialize();

    /* Initialize FileX subsystem */
    fx_system_initialize();

    /* Create the main FileX + LevelX demo thread */
    tx_thread_create(&demo_thread,
                     "FileX_LevelX_Demo",
                     demo_thread_entry,
                     0,
                     demo_thread_stack,
                     DEMO_STACK_SIZE,
                     1,
                     1,
                     TX_NO_TIME_SLICE,
                     TX_AUTO_START);
}

/***********************************************************************************************************************
 * Function Name: demo_thread_entry
 * Description  : Demo thread demonstrating LevelX + FileX on RX65N Data Flash:
 *                1. Open media with LevelX NOR Flash driver
 *                2. Detect unformatted media / initial open failure
 *                3. Perform physical full-chip erase (0xFF) if unformatted or corrupted
 *                4. Format media as FAT12 via FileX
 *                5. Create test file ("CONFIG.TXT")
 *                6. Write test string with cycle count
 *                7. Flush and close
 *                8. Re-open and read back to verify data integrity
 *                9. Report progress via UART/stdout (printf)
 ***********************************************************************************************************************/
void demo_thread_entry(ULONG thread_input)
{
    UINT  status;
    ULONG actual_bytes;
    CHAR  write_buffer[128];
    CHAR  read_buffer[128];
    ULONG write_len;

    (void)thread_input;

    printf("\r\n");
    printf("===============================================================\r\n");
    printf("  Renesas RX65N FileX + LevelX NOR Flash (32KB Data Flash) Demo\r\n");
    printf("===============================================================\r\n");

    /* 1. Attempt to open media via FileX LevelX NOR Flash driver */
    printf("[Demo] Opening media via fx_levelx_nor_driver...\r\n");
    status = fx_media_open(&levelx_media,
                           "RX_LX_NOR",
                           fx_levelx_nor_driver,
                           FX_NULL,
                           media_memory,
                           sizeof(media_memory));

    /* 2. Check if media is unformatted, invalid boot record, or initial open failed */
    if (status != FX_SUCCESS)
    {
        printf("[Demo] Media open returned status 0x%02X.\r\n", status);
        printf("[Demo] Performing physical chip erase of Data Flash (32KB, 512 blocks) to ensure clean 0xFF state...\r\n");

        /* If LevelX was open, close it first before raw flash erase */
        if (g_nor_flash.lx_nor_flash_state == LX_NOR_FLASH_OPENED)
        {
            lx_nor_flash_close(&g_nor_flash);
        }

        /* Perform physical full-chip erase (32KB: all 512 x 64B hardware blocks) */
        if (rx65n_dataflash_erase_all() != RX65N_DF_SUCCESS)
        {
            printf("[Demo] ERROR: rx65n_dataflash_erase_all failed!\r\n");
            return;
        }
        printf("[Demo] Data Flash physical chip erase completed successfully.\r\n");

        printf("[Demo] Formatting Data Flash via LevelX NOR Driver as FAT12 (%lu sectors)...\r\n",
               SAMPLE_TOTAL_SECTORS);

        status = fx_media_format(&levelx_media,
                                 fx_levelx_nor_driver,
                                 FX_NULL,
                                 media_memory,
                                 sizeof(media_memory),
                                 "RX_LX_NOR",           /* Volume label */
                                 1,                     /* Number of FATs */
                                 32,                    /* Directory entries */
                                 0,                     /* Hidden sectors */
                                 SAMPLE_TOTAL_SECTORS,  /* Total sectors (32) */
                                 SAMPLE_SECTOR_SIZE,    /* Sector size (512) */
                                 1,                     /* Sectors per cluster */
                                 1,                     /* Heads */
                                 1);                    /* Sectors per track */

        if (status != FX_SUCCESS)
        {
            printf("[Demo] ERROR: fx_media_format failed with status 0x%02X!\r\n", status);
            return;
        }

        printf("[Demo] FAT12 format completed successfully.\r\n");

        /* Re-open media after formatting */
        printf("[Demo] Re-opening media after format...\r\n");
        status = fx_media_open(&levelx_media,
                               "RX_LX_NOR",
                               fx_levelx_nor_driver,
                               FX_NULL,
                               media_memory,
                               sizeof(media_memory));

        if (status != FX_SUCCESS)
        {
            printf("[Demo] ERROR: fx_media_open after format failed with status 0x%02X!\r\n", status);
            return;
        }
    }

    printf("[Demo] Media opened successfully.\r\n");

    /* 3. Create test file */
    printf("[Demo] Creating file: %s...\r\n", SAMPLE_FILE_NAME);
    status = fx_file_create(&levelx_media, SAMPLE_FILE_NAME);
    if ((status != FX_SUCCESS) && (status != FX_ALREADY_CREATED))
    {
        printf("[Demo] ERROR: fx_file_create failed with status 0x%02X!\r\n", status);
        fx_media_close(&levelx_media);
        return;
    }

    if (status == FX_ALREADY_CREATED)
    {
        printf("[Demo] File %s already exists on media.\r\n", SAMPLE_FILE_NAME);
    }
    else
    {
        printf("[Demo] File %s created successfully.\r\n", SAMPLE_FILE_NAME);
    }

    /* 4. Open test file for writing */
    printf("[Demo] Opening %s for writing...\r\n", SAMPLE_FILE_NAME);
    status = fx_file_open(&levelx_media, &test_file, SAMPLE_FILE_NAME, FX_OPEN_FOR_WRITE);
    if (status != FX_SUCCESS)
    {
        printf("[Demo] ERROR: fx_file_open for write failed with status 0x%02X!\r\n", status);
        fx_media_close(&levelx_media);
        return;
    }

    /* Seek to beginning of file */
    fx_file_seek(&test_file, 0);

    /* Prepare test string */
    demo_thread_counter++;
    snprintf(write_buffer, sizeof(write_buffer),
             "Renesas RX65N FileX + LevelX NOR Flash Test OK! [Cycle #%lu]\r\n",
             demo_thread_counter);
    write_len = (ULONG)strlen(write_buffer);

    /* Write data */
    printf("[Demo] Writing %lu bytes: %s", write_len, write_buffer);
    status = fx_file_write(&test_file, write_buffer, write_len);
    if (status != FX_SUCCESS)
    {
        printf("[Demo] ERROR: fx_file_write failed with status 0x%02X!\r\n", status);
        fx_file_close(&test_file);
        fx_media_close(&levelx_media);
        return;
    }

    /* Close file and flush media cache */
    status = fx_file_close(&test_file);
    if (status != FX_SUCCESS)
    {
        printf("[Demo] ERROR: fx_file_close failed with status 0x%02X!\r\n", status);
    }

    fx_media_flush(&levelx_media);
    printf("[Demo] File written and closed successfully.\r\n");

    /* 5. Re-open test file for reading and verify */
    printf("[Demo] Opening %s for reading...\r\n", SAMPLE_FILE_NAME);
    status = fx_file_open(&levelx_media, &test_file, SAMPLE_FILE_NAME, FX_OPEN_FOR_READ);
    if (status != FX_SUCCESS)
    {
        printf("[Demo] ERROR: fx_file_open for read failed with status 0x%02X!\r\n", status);
        fx_media_close(&levelx_media);
        return;
    }

    memset(read_buffer, 0, sizeof(read_buffer));
    status = fx_file_read(&test_file, read_buffer, write_len, &actual_bytes);
    if ((status != FX_SUCCESS) || (actual_bytes != write_len))
    {
        printf("[Demo] ERROR: fx_file_read failed (status=0x%02X, actual=%lu, expected=%lu)!\r\n",
               status, actual_bytes, write_len);
        fx_file_close(&test_file);
        fx_media_close(&levelx_media);
        return;
    }

    fx_file_close(&test_file);

    /* 6. Verify data integrity */
    printf("[Demo] Read payload (%lu bytes): %s", actual_bytes, read_buffer);
    if (memcmp(write_buffer, read_buffer, write_len) == 0)
    {
        printf("[Demo] >>> DATA VERIFICATION SUCCESS: Data matches perfectly! <<<\r\n");
    }
    else
    {
        printf("[Demo] >>> DATA VERIFICATION FAILED: Data mismatch! <<<\r\n");
    }

    /* 7. Close media */
    status = fx_media_close(&levelx_media);
    if (status == FX_SUCCESS)
    {
        printf("[Demo] Media closed successfully. Demo completed.\r\n");
    }
    else
    {
        printf("[Demo] ERROR: fx_media_close failed with status 0x%02X!\r\n", status);
    }

    printf("===============================================================\r\n\r\n");
}
