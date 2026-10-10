/***********************************************************************************************************************
 * File Name    : rx65n_dataflash.c
 * Description  : Low-level driver implementation for Renesas RX65N on-chip Data Flash (32KB).
 ***********************************************************************************************************************/

#include "rx65n_dataflash.h"
#include "r_flash_rx_if.h"
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

    return RX65N_DF_ERR_HW;
}

/***********************************************************************************************************************
 * Function Name: rx65n_dataflash_erase
 * Description  : Erases num_blocks contiguous 64-byte blocks starting at addr.
 * Return Value : RX65N_DF_SUCCESS on success, negative error code on failure.
 ***********************************************************************************************************************/
int rx65n_dataflash_erase(uint32_t addr, uint32_t num_blocks)
{
    /* Validate start address alignment and range */
    if ((addr < RX65N_DF_START_ADDR) || ((addr % RX65N_DF_BLOCK_SIZE) != 0))
    {
        return RX65N_DF_ERR_INVALID_PARAM;
    }

    /* Validate block count and end address */
    if ((num_blocks == 0) || ((addr + (num_blocks * RX65N_DF_BLOCK_SIZE)) > RX65N_DF_END_ADDR))
    {
        return RX65N_DF_ERR_INVALID_PARAM;
    }

    flash_err_t err = R_FLASH_Erase((flash_block_address_t)addr, num_blocks);
    if (FLASH_SUCCESS != err)
    {
        return RX65N_DF_ERR_HW;
    }

    return RX65N_DF_SUCCESS;
}

/***********************************************************************************************************************
 * Function Name: rx65n_dataflash_write
 * Description  : Writes bytes of data from src to dst. bytes must be a multiple of 4.
 * Return Value : RX65N_DF_SUCCESS on success, negative error code on failure.
 ***********************************************************************************************************************/
int rx65n_dataflash_write(const uint8_t *src, uint32_t dst, uint32_t bytes)
{
    if (NULL == src)
    {
        return RX65N_DF_ERR_INVALID_PARAM;
    }

    /* Destination address must be 4-byte aligned and within Data Flash */
    if ((dst < RX65N_DF_START_ADDR) || ((dst % RX65N_DF_WRITE_UNIT) != 0))
    {
        return RX65N_DF_ERR_INVALID_PARAM;
    }

    /* Byte count must be a non-zero multiple of 4 and not overflow Data Flash */
    if ((bytes == 0) || ((bytes % RX65N_DF_WRITE_UNIT) != 0) || ((dst + bytes) > RX65N_DF_END_ADDR))
    {
        return RX65N_DF_ERR_INVALID_PARAM;
    }

    flash_err_t err = R_FLASH_Write((uint32_t)src, dst, bytes);
    if (FLASH_SUCCESS != err)
    {
        return RX65N_DF_ERR_HW;
    }

    return RX65N_DF_SUCCESS;
}

/***********************************************************************************************************************
 * Function Name: rx65n_dataflash_read
 * Description  : Reads bytes of data from Data Flash (memory-mapped address) to dst buffer.
 * Return Value : RX65N_DF_SUCCESS on success, negative error code on failure.
 ***********************************************************************************************************************/
int rx65n_dataflash_read(uint32_t src, uint8_t *dst, uint32_t bytes)
{
    if (NULL == dst)
    {
        return RX65N_DF_ERR_INVALID_PARAM;
    }

    if ((src < RX65N_DF_START_ADDR) || ((src + bytes) > RX65N_DF_END_ADDR))
    {
        return RX65N_DF_ERR_INVALID_PARAM;
    }

    if (bytes > 0)
    {
        /* On RX65N, Data Flash (0x00100000 - 0x00107FFF) is memory-mapped for read access */
        memcpy(dst, (const void *)src, bytes);
    }

    return RX65N_DF_SUCCESS;
}
