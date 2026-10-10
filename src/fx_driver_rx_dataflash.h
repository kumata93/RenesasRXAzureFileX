/***********************************************************************************************************************
 * File Name    : fx_driver_rx_dataflash.h
 * Description  : FileX I/O driver header for Renesas RX65N on-chip Data Flash (32KB).
 ***********************************************************************************************************************/

#ifndef FX_DRIVER_RX_DATAFLASH_H
#define FX_DRIVER_RX_DATAFLASH_H

#include "fx_api.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief FileX I/O Driver entry function for Renesas RX65N on-chip Data Flash.
 * @param media_ptr Pointer to the FileX media control block.
 */
VOID fx_driver_rx_dataflash(FX_MEDIA *media_ptr);

#ifdef __cplusplus
}
#endif

#endif /* FX_DRIVER_RX_DATAFLASH_H */
