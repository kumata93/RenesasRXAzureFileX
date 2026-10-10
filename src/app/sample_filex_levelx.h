/***********************************************************************************************************************
 * File Name    : sample_filex_levelx.h
 * Description  : Sample application header for FileX + LevelX NOR Flash on RX65N Data Flash.
 ***********************************************************************************************************************/

#ifndef SAMPLE_FILEX_LEVELX_H
#define SAMPLE_FILEX_LEVELX_H

#include "tx_api.h"
#include "fx_api.h"
#include "lx_api.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Total FileX logical sectors mapped through LevelX (LevelX has 45 usable logical sectors on 32KB NOR) */
#define SAMPLE_TOTAL_SECTORS            (32UL)
#define SAMPLE_SECTOR_SIZE              (512UL)

/* Test file parameters */
#define SAMPLE_FILE_NAME                "CONFIG.TXT"

/**
 * @brief Main demo entry point for thread.
 */
void demo_thread_entry(ULONG thread_input);

#ifdef __cplusplus
}
#endif

#endif /* SAMPLE_FILEX_LEVELX_H */
