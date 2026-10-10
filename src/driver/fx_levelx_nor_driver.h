/***********************************************************************************************************************
 * File Name    : fx_levelx_nor_driver.h
 * Description  : FileX to LevelX NOR Flash adapter driver header.
 ***********************************************************************************************************************/

#ifndef FX_LEVELX_NOR_DRIVER_H
#define FX_LEVELX_NOR_DRIVER_H

#include "fx_api.h"
#include "lx_api.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Global LevelX NOR Flash instance */
extern LX_NOR_FLASH g_nor_flash;

/**
 * @brief FileX device driver entry point for LevelX NOR Flash.
 * @param media_ptr Pointer to FileX media control block.
 */
VOID fx_levelx_nor_driver(FX_MEDIA *media_ptr);

#ifdef __cplusplus
}
#endif

#endif /* FX_LEVELX_NOR_DRIVER_H */
