/***********************************************************************************************************************
 * File Name    : rx65n_dataflash.c
 * Description  : Low-level driver implementation for Renesas RX65N on-chip Data Flash (32KB).
 ***********************************************************************************************************************/

#include "rx65n_dataflash.h"
#include "r_flash_rx_if.h"
#include <stdio.h>
#include <string.h>

/***********************************************************************************************************************
 * Function Name: rx65n_dataflash_init
 * Description  : Initializes the flash peripheral sequencer and enters ready state.
 * Return Value : RX65N_DF_SUCCESS on success, RX65N_DF_ERR_HW on failure.
 ***********************************************************************************************************************/
int rx65n_dataflash_init(void)
{
    flash_err_t err = R_FLASH_Open();

    if ((FLASH_SUCCESS == err) || (FLASH_ERR_ALREADY_OPEN == err))
    {
        return RX65N_DF_SUCCESS;
    }

    printf("  [DF_HW] R_FLASH_Open failed: err=%d\r\n", (int)err);
    return RX65N_DF_ERR_HW;
}

/***********************************************************************************************************************
 * Function Name: rx65n_dataflash_erase_block
 * Description  : Erases a single 64-byte block starting at block_addr.
 * Return Value : RX65N_DF_SUCCESS on success, negative error code on failure.
 ***********************************************************************************************************************/
int rx65n_dataflash_erase_block(uint32_t block_addr)
{
    return rx65n_dataflash_erase(block_addr, 1);
}

/***********************************************************************************************************************
 * Function Name: rx65n_dataflash_erase
 * Description  : Erases num_blocks contiguous 64-byte blocks starting at start_addr.
 * Return Value : RX65N_DF_SUCCESS on success, negative error code on failure.
 ***********************************************************************************************************************/
int rx65n_dataflash_erase(uint32_t start_addr, uint32_t num_blocks)
{
    /* Validate start address alignment and range */
    if ((start_addr < RX65N_DF_START_ADDR) || ((start_addr % RX65N_DF_BLOCK_SIZE) != 0))
    {
        printf("  [DF_HW] Erase param error: start_addr=0x%08lX invalid\r\n", (unsigned long)start_addr);
        return RX65N_DF_ERR_INVALID_PARAM;
    }

    /* Validate block count and end address */
    if ((num_blocks == 0) || ((start_addr + (num_blocks * RX65N_DF_BLOCK_SIZE)) > RX65N_DF_END_ADDR))
    {
        printf("  [DF_HW] Erase param error: num_blocks=%lu out of range\r\n", (unsigned long)num_blocks);
        return RX65N_DF_ERR_INVALID_PARAM;
    }

    flash_err_t err = R_FLASH_Erase((flash_block_address_t)start_addr, num_blocks);
    if (FLASH_SUCCESS != err)
    {
        printf("  [DF_HW] R_FLASH_Erase failed at 0x%08lX (blocks=%lu): err=%d\r\n",
               (unsigned long)start_addr, (unsigned long)num_blocks, (int)err);
        return RX65N_DF_ERR_HW;
    }

    return RX65N_DF_SUCCESS;
}

/***********************************************************************************************************************
 * Function Name: rx65n_dataflash_erase_all
 * Description  : Erases entire 32KB Data Flash block-by-block (all 512 physical 64-byte blocks).
 * Return Value : RX65N_DF_SUCCESS on success, negative error code on failure.
 ***********************************************************************************************************************/
int rx65n_dataflash_erase_all(void)
{
    if (rx65n_dataflash_init() != RX65N_DF_SUCCESS)
    {
        return RX65N_DF_ERR_HW;
    }

    printf("  [DF_HW] Erasing all %lu blocks (0x%08lX - 0x%08lX)...\r\n",
           (unsigned long)RX65N_DF_TOTAL_BLOCKS,
           (unsigned long)RX65N_DF_START_ADDR,
           (unsigned long)(RX65N_DF_END_ADDR - 1));

    /* Erase block by block to guarantee reliable sequencing on hardware */
    for (uint32_t blk = 0; blk < RX65N_DF_TOTAL_BLOCKS; blk++)
    {
        uint32_t addr = RX65N_DF_START_ADDR + (blk * RX65N_DF_BLOCK_SIZE);
        flash_err_t err = R_FLASH_Erase((flash_block_address_t)addr, 1);
        if (FLASH_SUCCESS != err)
        {
            printf("  [DF_HW] ERROR: R_FLASH_Erase failed at block %lu (0x%08lX): err=%d\r\n",
                   (unsigned long)blk, (unsigned long)addr, (int)err);
            return RX65N_DF_ERR_HW;
        }

        if ((blk == 0) || (blk == (RX65N_DF_TOTAL_BLOCKS - 1)))
        {
            printf("  [DF_HW] Block %lu (0x%08lX) erased OK\r\n", (unsigned long)blk, (unsigned long)addr);
        }
    }

    printf("  [DF_HW] All %lu blocks erased successfully.\r\n", (unsigned long)RX65N_DF_TOTAL_BLOCKS);
    return RX65N_DF_SUCCESS;
}

/***********************************************************************************************************************
 * Function Name: rx65n_dataflash_write
 * Description  : Writes byte_count data from src_addr to flash_addr. byte_count must be a multiple of 4.
 * Return Value : RX65N_DF_SUCCESS on success, negative error code on failure.
 ***********************************************************************************************************************/
int rx65n_dataflash_write(const uint8_t *src_addr, uint32_t flash_addr, uint32_t byte_count)
{
    if (NULL == src_addr)
    {
        printf("  [DF_HW] Write param error: src_addr is NULL\r\n");
        return RX65N_DF_ERR_INVALID_PARAM;
    }

    /* Destination address must be 4-byte aligned and within Data Flash */
    if ((flash_addr < RX65N_DF_START_ADDR) || ((flash_addr % RX65N_DF_WRITE_UNIT) != 0))
    {
        printf("  [DF_HW] Write param error: flash_addr=0x%08lX unaligned or out of range\r\n",
               (unsigned long)flash_addr);
        return RX65N_DF_ERR_INVALID_PARAM;
    }

    /* Byte count must be a non-zero multiple of 4 and not overflow Data Flash */
    if ((byte_count == 0) || ((byte_count % RX65N_DF_WRITE_UNIT) != 0) ||
        ((flash_addr + byte_count) > RX65N_DF_END_ADDR))
    {
        printf("  [DF_HW] Write param error: flash_addr=0x%08lX, byte_count=%lu invalid\r\n",
               (unsigned long)flash_addr, (unsigned long)byte_count);
        return RX65N_DF_ERR_INVALID_PARAM;
    }

    flash_err_t err = R_FLASH_Write((uint32_t)src_addr, flash_addr, byte_count);
    if (FLASH_SUCCESS != err)
    {
        printf("  [DF_HW] R_FLASH_Write failed: dest=0x%08lX, bytes=%lu, err=%d\r\n",
               (unsigned long)flash_addr, (unsigned long)byte_count, (int)err);
        return RX65N_DF_ERR_HW;
    }

    return RX65N_DF_SUCCESS;
}

/***********************************************************************************************************************
 * Function Name: rx65n_dataflash_read
 * Description  : Reads byte_count data from Data Flash (memory-mapped address) to dst_buf buffer.
 * Return Value : RX65N_DF_SUCCESS on success, negative error code on failure.
 ***********************************************************************************************************************/
int rx65n_dataflash_read(uint32_t flash_addr, uint8_t *dst_buf, uint32_t byte_count)
{
    if (NULL == dst_buf)
    {
        return RX65N_DF_ERR_INVALID_PARAM;
    }

    if ((flash_addr < RX65N_DF_START_ADDR) || ((flash_addr + byte_count) > RX65N_DF_END_ADDR))
    {
        printf("  [DF_HW] Read param error: addr=0x%08lX, bytes=%lu out of range\r\n",
               (unsigned long)flash_addr, (unsigned long)byte_count);
        return RX65N_DF_ERR_INVALID_PARAM;
    }

    if (byte_count > 0)
    {
        /* Memory-mapped read */
        memcpy(dst_buf, (const void *)flash_addr, byte_count);
    }

    return RX65N_DF_SUCCESS;
}
