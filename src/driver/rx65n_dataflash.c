/***********************************************************************************************************************
 * File Name    : rx65n_dataflash.c
 * Description  : Low-level driver implementation for Renesas RX65N on-chip Data Flash (32KB).
 ***********************************************************************************************************************/

#include "rx65n_dataflash.h"
#include "r_flash_rx_if.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

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

    printf("  [DF_HW] All %lu blocks erased successfully. Returned to read mode.\r\n",
           (unsigned long)RX65N_DF_TOTAL_BLOCKS);

    /* Physical verification: directly read the first word of the Data Flash */
    {
        uint32_t first_word = *(volatile uint32_t *)RX65N_DF_START_ADDR;
        printf("  [DF_HW] Direct read check at 0x%08lX: 0x%08lX (Expected: 0xFFFFFFFF)\r\n",
               (unsigned long)RX65N_DF_START_ADDR, (unsigned long)first_word);
        printf("  [DF_HW] FENTRYR=0x%04X FWEPROR=0x%02X FASTAT=0x%02X (DFAE=%u CMDLK=%u)\r\n",
               (unsigned)FLASH.FENTRYR.WORD, (unsigned)FLASH.FWEPROR.BYTE,
               (unsigned)FLASH.FASTAT.BYTE, (unsigned)FLASH.FASTAT.BIT.DFAE,
               (unsigned)FLASH.FASTAT.BIT.CMDLK);
        printf("  [DF_HW] FSTATR=0x%08lX (FRDY=%u ILGLERR=%u ERSERR=%u PRGERR=%u FLWEERR=%u) FPCKAR=0x%04X EEPFCLK=%u\r\n",
               (unsigned long)FLASH.FSTATR.LONG, (unsigned)FLASH.FSTATR.BIT.FRDY,
               (unsigned)FLASH.FSTATR.BIT.ILGLERR, (unsigned)FLASH.FSTATR.BIT.ERSERR,
               (unsigned)FLASH.FSTATR.BIT.PRGERR, (unsigned)FLASH.FSTATR.BIT.FLWEERR,
               (unsigned)FLASH.FPCKAR.WORD, (unsigned)FLASH.EEPFCLK);
        /* Repeated reads of the same words: a stable value means stored data (erase not applied),
         * a changing value means the bus is not returning real array data (unreadable / floating). */
        for (uint32_t i = 0; i < 4; i++)
        {
            volatile uint32_t *p = (volatile uint32_t *)(RX65N_DF_START_ADDR + (i * 4UL));
            uint32_t r0 = p[0];
            uint32_t r1 = p[0];
            uint32_t r2 = p[0];
            printf("  [DF_HW] read[%lu]: 0x%08lX 0x%08lX 0x%08lX (%s)\r\n",
                   (unsigned long)i, (unsigned long)r0, (unsigned long)r1, (unsigned long)r2,
                   ((r0 == r1) && (r1 == r2)) ? "stable" : "UNSTABLE");
        }

        /* FACI blank check does not depend on CPU bus reads: it tells whether the array is really erased. */
        {
            flash_res_t bc_res = FLASH_RES_NOT_BLANK;
            flash_err_t bc_err = R_FLASH_BlankCheck(RX65N_DF_START_ADDR, RX65N_DF_BLOCK_SIZE, &bc_res);
            printf("  [DF_HW] FACI BlankCheck(blk0, 64B): err=%d result=%s\r\n", (int)bc_err,
                   (FLASH_RES_BLANK == bc_res) ? "BLANK" : "NOT_BLANK");
            bc_res = FLASH_RES_NOT_BLANK;
            bc_err = R_FLASH_BlankCheck(RX65N_DF_START_ADDR, RX65N_DF_SIZE, &bc_res);
            printf("  [DF_HW] FACI BlankCheck(all, 32KB): err=%d result=%s\r\n", (int)bc_err,
                   (FLASH_RES_BLANK == bc_res) ? "BLANK" : "NOT_BLANK");
            rx65n_dataflash_ensure_read_mode();
        }

        /* Re-read after a settling delay to rule out a read-mode transition wait time. */
        R_BSP_SoftwareDelay(1, BSP_DELAY_MILLISECS);
        printf("  [DF_HW] re-read after 1ms: 0x%08lX\r\n",
               (unsigned long)(*(volatile uint32_t *)RX65N_DF_START_ADDR));

        /* Other addresses and access widths (byte / halfword / word) */
        {
            static const uint32_t probe[] = { 0x00100040UL, 0x00101000UL, 0x00104000UL, 0x00107FC0UL, 0x00107FFCUL };
            for (uint32_t i = 0; i < (sizeof(probe) / sizeof(probe[0])); i++)
            {
                printf("  [DF_HW] probe 0x%08lX: w=0x%08lX\r\n", (unsigned long)probe[i],
                       (unsigned long)(*(volatile uint32_t *)probe[i]));
            }
            printf("  [DF_HW] width probe @0x00100000: b=%02X %02X %02X %02X h=%04X %04X\r\n",
                   (unsigned)(*(volatile uint8_t *)0x00100000UL), (unsigned)(*(volatile uint8_t *)0x00100001UL),
                   (unsigned)(*(volatile uint8_t *)0x00100002UL), (unsigned)(*(volatile uint8_t *)0x00100003UL),
                   (unsigned)(*(volatile uint16_t *)0x00100000UL), (unsigned)(*(volatile uint16_t *)0x00100002UL));
        }


        if (first_word != 0xFFFFFFFFUL)
        {
            printf("  [DF_HW] WARNING: erase not reflected in hardware (first word != 0xFFFFFFFF)\r\n");
        }
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

    /* Program with raw FACI commands (proven on hardware). Source data is copied halfword-wise via
     * memcpy so the source buffer needs no particular alignment. */
    int ret = rx65n_raw_faci_program(flash_addr, src_addr, byte_count);
    if (RX65N_DF_SUCCESS != ret)
    {
        printf("  [DF_HW] RAW FACI program failed: dest=0x%08lX, bytes=%lu\r\n",
               (unsigned long)flash_addr, (unsigned long)byte_count);
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

    /* Ensure flash peripheral is in memory-mapped Read mode (FENTRYR == 0) */
    rx65n_dataflash_ensure_read_mode();

    if (byte_count > 0)
    {
        /* Memory-mapped read */
        memcpy(dst_buf, (const void *)flash_addr, byte_count);
    }

    return RX65N_DF_SUCCESS;
}
