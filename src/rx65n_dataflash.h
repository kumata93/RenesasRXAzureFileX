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

/* RX65N Data Flash Hardware Specifications */
#define RX65N_DF_START_ADDR         (0x00100000UL)                          /* Data Flash base address */
#define RX65N_DF_SIZE               (32768UL)                               /* 32 KB total size */
#define RX65N_DF_END_ADDR           (RX65N_DF_START_ADDR + RX65N_DF_SIZE)   /* 0x00108000 */
#define RX65N_DF_BLOCK_SIZE         (64UL)                                  /* Erase block unit: 64 bytes */
#define RX65N_DF_TOTAL_BLOCKS       (RX65N_DF_SIZE / RX65N_DF_BLOCK_SIZE)   /* 512 blocks */
#define RX65N_DF_WRITE_UNIT         (4UL)                                   /* Programming unit: 4 bytes */
#define RX65N_DF_SECTOR_SIZE        (512UL)                                 /* FileX Sector size: 512 bytes */
#define RX65N_DF_TOTAL_SECTORS      (RX65N_DF_SIZE / RX65N_DF_SECTOR_SIZE)  /* 64 sectors (FAT12) */
#define RX65N_DF_BLOCKS_PER_SECTOR  (RX65N_DF_SECTOR_SIZE / RX65N_DF_BLOCK_SIZE) /* 8 blocks per sector */

/* Return status codes */
#define RX65N_DF_SUCCESS            (0)
#define RX65N_DF_ERR_INVALID_PARAM  (-1)
#define RX65N_DF_ERR_HW             (-2)

/**
 * @brief  Initialize Data Flash (prepare P/E mode via Flash FIT module)
 * @retval RX65N_DF_SUCCESS on success
 * @retval RX65N_DF_ERR_HW on hardware/driver error
 */
int rx65n_dataflash_init(void);

/**
 * @brief  Erase contiguous 64-byte blocks in Data Flash
 * @param  addr       Start address (must be 64-byte aligned and within Data Flash range)
 * @param  num_blocks Number of 64-byte blocks to erase
 * @retval RX65N_DF_SUCCESS on success
 * @retval negative value on error
 */
int rx65n_dataflash_erase(uint32_t addr, uint32_t num_blocks);

/**
 * @brief  Write data to Data Flash (in 4-byte multiples)
 * @param  src        Pointer to source data buffer in RAM
 * @param  dst        Destination address in Data Flash (must be 4-byte aligned)
 * @param  bytes      Number of bytes to write (must be a multiple of 4)
 * @retval RX65N_DF_SUCCESS on success
 * @retval negative value on error
 */
int rx65n_dataflash_write(const uint8_t *src, uint32_t dst, uint32_t bytes);

/**
 * @brief  Read data from Data Flash (memory-mapped direct read)
 * @param  src        Source address in Data Flash
 * @param  dst        Pointer to destination buffer in RAM
 * @param  bytes      Number of bytes to read
 * @retval RX65N_DF_SUCCESS on success
 * @retval negative value on error
 */
int rx65n_dataflash_read(uint32_t src, uint8_t *dst, uint32_t bytes);

#ifdef __cplusplus
}
#endif

#endif /* RX65N_DATAFLASH_H */
