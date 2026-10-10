/***********************************************************************************************************************
 * File Name    : lx_nor_rx65n_dataflash.c
 * Description  : LevelX NOR Flash low-level driver implementation for Renesas RX65N on-chip Data Flash (32KB).
 ***********************************************************************************************************************/

#include "lx_nor_rx65n_dataflash.h"
#include <string.h>

/* Sector buffer for LevelX NOR Flash operation (512 bytes / 128 ULONG words) */
static ULONG g_lx_nor_sector_buffer[LX_NOR_SECTOR_SIZE];

/***********************************************************************************************************************
 * Function Name: lx_nor_rx65n_dataflash_initialize
 * Description  : Configures the LevelX NOR Flash geometry and callbacks for RX65N Data Flash.
 ***********************************************************************************************************************/
UINT lx_nor_rx65n_dataflash_initialize(LX_NOR_FLASH *nor_flash)
{
    if (NULL == nor_flash)
    {
        return LX_ERROR;
    }

    if (rx65n_dataflash_init() != RX65N_DF_SUCCESS)
    {
        return LX_ERROR;
    }

    /* Setup geometry and base address of the Data Flash memory */
    nor_flash->lx_nor_flash_base_address               = (ULONG *)RX65N_DF_START_ADDR;
    nor_flash->lx_nor_flash_total_blocks               = LX_NOR_RX65N_TOTAL_BLOCKS;
    nor_flash->lx_nor_flash_words_per_block            = LX_NOR_RX65N_WORDS_PER_BLOCK;

    /* Setup function pointers for LevelX NOR Flash driver interface */
    nor_flash->lx_nor_flash_driver_read                = lx_nor_rx65n_dataflash_read;
    nor_flash->lx_nor_flash_driver_write               = lx_nor_rx65n_dataflash_write;
    nor_flash->lx_nor_flash_driver_block_erase         = lx_nor_rx65n_dataflash_block_erase;
    nor_flash->lx_nor_flash_driver_block_erased_verify = lx_nor_rx65n_dataflash_block_erased_verify;
    nor_flash->lx_nor_flash_driver_system_error        = lx_nor_rx65n_dataflash_system_error;

    /* Setup local sector buffer (512 bytes) */
    nor_flash->lx_nor_flash_sector_buffer              = &g_lx_nor_sector_buffer[0];

    return LX_SUCCESS;
}

/***********************************************************************************************************************
 * Function Name: lx_nor_rx65n_dataflash_read
 * Description  : Reads words from Data Flash into destination buffer.
 ***********************************************************************************************************************/
UINT lx_nor_rx65n_dataflash_read(ULONG *flash_address, ULONG *destination, ULONG words)
{
    if ((NULL == flash_address) || (NULL == destination))
    {
        return LX_ERROR;
    }

    uint32_t addr  = (uint32_t)flash_address;
    uint32_t bytes = words * sizeof(ULONG);

    if (rx65n_dataflash_read(addr, (uint8_t *)destination, bytes) != RX65N_DF_SUCCESS)
    {
        return LX_ERROR;
    }

    return LX_SUCCESS;
}

/***********************************************************************************************************************
 * Function Name: lx_nor_rx65n_dataflash_write
 * Description  : Writes words from source buffer into Data Flash (in 4-byte multiples).
 ***********************************************************************************************************************/
UINT lx_nor_rx65n_dataflash_write(ULONG *flash_address, ULONG *source, ULONG words)
{
    if ((NULL == flash_address) || (NULL == source))
    {
        return LX_ERROR;
    }

    uint32_t addr  = (uint32_t)flash_address;
    uint32_t bytes = words * sizeof(ULONG);

    if (rx65n_dataflash_write((const uint8_t *)source, addr, bytes) != RX65N_DF_SUCCESS)
    {
        return LX_ERROR;
    }

    return LX_SUCCESS;
}

/***********************************************************************************************************************
 * Function Name: lx_nor_rx65n_dataflash_block_erase
 * Description  : Erases a LevelX logical block (4096 bytes = 64 contiguous 64-byte hardware blocks).
 ***********************************************************************************************************************/
UINT lx_nor_rx65n_dataflash_block_erase(ULONG block, ULONG erase_count)
{
    (void)erase_count;

    if (block >= LX_NOR_RX65N_TOTAL_BLOCKS)
    {
        return LX_ERROR;
    }

    uint32_t start_addr = RX65N_DF_START_ADDR + (block * LX_NOR_RX65N_BLOCK_SIZE);

    if (rx65n_dataflash_erase(start_addr, LX_NOR_RX65N_HW_BLOCKS_PER_LX_BLOCK) != RX65N_DF_SUCCESS)
    {
        return LX_ERROR;
    }

    return LX_SUCCESS;
}

/***********************************************************************************************************************
 * Function Name: lx_nor_rx65n_dataflash_block_erased_verify
 * Description  : Verifies that all 32-bit words in the LevelX block are erased (0xFFFFFFFF).
 ***********************************************************************************************************************/
UINT lx_nor_rx65n_dataflash_block_erased_verify(ULONG block)
{
    if (block >= LX_NOR_RX65N_TOTAL_BLOCKS)
    {
        return LX_ERROR;
    }

    const uint32_t *word_ptr = (const uint32_t *)(RX65N_DF_START_ADDR + (block * LX_NOR_RX65N_BLOCK_SIZE));
    ULONG words = LX_NOR_RX65N_WORDS_PER_BLOCK;

    while (words--)
    {
        if (*word_ptr++ != 0xFFFFFFFFUL)
        {
            return LX_ERROR;
        }
    }

    return LX_SUCCESS;
}

/***********************************************************************************************************************
 * Function Name: lx_nor_rx65n_dataflash_system_error
 * Description  : LevelX system error callback.
 ***********************************************************************************************************************/
UINT lx_nor_rx65n_dataflash_system_error(UINT error_code)
{
    (void)error_code;
    return LX_ERROR;
}

/***********************************************************************************************************************
 * Function Name: lx_nor_rx65n_dataflash_erase_all
 * Description  : Erases all 8 LevelX logical blocks (the entire 32KB Data Flash).
 ***********************************************************************************************************************/
UINT lx_nor_rx65n_dataflash_erase_all(VOID)
{
    if (rx65n_dataflash_erase_all() != RX65N_DF_SUCCESS)
    {
        return LX_ERROR;
    }

    return LX_SUCCESS;
}
