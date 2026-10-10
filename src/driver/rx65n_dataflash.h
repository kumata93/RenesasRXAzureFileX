/***********************************************************************************************************************
 * File Name    : rx65n_dataflash.h
 * Description  : Low-level driver interface for Renesas RX65N on-chip Data Flash (32KB).
 ***********************************************************************************************************************/

#ifndef RX65N_DATAFLASH_H
#define RX65N_DATAFLASH_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* RX65N Data Flash Specifications */
#define RX65N_DF_START_ADDR         (0x00100000UL)                          /* Data Flash base address */
#define RX65N_DF_SIZE               (32768UL)                               /* 32 KB total capacity */
#define RX65N_DF_END_ADDR           (RX65N_DF_START_ADDR + RX65N_DF_SIZE)   /* 0x00108000 */
#define RX65N_DF_BLOCK_SIZE         (64UL)                                  /* Hardware erase unit: 64 bytes */
#define RX65N_DF_TOTAL_BLOCKS       (RX65N_DF_SIZE / RX65N_DF_BLOCK_SIZE)   /* 512 physical blocks */
#define RX65N_DF_WRITE_UNIT         (4UL)                                   /* Programming unit: 4 bytes */

/* Return status codes */
#define RX65N_DF_SUCCESS            (0)
#define RX65N_DF_ERR_INVALID_PARAM  (-1)
#define RX65N_DF_ERR_HW             (-2)

/**
 * @brief Initialize Data Flash peripheral and enter P/E ready state.
 * @return RX65N_DF_SUCCESS on success, negative value on error.
 */
int rx65n_dataflash_init(void);

/**
 * @brief Erase a single 64-byte block in Data Flash.
 * @param block_addr Start address of the 64-byte block (must be 64-byte aligned).
 * @return RX65N_DF_SUCCESS on success, negative value on error.
 */
int rx65n_dataflash_erase_block(uint32_t block_addr);

/**
 * @brief Erase multiple contiguous 64-byte blocks in Data Flash.
 * @param start_addr Start address (must be 64-byte aligned).
 * @param num_blocks Number of 64-byte blocks to erase.
 * @return RX65N_DF_SUCCESS on success, negative value on error.
 */
int rx65n_dataflash_erase(uint32_t start_addr, uint32_t num_blocks);

/**
 * @brief Erase entire 32KB Data Flash memory (all 512 physical 64-byte blocks).
 * @return RX65N_DF_SUCCESS on success, negative value on error.
 */
int rx65n_dataflash_erase_all(void);

/**
 * @brief Write data to Data Flash (in 4-byte multiples).
 * @param src_addr Pointer to source data buffer in RAM.
 * @param flash_addr Destination address in Data Flash (must be 4-byte aligned).
 * @param byte_count Number of bytes to write (must be a multiple of 4).
 * @return RX65N_DF_SUCCESS on success, negative value on error.
 */
int rx65n_dataflash_write(const uint8_t *src_addr, uint32_t flash_addr, uint32_t byte_count);

/**
 * @brief Read data from Data Flash (memory-mapped address lookup / memcpy).
 * @param flash_addr Source address in Data Flash.
 * @param dst_buf Pointer to destination buffer in RAM.
 * @param byte_count Number of bytes to read.
 * @return RX65N_DF_SUCCESS on success, negative value on error.
 */
int rx65n_dataflash_read(uint32_t flash_addr, uint8_t *dst_buf, uint32_t byte_count);

/**
 * @brief Ensures the flash sequencer has exited P/E mode and returned to memory-mapped Read mode.
 * @return RX65N_DF_SUCCESS on success, negative value on error.
 */
int rx65n_dataflash_ensure_read_mode(void);

#ifdef __cplusplus
}
#endif

#endif /* RX65N_DATAFLASH_H */
