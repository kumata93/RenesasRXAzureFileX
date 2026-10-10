/***********************************************************************************************************************
 * File Name    : sim_dataflash.h
 * Description  : 32KB RAM-based simulated RX65N Data Flash for host unit tests.
 ***********************************************************************************************************************/

#ifndef SIM_DATAFLASH_H
#define SIM_DATAFLASH_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#define SIM_DF_START_ADDR       (0x00100000UL)
#define SIM_DF_SIZE             (32768UL)                           /* 32 KB */
#define SIM_DF_END_ADDR         (SIM_DF_START_ADDR + SIM_DF_SIZE)   /* 0x00108000 */
#define SIM_DF_BLOCK_SIZE       (64UL)                              /* 64-byte physical erase block */
#define SIM_DF_TOTAL_BLOCKS     (SIM_DF_SIZE / SIM_DF_BLOCK_SIZE)   /* 512 blocks */
#define SIM_DF_WRITE_UNIT       (4UL)                               /* 4-byte programming unit */

#define SIM_DF_SUCCESS          (0)
#define SIM_DF_ERR_PARAM        (-1)
#define SIM_DF_ERR_RANGE        (-2)
#define SIM_DF_ERR_NOT_ERASED   (-3)

/* Underlying simulated Data Flash memory buffer */
extern uint8_t g_sim_dataflash[SIM_DF_SIZE];

/**
 * @brief Initialize simulated Data Flash memory to 0xFF (clean erase state).
 */
void sim_dataflash_init_clean(void);

/**
 * @brief Fill simulated Data Flash with dirty non-0xFF pattern (to reproduce garbage data state).
 */
void sim_dataflash_init_dirty(void);

/**
 * @brief Erase a single 64-byte physical block.
 */
int sim_dataflash_erase_block(uint32_t block_addr);

/**
 * @brief Erase multiple contiguous 64-byte physical blocks.
 */
int sim_dataflash_erase(uint32_t start_addr, uint32_t num_blocks);

/**
 * @brief Erase entire 32KB simulated Data Flash (all 512 blocks).
 */
int sim_dataflash_erase_all(void);

/**
 * @brief Write data (4-byte aligned and multiple of 4 bytes).
 *        Detects and rejects attempts to write 1s over 0s without erasing (NOR flash hardware behavior).
 */
int sim_dataflash_write(const uint8_t *src_addr, uint32_t flash_addr, uint32_t byte_count);

/**
 * @brief Read data from simulated Data Flash.
 */
int sim_dataflash_read(uint32_t flash_addr, uint8_t *dst_buf, uint32_t byte_count);

#endif /* SIM_DATAFLASH_H */
