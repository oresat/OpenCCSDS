/**
 * @file    sdls.h
 * @brief   Space Data Link Security (SDLS) support library.
 *
 * @addtogroup CCSDS
 * @{
 */
#ifndef _SDLS_H_
#define _SDLS_H_

#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include "frame_buf.h"

/*===========================================================================*/
/* Constants.                                                                */
/*===========================================================================*/

/*===========================================================================*/
/* Pre-compile time settings.                                                */
/*===========================================================================*/

/*===========================================================================*/
/* Derived constants and error checks.                                       */
/*===========================================================================*/

/*===========================================================================*/
/* Data structures and types.                                                */
/*===========================================================================*/

typedef int (*spi_func_t)(void *data, size_t len, void *iv, void *seq_num, void *mac, void *arg);

/**
 * @name    SDLS Security Parameter Index Configuration
 * @{
 */
typedef struct __attribute__((packed)) {
    uint16_t    spi;            /** Security Parameter Index                 */
    size_t      iv_len;         /** Initialization Vector Length             */
    size_t      seq_num_len;    /** Sequence Number Length                   */
    size_t      pad_len;        /** Pad Length                               */
    size_t      mac_len;        /** MAC Length                               */
    spi_func_t  send_func;      /** Send Function                            */
    void        *send_arg;      /** Send Function Optional Arg               */
    spi_func_t  recv_func;      /** Receive Function                         */
    void        *recv_arg;      /** Receive Function Optional Arg            */
} sdls_cfg_t;
/** @} */

/*===========================================================================*/
/* Macros.                                                                   */
/*===========================================================================*/

/*===========================================================================*/
/* External declarations.                                                    */
/*===========================================================================*/

#ifdef __cplusplus
extern "C" {
#endif

int sdls_send(const sdls_cfg_t *cfg, fb_t *fb);
int sdls_recv(const sdls_cfg_t *cfg, fb_t *fb);

#ifdef __cplusplus
}
#endif

#endif /* _SDLS_H_ */

/** @} */
