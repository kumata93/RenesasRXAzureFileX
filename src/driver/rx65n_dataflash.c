/***********************************************************************************************************************
 * File Name    : rx65n_dataflash.c
 * Description  : Low-level driver implementation for Renesas RX65N on-chip Data Flash (32KB).
 ***********************************************************************************************************************/

#include "rx65n_dataflash.h"
#include "r_flash_rx_if.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

/* Bitmap tracking the programmed state of every 4-byte word (8192 words = 1 KB of RAM).
 * On RX65N (RV40F process), erased flash reads are undefined rather than 0xFF, and a programmed
 * word can NOT be programmed again without erasing its whole 64-byte block.
 * LevelX NOR flash, however, expects (a) erased areas to read 0xFFFFFFFF and (b) the ability to
 * overwrite a word in place by clearing bits (1 -> 0). Both are emulated in this driver:
 *   0 = not programmed (reads as 0xFFFFFFFF), 1 = programmed (reads real memory contents). */
#define RX65N_DF_WORDS_PER_HW_BLOCK (RX65N_DF_BLOCK_SIZE / RX65N_DF_WRITE_UNIT) /* 16 */
#define RX65N_DF_TOTAL_WORDS        (RX65N_DF_SIZE / RX65N_DF_WRITE_UNIT)       /* 8192 */
static uint8_t s_df_written_map[RX65N_DF_TOTAL_WORDS / 8];
static bool    s_df_map_initialized = false;

static bool rx65n_dataflash_is_word_programmed(uint32_t word_idx)
{
    if (word_idx >= RX65N_DF_TOTAL_WORDS)
    {
        return false;
    }
    return (s_df_written_map[word_idx / 8] & (1 << (word_idx % 8))) != 0;
}

static void rx65n_dataflash_set_word(uint32_t word_idx, bool programmed)
{
    if (word_idx >= RX65N_DF_TOTAL_WORDS)
    {
        return;
    }
    if (programmed)
    {
        s_df_written_map[word_idx / 8] |= (uint8_t)(1 << (word_idx % 8));
    }
    else
    {
        s_df_written_map[word_idx / 8] &= (uint8_t)(~(1 << (word_idx % 8)));
    }
}

static void rx65n_dataflash_map_init_from_hw(void)
{
    /* Clear bitmap first (mark all unprogrammed) */
    memset(s_df_written_map, 0, sizeof(s_df_written_map));

    /* Use hardware BlankCheck per 64-byte block, then refine non-blank blocks word by word
     * (4-byte BlankCheck = the Data Flash minimum program size). This is essential after a reset:
     * unprogrammed words inside a partially written block read undefined values and must NOT be
     * treated as programmed data. If a word-level check fails, conservatively treat it as programmed. */
    for (uint32_t b = 0; b < RX65N_DF_TOTAL_BLOCKS; b++)
    {
        uint32_t addr = RX65N_DF_START_ADDR + (b * RX65N_DF_BLOCK_SIZE);
        flash_res_t res = FLASH_RES_NOT_BLANK;
        flash_err_t err = R_FLASH_BlankCheck(addr, RX65N_DF_BLOCK_SIZE, &res);
        if ((FLASH_SUCCESS == err) && (FLASH_RES_BLANK == res))
        {
            continue;
        }

        for (uint32_t w = 0; w < RX65N_DF_WORDS_PER_HW_BLOCK; w++)
        {
            flash_res_t wres = FLASH_RES_NOT_BLANK;
            flash_err_t werr = R_FLASH_BlankCheck(addr + (w * RX65N_DF_WRITE_UNIT), RX65N_DF_WRITE_UNIT, &wres);
            if ((FLASH_SUCCESS != werr) || (FLASH_RES_BLANK != wres))
            {
                rx65n_dataflash_set_word((b * RX65N_DF_WORDS_PER_HW_BLOCK) + w, true);
            }
        }
    }
    rx65n_dataflash_ensure_read_mode();
    s_df_map_initialized = true;
}

static void rx65n_dataflash_mark_erased(uint32_t addr, uint32_t num_blocks)
{
    uint32_t start_word = (addr - RX65N_DF_START_ADDR) / RX65N_DF_WRITE_UNIT;
    uint32_t count      = num_blocks * RX65N_DF_WORDS_PER_HW_BLOCK;

    for (uint32_t w = start_word; (w < (start_word + count)) && (w < RX65N_DF_TOTAL_WORDS); w++)
    {
        rx65n_dataflash_set_word(w, false);
    }
}

/* True if any word of the 64-byte hardware block is programmed. */
static bool rx65n_dataflash_is_hw_block_written(uint32_t blk_idx)
{
    if (blk_idx >= RX65N_DF_TOTAL_BLOCKS)
    {
        return false;
    }
    for (uint32_t w = 0; w < RX65N_DF_WORDS_PER_HW_BLOCK; w++)
    {
        if (rx65n_dataflash_is_word_programmed((blk_idx * RX65N_DF_WORDS_PER_HW_BLOCK) + w))
        {
            return true;
        }
    }
    return false;
}

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
        /* NOTE: The RX65N FLASH register block (see iodefine.h st_flash) has no DFLRE0/1 or
         * DFLWE0/1 registers (those belong to other RX families). Data Flash read/write access
         * on RX65N is gated by FENTRYR (P/E vs. read mode) and FWEPROR.FLWE, which the FIT
         * driver manages. Hence we only make sure the sequencer is in memory-mapped read mode. */
        rx65n_dataflash_ensure_read_mode();

        if (!s_df_map_initialized)
        {
            rx65n_dataflash_map_init_from_hw();
        }

        return RX65N_DF_SUCCESS;
    }

    printf("  [DF_HW] R_FLASH_Open failed: err=%d\r\n", (int)err);
    return RX65N_DF_ERR_HW;
}

/***********************************************************************************************************************
 * Function Name: rx65n_dataflash_ensure_read_mode
 * Description  : Ensures the flash sequencer has exited P/E mode and returned to memory-mapped Read mode.
 ***********************************************************************************************************************/
int rx65n_dataflash_ensure_read_mode(void)
{
    /* Transition FCU from P/E mode back to Read mode if FENTRYR is non-zero.
     * NOTE: Do NOT call R_FLASH_Control(FLASH_CMD_RESET) here. In the READY state the FIT's
     * flash_reset() enters Code Flash P/E mode (FENTRYR=0xAA01) from code that is not placed in
     * RAM (FLASH_CFG_CODE_FLASH_ENABLE == 0), which makes ROM unreadable to the CPU. */
    if (FLASH.FENTRYR.WORD != 0x0000)
    {
        FLASH.FENTRYR.WORD = 0xAA00;
        uint32_t timeout = 100000;
        while ((FLASH.FENTRYR.WORD != 0x0000) && (timeout > 0))
        {
            timeout--;
        }
        FLASH.FWEPROR.BYTE = 0x00; /* Disable FLWE bit */
    }

    return RX65N_DF_SUCCESS;
}

/***********************************************************************************************************************
 * Raw FACI (Flash Application Command Interface) primitives for RX65N Data Flash.
 * These bypass the FIT r_flash_rx API, whose R_FLASH_Write/R_FLASH_Erase failed on this target while the same raw
 * sequence was verified on hardware (program + read-back OK).
 ***********************************************************************************************************************/
#define RAW_FACI_CMD_AREA           (0x007E0000UL)
#define RAW_FACI_CMD_PROGRAM        (0xE8)
#define RAW_FACI_CMD_PROGRAM_DF_CNT (0x02)   /* 2 halfwords (4 bytes) per program command */
#define RAW_FACI_CMD_BLOCK_ERASE    (0x20)
#define RAW_FACI_CMD_FINAL          (0xD0)
#define RAW_FACI_CMD_STATUS_CLEAR   (0x50)
#define RAW_FACI_CMD_FORCED_STOP    (0xB3)
#define RAW_FACI_WAIT_PE            (100000UL)
#define RAW_FACI_WAIT_ERASE         (20000000UL)
#define RAW_FACI_WAIT_PROGRAM       (5000000UL)

static int rx65n_raw_faci_pe_enter(void)
{
    uint32_t t;

    (void)rx65n_dataflash_init(); /* idempotent: sets FPCKAR/EEPFCLK via FIT open */

    FLASH.FENTRYR.WORD = 0xAA80; /* Data Flash P/E mode */
    for (t = RAW_FACI_WAIT_PE; (FLASH.FENTRYR.WORD != 0x0080) && (t > 0); t--) { }
    if (FLASH.FENTRYR.WORD != 0x0080)
    {
        printf("  [DF_HW] RAW: failed to enter DF P/E mode (FENTRYR=0x%04X)\r\n", (unsigned)FLASH.FENTRYR.WORD);
        return RX65N_DF_ERR_HW;
    }
    FLASH.FWEPROR.BYTE = 0x01;
    return RX65N_DF_SUCCESS;
}

static void rx65n_raw_faci_pe_exit(void)
{
    uint32_t t;

    FLASH.FENTRYR.WORD = 0xAA00;
    for (t = RAW_FACI_WAIT_PE; (FLASH.FENTRYR.WORD != 0x0000) && (t > 0); t--) { }
    FLASH.FWEPROR.BYTE = 0x00;
}

/* Waits for FRDY and checks error flags. On error, releases the sequencer (forced stop + status clear). */
static int rx65n_raw_faci_wait_done(uint32_t timeout, const char *what, uint32_t addr)
{
    volatile uint8_t *cmd8 = (volatile uint8_t *)RAW_FACI_CMD_AREA;
    uint32_t fstatr;
    int      err = RX65N_DF_SUCCESS;

    while ((FLASH.FSTATR.BIT.FRDY != 1) && (timeout > 0))
    {
        timeout--;
    }

    fstatr = FLASH.FSTATR.LONG;
    if ((FLASH.FSTATR.BIT.FRDY != 1) || (FLASH.FASTAT.BIT.CMDLK == 1) || (FLASH.FASTAT.BIT.DFAE == 1) ||
        (FLASH.FSTATR.BIT.ILGLERR == 1) || (FLASH.FSTATR.BIT.ERSERR == 1) || (FLASH.FSTATR.BIT.PRGERR == 1))
    {
        printf("  [DF_HW] RAW %s error @0x%08lX: FSTATR=0x%08lX FASTAT=0x%02X\r\n",
               what, (unsigned long)addr, (unsigned long)fstatr, (unsigned)FLASH.FASTAT.BYTE);
        FLASH.FASTAT.BIT.DFAE = 0;
        *cmd8 = RAW_FACI_CMD_FORCED_STOP;
        for (timeout = 1000000UL; (FLASH.FSTATR.BIT.FRDY != 1) && (timeout > 0); timeout--) { }
        *cmd8 = RAW_FACI_CMD_STATUS_CLEAR;
        for (timeout = 1000000UL; (FLASH.FSTATR.BIT.FRDY != 1) && (timeout > 0); timeout--) { }
        err = RX65N_DF_ERR_HW;
    }
    return err;
}

/* Erase num_blocks 64-byte blocks starting at start_addr (single P/E session). */
static int rx65n_raw_faci_erase(uint32_t start_addr, uint32_t num_blocks)
{
    volatile uint8_t *cmd8 = (volatile uint8_t *)RAW_FACI_CMD_AREA;
    int ret = rx65n_raw_faci_pe_enter();

    if (RX65N_DF_SUCCESS != ret)
    {
        rx65n_raw_faci_pe_exit();
        return ret;
    }

    FLASH.FCPSR.WORD = 0x0001; /* erasure priority mode (as in the FIT) */

    for (uint32_t i = 0; i < num_blocks; i++)
    {
        uint32_t addr = start_addr + (i * RX65N_DF_BLOCK_SIZE);

        FLASH.FSADDR.LONG = addr;
        *cmd8 = RAW_FACI_CMD_BLOCK_ERASE;
        *cmd8 = RAW_FACI_CMD_FINAL;

        ret = rx65n_raw_faci_wait_done(RAW_FACI_WAIT_ERASE, "erase", addr);
        if (RX65N_DF_SUCCESS != ret)
        {
            break;
        }
    }

    rx65n_raw_faci_pe_exit();
    return ret;
}

/* Program byte_count bytes (multiple of 4, flash_addr 4-byte aligned) from src (any alignment). */
static int rx65n_raw_faci_program(uint32_t flash_addr, const uint8_t *src, uint32_t byte_count)
{
    volatile uint8_t  *cmd8  = (volatile uint8_t *)RAW_FACI_CMD_AREA;
    volatile uint16_t *cmd16 = (volatile uint16_t *)RAW_FACI_CMD_AREA;
    int ret = rx65n_raw_faci_pe_enter();

    if (RX65N_DF_SUCCESS != ret)
    {
        rx65n_raw_faci_pe_exit();
        return ret;
    }

    for (uint32_t off = 0; off < byte_count; off += 4)
    {
        uint16_t h[2];
        uint32_t addr = flash_addr + off;

        memcpy(h, &src[off], sizeof(h));

        FLASH.FSADDR.LONG = addr;
        *cmd8 = RAW_FACI_CMD_PROGRAM;
        *cmd8 = RAW_FACI_CMD_PROGRAM_DF_CNT;
        *cmd16 = h[0];
        while (FLASH.FSTATR.BIT.DBFULL == 1) { }
        *cmd16 = h[1];
        while (FLASH.FSTATR.BIT.DBFULL == 1) { }
        *cmd8 = RAW_FACI_CMD_FINAL;

        ret = rx65n_raw_faci_wait_done(RAW_FACI_WAIT_PROGRAM, "program", addr);
        if (RX65N_DF_SUCCESS != ret)
        {
            break;
        }
    }

    rx65n_raw_faci_pe_exit();
    return ret;
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

    if (RX65N_DF_SUCCESS != rx65n_raw_faci_erase(start_addr, num_blocks))
    {
        printf("  [DF_HW] RAW FACI erase failed at 0x%08lX (blocks=%lu)\r\n",
               (unsigned long)start_addr, (unsigned long)num_blocks);
        return RX65N_DF_ERR_HW;
    }

    /* Mark erased blocks in bitmap */
    rx65n_dataflash_mark_erased(start_addr, num_blocks);

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

    /* Erase all blocks with raw FACI commands in a single P/E session */
    if (RX65N_DF_SUCCESS != rx65n_raw_faci_erase(RX65N_DF_START_ADDR, RX65N_DF_TOTAL_BLOCKS))
    {
        printf("  [DF_HW] ERROR: RAW FACI erase of all blocks failed\r\n");
        return RX65N_DF_ERR_HW;
    }
    printf("  [DF_HW] Block 0 (0x%08lX) .. Block %lu (0x%08lX) erased OK\r\n",
           (unsigned long)RX65N_DF_START_ADDR, (unsigned long)(RX65N_DF_TOTAL_BLOCKS - 1),
           (unsigned long)(RX65N_DF_END_ADDR - RX65N_DF_BLOCK_SIZE));

    /* Guarantee flash controller is returned to memory-mapped read mode */
    rx65n_dataflash_ensure_read_mode();

    /* Mark all blocks erased in bitmap */
    memset(s_df_written_map, 0, sizeof(s_df_written_map));
    s_df_map_initialized = true;

    printf("  [DF_HW] All %lu blocks erased successfully. Returned to read mode.\r\n",
           (unsigned long)RX65N_DF_TOTAL_BLOCKS);

    /* Verify virtual read returns clean 0xFFFFFFFF across start of flash */
    {
        uint32_t first_word = 0;
        rx65n_dataflash_read(RX65N_DF_START_ADDR, (uint8_t *)&first_word, sizeof(first_word));
        printf("  [DF_HW] Virtual read check at 0x%08lX: 0x%08lX (Expected: 0xFFFFFFFF)\r\n",
               (unsigned long)RX65N_DF_START_ADDR, (unsigned long)first_word);
    }
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

    /* The RX65N Data Flash cannot reprogram an already programmed word without erasing its
     * 64-byte block, while LevelX relies on NOR semantics (in-place bit clearing). Emulate:
     *  - unprogrammed word      -> program directly (runs of words are batched)
     *  - programmed, same value -> nothing to do
     *  - programmed, new value  -> erase the 64-byte block and reprogram a merged image */
    uint32_t off = 0;
    while (off < byte_count)
    {
        uint32_t addr     = flash_addr + off;
        uint32_t word_idx = (addr - RX65N_DF_START_ADDR) / RX65N_DF_WRITE_UNIT;
        uint32_t new_val;
        int      ret;

        memcpy(&new_val, &src_addr[off], sizeof(new_val));

        if (!rx65n_dataflash_is_word_programmed(word_idx))
        {
            /* Batch a run of consecutive unprogrammed words that actually need programming */
            uint32_t run = 0;
            while (((off + (run * 4UL)) < byte_count))
            {
                uint32_t v;
                uint32_t idx = word_idx + run;
                memcpy(&v, &src_addr[off + (run * 4UL)], sizeof(v));
                if (rx65n_dataflash_is_word_programmed(idx) || (0xFFFFFFFFUL == v))
                {
                    break;
                }
                run++;
            }

            if (0 == run)
            {
                /* Writing 0xFFFFFFFF over an erased word: nothing to program */
                off += 4;
                continue;
            }

            ret = rx65n_raw_faci_program(addr, &src_addr[off], run * 4UL);
            if (RX65N_DF_SUCCESS != ret)
            {
                printf("  [DF_HW] RAW FACI program failed: dest=0x%08lX, bytes=%lu\r\n",
                       (unsigned long)addr, (unsigned long)(run * 4UL));
                return RX65N_DF_ERR_HW;
            }
            for (uint32_t i = 0; i < run; i++)
            {
                rx65n_dataflash_set_word(word_idx + i, true);
            }
            off += run * 4UL;
        }
        else
        {
            uint32_t cur_val;
            uint32_t blk_base;
            uint32_t img[RX65N_DF_WORDS_PER_HW_BLOCK];
            uint32_t first_word;

            memcpy(&cur_val, (const void *)addr, sizeof(cur_val));
            if (cur_val == new_val)
            {
                off += 4;
                continue;
            }

            /* Build merged image of the 64-byte block (virtual read semantics) */
            blk_base   = RX65N_DF_START_ADDR + (((addr - RX65N_DF_START_ADDR) / RX65N_DF_BLOCK_SIZE) * RX65N_DF_BLOCK_SIZE);
            first_word = (blk_base - RX65N_DF_START_ADDR) / RX65N_DF_WRITE_UNIT;
            for (uint32_t i = 0; i < RX65N_DF_WORDS_PER_HW_BLOCK; i++)
            {
                img[i] = rx65n_dataflash_is_word_programmed(first_word + i) ?
                         *(const volatile uint32_t *)(blk_base + (i * 4UL)) : 0xFFFFFFFFUL;
            }
            img[(addr - blk_base) / 4UL] = new_val;

            ret = rx65n_raw_faci_erase(blk_base, 1);
            if (RX65N_DF_SUCCESS != ret)
            {
                printf("  [DF_HW] RAW FACI block rewrite: erase failed at 0x%08lX\r\n", (unsigned long)blk_base);
                return RX65N_DF_ERR_HW;
            }
            rx65n_dataflash_mark_erased(blk_base, 1);

            /* Reprogram every word of the image that is not 0xFFFFFFFF (runs batched) */
            uint32_t i = 0;
            while (i < RX65N_DF_WORDS_PER_HW_BLOCK)
            {
                if (0xFFFFFFFFUL == img[i])
                {
                    i++;
                    continue;
                }
                uint32_t j = i;
                while ((j < RX65N_DF_WORDS_PER_HW_BLOCK) && (0xFFFFFFFFUL != img[j]))
                {
                    j++;
                }
                ret = rx65n_raw_faci_program(blk_base + (i * 4UL), (const uint8_t *)&img[i], (j - i) * 4UL);
                if (RX65N_DF_SUCCESS != ret)
                {
                    printf("  [DF_HW] RAW FACI block rewrite: program failed at 0x%08lX\r\n",
                           (unsigned long)(blk_base + (i * 4UL)));
                    return RX65N_DF_ERR_HW;
                }
                for (uint32_t k = i; k < j; k++)
                {
                    rx65n_dataflash_set_word(first_word + k, true);
                }
                i = j;
            }
            off += 4;
        }
    }

    return RX65N_DF_SUCCESS;
}

/***********************************************************************************************************************
 * Function Name: rx65n_dataflash_read
 * Description  : Reads byte_count data from Data Flash (memory-mapped address) to dst_buf buffer.
 *                Provides virtual NOR 0xFF semantics for unwritten/erased blocks.
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

    /* Ensure flash peripheral is in memory-mapped Read mode (FENTRYR == 0) */
    rx65n_dataflash_ensure_read_mode();

    if (byte_count > 0)
    {
        uint32_t curr_addr = flash_addr;
        uint32_t remaining = byte_count;
        uint8_t *dst       = dst_buf;

        while (remaining > 0)
        {
            uint32_t word_idx    = (curr_addr - RX65N_DF_START_ADDR) / RX65N_DF_WRITE_UNIT;
            uint32_t word_offset = (curr_addr - RX65N_DF_START_ADDR) % RX65N_DF_WRITE_UNIT;
            uint32_t chunk       = RX65N_DF_WRITE_UNIT - word_offset;
            if (chunk > remaining)
            {
                chunk = remaining;
            }

            if (rx65n_dataflash_is_word_programmed(word_idx))
            {
                /* Programmed word: read actual physical flash contents */
                memcpy(dst, (const void *)curr_addr, chunk);
            }
            else
            {
                /* Unprogrammed (erased) word: virtual NOR 0xFF semantics */
                memset(dst, 0xFF, chunk);
            }

            curr_addr += chunk;
            dst += chunk;
            remaining -= chunk;
        }
    }

    return RX65N_DF_SUCCESS;
}

/***********************************************************************************************************************
 * Function Name: rx65n_dataflash_is_block_erased
 * Description  : Checks if a 4KB logical LevelX block (64 physical blocks) is completely erased.
 * Return Value : 1 if erased, 0 if written.
 ***********************************************************************************************************************/
int rx65n_dataflash_is_block_erased(uint32_t logical_block)
{
    if (logical_block >= (RX65N_DF_SIZE / 4096UL))
    {
        return 0;
    }

    uint32_t start_hw_blk = logical_block * (4096UL / RX65N_DF_BLOCK_SIZE);
    uint32_t num_hw_blks  = 4096UL / RX65N_DF_BLOCK_SIZE;

    for (uint32_t b = start_hw_blk; b < (start_hw_blk + num_hw_blks); b++)
    {
        if (rx65n_dataflash_is_hw_block_written(b))
        {
            return 0;
        }
    }

    return 1;
}
