/***********************************************************************************************************************
 * File Name    : lx_nor_rx65n_dataflash.h
 * Description  : LevelX NOR Flash low-level driver for Renesas RX65N on-chip Data Flash (32KB).
 ***********************************************************************************************************************/

#ifndef LX_NOR_RX65N_DATAFLASH_H
#define LX_NOR_RX65N_DATAFLASH_H

#include "lx_api.h"
#include "rx65n_dataflash.h"

#ifdef __cplusplus
extern "C" {
#endif

/* LevelX NOR Block Configuration for RX65N Data Flash (32KB total) */
/* Each LevelX block is 2048 bytes (32 hardware blocks of 64 bytes).
 * 32KB / 2048 = 16 LevelX blocks.
 * Each LevelX block contains 1 header sector (512B) + 3 physical data sectors (512B x 3 = 1536B).
 * Total 48 physical sectors, 1 block reserved for wear-leveling / reclamation -> ~45 logical sectors. */
#define LX_NOR_RX65N_BLOCK_SIZE                 (2048UL)
#define LX_NOR_RX65N_TOTAL_BLOCKS               (RX65N_DF_SIZE / LX_NOR_RX65N_BLOCK_SIZE)           /* 16 blocks */
#define LX_NOR_RX65N_WORDS_PER_BLOCK            (LX_NOR_RX65N_BLOCK_SIZE / sizeof(ULONG))           /* 512 words */
#define LX_NOR_RX65N_HW_BLOCKS_PER_LX_BLOCK     (LX_NOR_RX65N_BLOCK_SIZE / RX65N_DF_BLOCK_SIZE)     /* 32 HW blocks */
#define LX_NOR_RX65N_SECTOR_SIZE_BYTES          (LX_NOR_SECTOR_SIZE * sizeof(ULONG))                /* 512 bytes */

/**
 * @brief Initialize the LevelX NOR Flash driver for RX65N Data Flash.
 * @param nor_flash Pointer to the LevelX NOR Flash control block.
 * @return LX_SUCCESS on success, error code otherwise.
 */
UINT lx_nor_rx65n_dataflash_initialize(LX_NOR_FLASH *nor_flash);

/**
 * @brief Read words from Data Flash.
 */
UINT lx_nor_rx65n_dataflash_read(ULONG *flash_address, ULONG *destination, ULONG words);

/**
 * @brief Write words to Data Flash (in 4-byte multiples).
 */
UINT lx_nor_rx65n_dataflash_write(ULONG *flash_address, ULONG *source, ULONG words);

/**
 * @brief Erase a LevelX block in Data Flash.
 */
UINT lx_nor_rx65n_dataflash_block_erase(ULONG block, ULONG erase_count);

/**
 * @brief Verify that a LevelX block is completely erased (all 0xFF).
 */
UINT lx_nor_rx65n_dataflash_block_erased_verify(ULONG block);

/**
 * @brief LevelX system error notification callback.
 */
UINT lx_nor_rx65n_dataflash_system_error(UINT error_code);

/**
 * @brief Erase all LevelX blocks in Data Flash (full media format).
 */
UINT lx_nor_rx65n_dataflash_erase_all(VOID);

#ifdef __cplusplus
}
#endif

#endif /* LX_NOR_RX65N_DATAFLASH_H */
