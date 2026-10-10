/***********************************************************************************************************************
 * File Name    : test_dataflash_sim.c
 * Description  : Unit tests for 32KB simulated Data Flash and block aggregation logic.
 ***********************************************************************************************************************/

#include "unity.h"
#include "sim_dataflash.h"
#include <string.h>

void setUp(void)
{
    sim_dataflash_init_clean();
}

void tearDown(void)
{
}

/* Test 1: Clean initialization produces 0xFF across all 32KB */
void test_dataflash_init_clean_state(void)
{
    for (size_t i = 0; i < SIM_DF_SIZE; i++)
    {
        TEST_ASSERT_EQUAL_HEX8(0xFF, g_sim_dataflash[i]);
    }
}

/* Test 2: Single 64-byte physical block erase and write */
void test_dataflash_single_block_erase_and_write(void)
{
    uint32_t test_addr = SIM_DF_START_ADDR + 128; /* Block 2 */
    uint8_t payload[16] = {0x12, 0x34, 0x56, 0x78, 0x9A, 0xBC, 0xDE, 0xF0,
                           0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88};
    uint8_t readback[16] = {0};

    /* Write 16 bytes */
    int res = sim_dataflash_write(payload, test_addr, sizeof(payload));
    TEST_ASSERT_EQUAL_INT(SIM_DF_SUCCESS, res);

    /* Read back and verify */
    res = sim_dataflash_read(test_addr, readback, sizeof(readback));
    TEST_ASSERT_EQUAL_INT(SIM_DF_SUCCESS, res);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(payload, readback, sizeof(payload));

    /* Erase block */
    res = sim_dataflash_erase_block(test_addr);
    TEST_ASSERT_EQUAL_INT(SIM_DF_SUCCESS, res);

    /* Verify all 64 bytes in block are back to 0xFF */
    uint8_t block_buf[64];
    res = sim_dataflash_read(test_addr, block_buf, sizeof(block_buf));
    TEST_ASSERT_EQUAL_INT(SIM_DF_SUCCESS, res);
    for (int i = 0; i < 64; i++)
    {
        TEST_ASSERT_EQUAL_HEX8(0xFF, block_buf[i]);
    }
}

/* Test 3: Block aggregation calculation (4KB = 64 HW blocks x 64 bytes) */
void test_dataflash_aggregated_block_erase(void)
{
    uint32_t logical_block_size = 4096;
    uint32_t hw_blocks_per_logical = logical_block_size / SIM_DF_BLOCK_SIZE; /* 64 */
    TEST_ASSERT_EQUAL_UINT32(64, hw_blocks_per_logical);

    /* Write non-0xFF data at various offsets in logical block 1 (0x00101000 - 0x00101FFF) */
    uint32_t block1_base = SIM_DF_START_ADDR + logical_block_size;
    uint8_t data[4] = {0xAA, 0x55, 0xAA, 0x55};

    sim_dataflash_write(data, block1_base, 4);
    sim_dataflash_write(data, block1_base + 2048, 4);
    sim_dataflash_write(data, block1_base + 4092, 4);

    /* Verify written */
    uint8_t r[4];
    sim_dataflash_read(block1_base, r, 4);
    TEST_ASSERT_EQUAL_HEX8(0xAA, r[0]);

    /* Erase logical block 1 (all 64 physical blocks) */
    int res = sim_dataflash_erase(block1_base, hw_blocks_per_logical);
    TEST_ASSERT_EQUAL_INT(SIM_DF_SUCCESS, res);

    /* Verify entire 4KB block is 0xFF */
    uint32_t offset = block1_base - SIM_DF_START_ADDR;
    for (uint32_t i = 0; i < logical_block_size; i++)
    {
        TEST_ASSERT_EQUAL_HEX8(0xFF, g_sim_dataflash[offset + i]);
    }
}

/* Test 4: Boundary check and illegal write rejection */
void test_dataflash_boundary_and_overwriting_checks(void)
{
    uint8_t buf[8] = {0};

    /* Beyond 32KB range */
    int res = sim_dataflash_write(buf, SIM_DF_END_ADDR, 4);
    TEST_ASSERT_EQUAL_INT(SIM_DF_ERR_RANGE, res);

    res = sim_dataflash_read(SIM_DF_END_ADDR, buf, 4);
    TEST_ASSERT_EQUAL_INT(SIM_DF_ERR_RANGE, res);

    /* Unaligned address */
    res = sim_dataflash_write(buf, SIM_DF_START_ADDR + 1, 4);
    TEST_ASSERT_EQUAL_INT(SIM_DF_ERR_PARAM, res);

    /* Write 0x00 first */
    uint8_t zero[4] = {0x00, 0x00, 0x00, 0x00};
    res = sim_dataflash_write(zero, SIM_DF_START_ADDR, 4);
    TEST_ASSERT_EQUAL_INT(SIM_DF_SUCCESS, res);

    /* Attempt to write 0xFF over 0x00 without erase (illegal on NOR flash) */
    uint8_t ones[4] = {0xFF, 0xFF, 0xFF, 0xFF};
    res = sim_dataflash_write(ones, SIM_DF_START_ADDR, 4);
    TEST_ASSERT_EQUAL_INT(SIM_DF_ERR_NOT_ERASED, res);
}

/* Test 5: Chip-wide erase all 512 blocks */
void test_dataflash_erase_all(void)
{
    sim_dataflash_init_dirty();

    int res = sim_dataflash_erase_all();
    TEST_ASSERT_EQUAL_INT(SIM_DF_SUCCESS, res);

    for (size_t i = 0; i < SIM_DF_SIZE; i++)
    {
        TEST_ASSERT_EQUAL_HEX8(0xFF, g_sim_dataflash[i]);
    }
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_dataflash_init_clean_state);
    RUN_TEST(test_dataflash_single_block_erase_and_write);
    RUN_TEST(test_dataflash_aggregated_block_erase);
    RUN_TEST(test_dataflash_boundary_and_overwriting_checks);
    RUN_TEST(test_dataflash_erase_all);
    return UNITY_END();
}
