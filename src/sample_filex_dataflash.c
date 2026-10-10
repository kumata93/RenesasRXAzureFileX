/***********************************************************************************************************************
 * File Name    : sample_filex_dataflash.c
 * Description  : Demo sample application for FileX on Renesas RX65N on-chip Data Flash (32KB).
 ***********************************************************************************************************************/

#include <stdio.h>
#include <string.h>
#include "fx_api.h"
#include "tx_api.h"
#include "fx_driver_rx_dataflash.h"
#include "rx65n_dataflash.h"

#define DEMO_STACK_SIZE         (4096)
#define TEST_FILE_NAME          "CONFIG.DAT"

/* Buffer for FileX FX_MEDIA sector cache (512 bytes for 1 sector) */
static unsigned char media_memory[RX65N_DF_SECTOR_SIZE];

/* FileX global structures */
static FX_MEDIA dataflash_media;
static FX_FILE  test_file;

/* ThreadX global structures */
static TX_THREAD demo_thread;
static ULONG     demo_thread_counter = 0;

/* External platform setup prototype */
void platform_setup(void);

/* Demo thread entry prototype */
void demo_thread_entry(ULONG thread_input);

/***********************************************************************************************************************
 * Function Name: main
 * Description  : Main entry point. Initializes platform hardware and starts ThreadX kernel.
 ***********************************************************************************************************************/
void main(void)
{
    /* Setup MCU clocks, CMT timer for ThreadX ticks, and peripherals */
    platform_setup();

    /* Enter the ThreadX kernel */
    tx_kernel_enter();
}

/***********************************************************************************************************************
 * Function Name: tx_application_define
 * Description  : Application system definition. Creates threads and initializes FileX system.
 ***********************************************************************************************************************/
void tx_application_define(void *first_unused_memory)
{
    CHAR *pointer = (CHAR *)first_unused_memory;

    /* Create the main demo thread */
    tx_thread_create(&demo_thread,
                     "FileX DataFlash Demo",
                     demo_thread_entry,
                     0,
                     pointer,
                     DEMO_STACK_SIZE,
                     1,
                     1,
                     TX_NO_TIME_SLICE,
                     TX_AUTO_START);

    /* Initialize FileX system */
    fx_system_initialize();
}

/***********************************************************************************************************************
 * Function Name: demo_thread_entry
 * Description  : Thread executing FileX Data Flash format, open, write, read, and verification test.
 ***********************************************************************************************************************/
void demo_thread_entry(ULONG thread_input)
{
    UINT  status;
    ULONG actual_bytes;
    CHAR  write_buffer[128];
    CHAR  read_buffer[128];

    (void)thread_input;

    printf("\r\n");
    printf("====================================================\r\n");
    printf("  Renesas RX65N FileX Data Flash (32KB) Port Demo   \r\n");
    printf("====================================================\r\n");

    /* 1. Attempt to open Data Flash media */
    printf("[FileX] Opening Data Flash media (32KB, 512B sectors)...\r\n");
    status = fx_media_open(&dataflash_media,
                           "RX_DFLASH",
                           fx_driver_rx_dataflash,
                           FX_NULL,
                           media_memory,
                           sizeof(media_memory));

    /* 2. Check if media is already formatted; if not, format with FAT12 */
    if (status != FX_SUCCESS)
    {
        printf("[FileX] Media open failed (status=0x%02X). Unformatted or invalid boot record.\r\n", status);
        printf("[FileX] Formatting Data Flash as FAT12 (64 sectors x 512B = 32KB)...\r\n");

        status = fx_media_format(&dataflash_media,
                                 fx_driver_rx_dataflash,
                                 FX_NULL,
                                 media_memory,
                                 sizeof(media_memory),
                                 "RX_DFLASH",                /* Volume label */
                                 1,                          /* Number of FATs */
                                 32,                         /* Directory entries */
                                 0,                          /* Hidden sectors */
                                 RX65N_DF_TOTAL_SECTORS,     /* Total sectors: 64 */
                                 RX65N_DF_SECTOR_SIZE,       /* Sector size: 512 bytes */
                                 1,                          /* Sectors per cluster */
                                 1,                          /* Number of heads */
                                 1);                         /* Sectors per track */

        if (status != FX_SUCCESS)
        {
            printf("[FileX] ERROR: fx_media_format failed with status 0x%02X\r\n", status);
            return;
        }

        printf("[FileX] FAT12 Format completed successfully.\r\n");

        /* Re-open media after formatting */
        printf("[FileX] Re-opening Data Flash media...\r\n");
        status = fx_media_open(&dataflash_media,
                               "RX_DFLASH",
                               fx_driver_rx_dataflash,
                               FX_NULL,
                               media_memory,
                               sizeof(media_memory));

        if (status != FX_SUCCESS)
        {
            printf("[FileX] ERROR: fx_media_open after format failed with status 0x%02X\r\n", status);
            return;
        }
    }

    printf("[FileX] Data Flash media opened successfully.\r\n");

    /* 3. Create test file */
    printf("[FileX] Creating test file: %s...\r\n", TEST_FILE_NAME);
    status = fx_file_create(&dataflash_media, TEST_FILE_NAME);
    if ((status != FX_SUCCESS) && (status != FX_ALREADY_CREATED))
    {
        printf("[FileX] ERROR: fx_file_create failed with status 0x%02X\r\n", status);
        fx_media_close(&dataflash_media);
        return;
    }

    if (status == FX_ALREADY_CREATED)
    {
        printf("[FileX] %s already exists on media.\r\n", TEST_FILE_NAME);
    }
    else
    {
        printf("[FileX] %s created successfully.\r\n", TEST_FILE_NAME);
    }

    /* 4. Open test file for writing */
    printf("[FileX] Opening %s for writing...\r\n", TEST_FILE_NAME);
    status = fx_file_open(&dataflash_media, &test_file, TEST_FILE_NAME, FX_OPEN_FOR_WRITE);
    if (status != FX_SUCCESS)
    {
        printf("[FileX] ERROR: fx_file_open for write failed with status 0x%02X\r\n", status);
        fx_media_close(&dataflash_media);
        return;
    }

    /* Seek to beginning */
    fx_file_seek(&test_file, 0);

    /* Prepare test content */
    demo_thread_counter++;
    snprintf(write_buffer, sizeof(write_buffer),
             "Renesas RX65N FileX Data Flash Port Test OK! [Cycle #%lu]\r\n",
             demo_thread_counter);
    ULONG write_len = (ULONG)strlen(write_buffer);

    /* Write data */
    printf("[FileX] Writing payload (%lu bytes): %s", write_len, write_buffer);
    status = fx_file_write(&test_file, write_buffer, write_len);
    if (status != FX_SUCCESS)
    {
        printf("[FileX] ERROR: fx_file_write failed with status 0x%02X\r\n", status);
        fx_file_close(&test_file);
        fx_media_close(&dataflash_media);
        return;
    }

    /* Close file and flush media cache */
    status = fx_file_close(&test_file);
    if (status != FX_SUCCESS)
    {
        printf("[FileX] ERROR: fx_file_close failed with status 0x%02X\r\n", status);
    }

    fx_media_flush(&dataflash_media);
    printf("[FileX] File written and closed successfully.\r\n");

    /* 5. Re-open test file for reading and verify */
    printf("[FileX] Opening %s for reading...\r\n", TEST_FILE_NAME);
    status = fx_file_open(&dataflash_media, &test_file, TEST_FILE_NAME, FX_OPEN_FOR_READ);
    if (status != FX_SUCCESS)
    {
        printf("[FileX] ERROR: fx_file_open for read failed with status 0x%02X\r\n", status);
        fx_media_close(&dataflash_media);
        return;
    }

    memset(read_buffer, 0, sizeof(read_buffer));
    status = fx_file_read(&test_file, read_buffer, write_len, &actual_bytes);
    if ((status != FX_SUCCESS) || (actual_bytes != write_len))
    {
        printf("[FileX] ERROR: fx_file_read failed (status=0x%02X, actual=%lu, expected=%lu)\r\n",
               status, actual_bytes, write_len);
        fx_file_close(&test_file);
        fx_media_close(&dataflash_media);
        return;
    }

    fx_file_close(&test_file);

    /* 6. Verify data integrity */
    printf("[FileX] Read payload (%lu bytes): %s", actual_bytes, read_buffer);
    if (memcmp(write_buffer, read_buffer, write_len) == 0)
    {
        printf("[FileX] >>> DATA VERIFICATION SUCCESS: Data matches perfectly! <<<\r\n");
    }
    else
    {
        printf("[FileX] >>> DATA VERIFICATION FAILED: Data mismatch! <<<\r\n");
    }

    /* 7. Close media */
    status = fx_media_close(&dataflash_media);
    if (status == FX_SUCCESS)
    {
        printf("[FileX] Media closed successfully. Demo completed.\r\n");
    }
    else
    {
        printf("[FileX] ERROR: fx_media_close failed with status 0x%02X\r\n", status);
    }

    printf("====================================================\r\n\r\n");
}
