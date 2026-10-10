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
/* We aggregate 64 physical 64-byte hardware blocks into one 4,096-byte (4KB) LevelX block.
 * 32KB / 4096 = 8 LevelX logical blocks.
 *
 * In LevelX NOR:
 *   - words_per_block = 4096 / 4 = 1024 words
 *   - sector_size = 512 bytes = 128 words
 *   - Block overhead:
 *       header words = 3 (erase_count, min_log_sector, max_log_sector)
 *       sectors_per_block = (1024 / 128) - 1 = 7 sectors
 *       bit_map_words = (7 + 31) / 32 = 1 word
 *       sector_map_words = 7 words
 *       total header words = 3 + 1 + 7 = 11 words <= 128 words (1 sector)
 *       physical sectors per block = 7 sectors (3,584 bytes)
 *   - Total physical sectors = 8 blocks * 7 sectors = 56 physical sectors
 *   - 1 block is reserved by LevelX for wear-leveling / garbage collection
 *   - Max usable logical sectors = 7 * (8 - 1) = 49 logical sectors (~24.5 KB)
 */
#define LX_NOR_RX65N_BLOCK_SIZE                 (4096UL)
#define LX_NOR_RX65N_TOTAL_BLOCKS               (RX65N_DF_SIZE / LX_NOR_RX65N_BLOCK_SIZE)           /* 8 blocks */
#define LX_NOR_RX65N_WORDS_PER_BLOCK            (LX_NOR_RX65N_BLOCK_SIZE / sizeof(ULONG))           /* 1024 words */
#define LX_NOR_RX65N_HW_BLOCKS_PER_LX_BLOCK     (LX_NOR_RX65N_BLOCK_SIZE / RX65N_DF_BLOCK_SIZE)     /* 64 HW blocks */
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
 * @brief Erase a LevelX logical block in Data Flash (64 HW blocks of 64 bytes).
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
