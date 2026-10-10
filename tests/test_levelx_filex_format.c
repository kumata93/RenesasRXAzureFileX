/***********************************************************************************************************************
 * File Name    : test_levelx_filex_format.c
 * Description  : Unit tests reproducing and resolving 0x90 format error via LevelX NOR & FileX FAT12.
 ***********************************************************************************************************************/

#include "unity.h"
#include "tx_api.h"
#include "lx_api.h"
#include "fx_api.h"
#include "sim_dataflash.h"
#include <string.h>

/* LevelX NOR and FileX instance variables */
static LX_NOR_FLASH g_test_nor_flash;
static FX_MEDIA     g_test_filex_media;
static FX_FILE      g_test_file;
static UCHAR        g_media_memory[512];
static ULONG        g_nor_sector_buffer[LX_NOR_SECTOR_SIZE];

/* Simulated LevelX geometry configuration (matches rx65n_dataflash: 4KB block x 8 blocks) */
#define TEST_LX_BLOCK_SIZE          (4096UL)
#define TEST_LX_TOTAL_BLOCKS        (SIM_DF_SIZE / TEST_LX_BLOCK_SIZE) /* 8 */
#define TEST_LX_WORDS_PER_BLOCK     (TEST_LX_BLOCK_SIZE / sizeof(ULONG)) /* 1024 */
#define TEST_LX_HW_BLOCKS_PER_BLOCK (TEST_LX_BLOCK_SIZE / SIM_DF_BLOCK_SIZE) /* 64 */

/* LevelX low-level callbacks using sim_dataflash */
static UINT test_nor_driver_read(ULONG *flash_address, ULONG *destination, ULONG words)
{
    uint32_t addr  = (uint32_t)(uintptr_t)flash_address;
    uint32_t bytes = words * sizeof(ULONG);
    if (sim_dataflash_read(addr, (uint8_t *)destination, bytes) != SIM_DF_SUCCESS)
    {
        return LX_ERROR;
    }
    return LX_SUCCESS;
}

static UINT test_nor_driver_write(ULONG *flash_address, ULONG *source, ULONG words)
{
    uint32_t addr  = (uint32_t)(uintptr_t)flash_address;
    uint32_t bytes = words * sizeof(ULONG);
    if (sim_dataflash_write((const uint8_t *)source, addr, bytes) != SIM_DF_SUCCESS)
    {
        return LX_ERROR;
    }
    return LX_SUCCESS;
}

static UINT test_nor_driver_block_erase(ULONG block, ULONG erase_count)
{
    (void)erase_count;
    if (block >= TEST_LX_TOTAL_BLOCKS)
    {
        return LX_ERROR;
    }
    uint32_t start_addr = SIM_DF_START_ADDR + (block * TEST_LX_BLOCK_SIZE);
    if (sim_dataflash_erase(start_addr, TEST_LX_HW_BLOCKS_PER_BLOCK) != SIM_DF_SUCCESS)
    {
        return LX_ERROR;
    }
    return LX_SUCCESS;
}

static UINT test_nor_driver_block_erased_verify(ULONG block)
{
    if (block >= TEST_LX_TOTAL_BLOCKS)
    {
        return LX_ERROR;
    }
    uint32_t offset = block * TEST_LX_BLOCK_SIZE;
    const uint32_t *word_ptr = (const uint32_t *)(uintptr_t)&g_sim_dataflash[offset];
    ULONG words = TEST_LX_WORDS_PER_BLOCK;
    while (words--)
    {
        if (*word_ptr++ != 0xFFFFFFFFUL)
        {
            return LX_ERROR;
        }
    }
    return LX_SUCCESS;
}

static UINT test_nor_driver_system_error(UINT error_code)
{
    (void)error_code;
    return LX_ERROR;
}

static UINT test_nor_driver_initialize(LX_NOR_FLASH *nor_flash)
{
    nor_flash->lx_nor_flash_base_address               = (ULONG *)(uintptr_t)SIM_DF_START_ADDR;
    nor_flash->lx_nor_flash_total_blocks               = TEST_LX_TOTAL_BLOCKS;
    nor_flash->lx_nor_flash_words_per_block            = TEST_LX_WORDS_PER_BLOCK;

    nor_flash->lx_nor_flash_driver_read                = test_nor_driver_read;
    nor_flash->lx_nor_flash_driver_write               = test_nor_driver_write;
    nor_flash->lx_nor_flash_driver_block_erase         = test_nor_driver_block_erase;
    nor_flash->lx_nor_flash_driver_block_erased_verify = test_nor_driver_block_erased_verify;
    nor_flash->lx_nor_flash_driver_system_error        = test_nor_driver_system_error;

    nor_flash->lx_nor_flash_sector_buffer              = &g_nor_sector_buffer[0];
    return LX_SUCCESS;
}

/* FileX - LevelX adapter function */
static VOID test_filex_levelx_driver(FX_MEDIA *media_ptr)
{
    UCHAR *source_buffer;
    UCHAR *destination_buffer;
    ULONG  logical_sector;
    ULONG  i;
    UINT   status;

    if (media_ptr == FX_NULL) return;

    switch (media_ptr->fx_media_driver_request)
    {
        case FX_DRIVER_READ:
        {
            logical_sector     = media_ptr->fx_media_driver_logical_sector;
            destination_buffer = (UCHAR *)media_ptr->fx_media_driver_buffer;

            for (i = 0; i < media_ptr->fx_media_driver_sectors; i++)
            {
                status = lx_nor_flash_sector_read(&g_test_nor_flash, logical_sector, destination_buffer);
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
                status = lx_nor_flash_sector_write(&g_test_nor_flash, logical_sector, source_buffer);
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
                status = lx_nor_flash_sector_release(&g_test_nor_flash, logical_sector);
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
        case FX_DRIVER_ABORT:
        {
            media_ptr->fx_media_driver_status = FX_SUCCESS;
            break;
        }

        case FX_DRIVER_INIT:
        {
            media_ptr->fx_media_driver_free_sector_update = FX_TRUE;

            if (g_test_nor_flash.lx_nor_flash_state != LX_NOR_FLASH_OPENED)
            {
                status = lx_nor_flash_open(&g_test_nor_flash, "SIM_NOR_FLASH", test_nor_driver_initialize);
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
            if (g_test_nor_flash.lx_nor_flash_state == LX_NOR_FLASH_OPENED)
            {
                status = lx_nor_flash_close(&g_test_nor_flash);
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
            status = lx_nor_flash_sector_read(&g_test_nor_flash, 0, destination_buffer);
            if (status != LX_SUCCESS)
            {
                media_ptr->fx_media_driver_status = FX_MEDIA_INVALID;
                return;
            }

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
            status = lx_nor_flash_sector_write(&g_test_nor_flash, 0, source_buffer);
            if (status != LX_SUCCESS)
            {
                media_ptr->fx_media_driver_status = FX_IO_ERROR;
                return;
            }
            media_ptr->fx_media_driver_status = FX_SUCCESS;
            break;
        }

        default:
            media_ptr->fx_media_driver_status = FX_IO_ERROR;
            break;
    }
}

void setUp(void)
{
    lx_nor_flash_initialize();
    fx_system_initialize();
}

void tearDown(void)
{
    if (g_test_nor_flash.lx_nor_flash_state == LX_NOR_FLASH_OPENED)
    {
        lx_nor_flash_close(&g_test_nor_flash);
    }
}

/* Test 1: Reproduce 0x90 when flash contains dirty/garbage data */
void test_reproduce_0x90_on_unformatted_dirty_flash(void)
{
    /* Initialize simulated flash with dirty non-0xFF data */
    sim_dataflash_init_dirty();

    /* Attempt to open LevelX directly -> Should fail because block header metadata is invalid */
    UINT status = lx_nor_flash_open(&g_test_nor_flash, "TEST_NOR", test_nor_driver_initialize);
    TEST_ASSERT_NOT_EQUAL(LX_SUCCESS, status);

    /* Attempt to format FileX while LevelX cannot open -> fx_media_format returns 0x90 (FX_IO_ERROR) */
    UINT fx_status = fx_media_format(&g_test_filex_media,
                                     test_filex_levelx_driver,
                                     FX_NULL,
                                     g_media_memory,
                                     sizeof(g_media_memory),
                                     "TEST_VOL",
                                     1, 32, 0, 32, 512, 1, 1, 1);
    TEST_ASSERT_EQUAL_HEX8(FX_IO_ERROR, fx_status); /* 0x90 reproduced! */
}

/* Test 2: Full chip erase resolves 0x90 and enables successful format and open */
void test_chip_erase_resolves_0x90_and_formats_successfully(void)
{
    /* 1. Simulate starting from dirty flash */
    sim_dataflash_init_dirty();

    /* 2. Execute physical full-chip erase (as added in rx65n_dataflash_erase_all) */
    int erase_res = sim_dataflash_erase_all();
    TEST_ASSERT_EQUAL_INT(SIM_DF_SUCCESS, erase_res);

    /* 3. Now LevelX open succeeds because all blocks are clean 0xFF */
    UINT lx_status = lx_nor_flash_open(&g_test_nor_flash, "TEST_NOR", test_nor_driver_initialize);
    TEST_ASSERT_EQUAL_INT(LX_SUCCESS, lx_status);

    /* 4. Format media with FAT12 via FileX */
    UINT fx_format_status = fx_media_format(&g_test_filex_media,
                                            test_filex_levelx_driver,
                                            FX_NULL,
                                            g_media_memory,
                                            sizeof(g_media_memory),
                                            "RX_LX_NOR",
                                            1, 32, 0, 32, 512, 1, 1, 1);
    TEST_ASSERT_EQUAL_HEX8(FX_SUCCESS, fx_format_status);

    /* 5. Open media via FileX */
    UINT fx_open_status = fx_media_open(&g_test_filex_media,
                                        "RX_LX_NOR",
                                        test_filex_levelx_driver,
                                        FX_NULL,
                                        g_media_memory,
                                        sizeof(g_media_memory));
    TEST_ASSERT_EQUAL_HEX8(FX_SUCCESS, fx_open_status);

    /* Clean close */
    fx_media_close(&g_test_filex_media);
}

/* Test 3: Full File I/O verification (create, write, close, re-open, read, verify) */
void test_file_create_write_read_verify(void)
{
    sim_dataflash_init_clean();

    /* Format and open media */
    UINT st = fx_media_format(&g_test_filex_media, test_filex_levelx_driver, FX_NULL,
                              g_media_memory, sizeof(g_media_memory),
                              "RX_LX_NOR", 1, 32, 0, 32, 512, 1, 1, 1);
    TEST_ASSERT_EQUAL_HEX8(FX_SUCCESS, st);

    st = fx_media_open(&g_test_filex_media, "RX_LX_NOR", test_filex_levelx_driver,
                       FX_NULL, g_media_memory, sizeof(g_media_memory));
    TEST_ASSERT_EQUAL_HEX8(FX_SUCCESS, st);

    /* Create test file */
    const char *filename = "CONFIG.TXT";
    st = fx_file_create(&g_test_filex_media, (CHAR *)filename);
    TEST_ASSERT_EQUAL_HEX8(FX_SUCCESS, st);

    /* Open for write */
    st = fx_file_open(&g_test_filex_media, &g_test_file, (CHAR *)filename, FX_OPEN_FOR_WRITE);
    TEST_ASSERT_EQUAL_HEX8(FX_SUCCESS, st);

    const char *test_payload = "Renesas RX65N FileX + LevelX NOR Flash Test OK! [Cycle #1]";
    ULONG payload_len = (ULONG)strlen(test_payload);

    st = fx_file_write(&g_test_file, (VOID *)test_payload, payload_len);
    TEST_ASSERT_EQUAL_HEX8(FX_SUCCESS, st);

    st = fx_file_close(&g_test_file);
    TEST_ASSERT_EQUAL_HEX8(FX_SUCCESS, st);

    fx_media_flush(&g_test_filex_media);

    /* Re-open for read */
    st = fx_file_open(&g_test_filex_media, &g_test_file, (CHAR *)filename, FX_OPEN_FOR_READ);
    TEST_ASSERT_EQUAL_HEX8(FX_SUCCESS, st);

    CHAR read_buf[128] = {0};
    ULONG actual_bytes = 0;
    st = fx_file_read(&g_test_file, read_buf, payload_len, &actual_bytes);
    TEST_ASSERT_EQUAL_HEX8(FX_SUCCESS, st);
    TEST_ASSERT_EQUAL_UINT32(payload_len, actual_bytes);
    TEST_ASSERT_EQUAL_STRING(test_payload, read_buf);

    fx_file_close(&g_test_file);
    fx_media_close(&g_test_filex_media);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_reproduce_0x90_on_unformatted_dirty_flash);
    RUN_TEST(test_chip_erase_resolves_0x90_and_formats_successfully);
    RUN_TEST(test_file_create_write_read_verify);
    return UNITY_END();
}
