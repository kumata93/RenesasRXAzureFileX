/***********************************************************************************************************************
 * File Name    : sim_dataflash.c
 * Description  : Implementation of 32KB RAM-based simulated RX65N Data Flash.
 ***********************************************************************************************************************/

#include "sim_dataflash.h"
#include <string.h>

/* 32KB Simulated Data Flash memory */
uint8_t g_sim_dataflash[SIM_DF_SIZE];

void sim_dataflash_init_clean(void)
{
    memset(g_sim_dataflash, 0xFF, sizeof(g_sim_dataflash));
}

void sim_dataflash_init_dirty(void)
{
    /* Fill with non-0xFF arbitrary data pattern representing uninitialized/garbage flash */
    for (size_t i = 0; i < sizeof(g_sim_dataflash); i++)
    {
        g_sim_dataflash[i] = (uint8_t)(i ^ 0xA5);
    }
}

int sim_dataflash_erase_block(uint32_t block_addr)
{
    return sim_dataflash_erase(block_addr, 1);
}

int sim_dataflash_erase(uint32_t start_addr, uint32_t num_blocks)
{
    if ((start_addr < SIM_DF_START_ADDR) || ((start_addr % SIM_DF_BLOCK_SIZE) != 0))
    {
        return SIM_DF_ERR_PARAM;
    }

    uint32_t total_bytes = num_blocks * SIM_DF_BLOCK_SIZE;
    if ((num_blocks == 0) || ((start_addr + total_bytes) > SIM_DF_END_ADDR))
    {
        return SIM_DF_ERR_RANGE;
    }

    uint32_t offset = start_addr - SIM_DF_START_ADDR;
    memset(&g_sim_dataflash[offset], 0xFF, total_bytes);

    return SIM_DF_SUCCESS;
}

int sim_dataflash_erase_all(void)
{
    return sim_dataflash_erase(SIM_DF_START_ADDR, SIM_DF_TOTAL_BLOCKS);
}

int sim_dataflash_write(const uint8_t *src_addr, uint32_t flash_addr, uint32_t byte_count)
{
    if (src_addr == NULL)
    {
        return SIM_DF_ERR_PARAM;
    }

    if ((flash_addr < SIM_DF_START_ADDR) || ((flash_addr % SIM_DF_WRITE_UNIT) != 0))
    {
        return SIM_DF_ERR_PARAM;
    }

    if ((byte_count == 0) || ((byte_count % SIM_DF_WRITE_UNIT) != 0) ||
        ((flash_addr + byte_count) > SIM_DF_END_ADDR))
    {
        return SIM_DF_ERR_RANGE;
    }

    uint32_t offset = flash_addr - SIM_DF_START_ADDR;

    /* Verify flash physical property: bits can only be transitioned from 1 to 0.
     * Attempting to program a 1 where a 0 exists without an erase is rejected. */
    for (uint32_t i = 0; i < byte_count; i++)
    {
        uint8_t current_val = g_sim_dataflash[offset + i];
        uint8_t write_val   = src_addr[i];

        /* If write_val has 1s where current_val has 0s, it's an illegal write on NOR flash */
        if ((current_val & write_val) != write_val)
        {
            return SIM_DF_ERR_NOT_ERASED;
        }
    }

    /* Apply AND-programming (bits flip from 1 to 0) */
    for (uint32_t i = 0; i < byte_count; i++)
    {
        g_sim_dataflash[offset + i] &= src_addr[i];
    }

    return SIM_DF_SUCCESS;
}

int sim_dataflash_read(uint32_t flash_addr, uint8_t *dst_buf, uint32_t byte_count)
{
    if (dst_buf == NULL)
    {
        return SIM_DF_ERR_PARAM;
    }

    if ((flash_addr < SIM_DF_START_ADDR) || ((flash_addr + byte_count) > SIM_DF_END_ADDR))
    {
        return SIM_DF_ERR_RANGE;
    }

    uint32_t offset = flash_addr - SIM_DF_START_ADDR;
    memcpy(dst_buf, &g_sim_dataflash[offset], byte_count);

    return SIM_DF_SUCCESS;
}
