/**
 * @file    sdls.c
 * @brief   Space Data Link Security (SDLS) support library.
 *
 * @addtogroup CCSDS
 * @{
 */
#include "sdls.h"

/*===========================================================================*/
/* Local definitions.                                                        */
/*===========================================================================*/

/*===========================================================================*/
/* Exported variables.                                                       */
/*===========================================================================*/

/*===========================================================================*/
/* Local variables and types.                                                */
/*===========================================================================*/

/*===========================================================================*/
/* Local functions.                                                          */
/*===========================================================================*/

/*===========================================================================*/
/* Interface implementation.                                                 */
/*===========================================================================*/

/*===========================================================================*/
/* Exported functions.                                                       */
/*===========================================================================*/

int sdls_send(const sdls_cfg_t *cfg, fb_t *fb)
{
    int ret = 0;
    uint16_t *spi = NULL;
    void *iv = NULL;
    void *seq_num = NULL;
    void *mac = NULL;

    fb_push(fb, cfg->pad_len);
    seq_num = fb_push(fb, cfg->seq_num_len);
    iv = fb_push(fb, cfg->iv_len);
    spi = fb_push(fb, sizeof(uint16_t));
    mac = fb_put(fb, cfg->mac_len);
    *spi = __builtin_bswap16(cfg->spi);

    if (cfg->send_func != NULL) {
        ret = cfg->send_func(fb->data, fb->len - cfg->mac_len, iv, seq_num, mac, cfg->send_arg);
    }

    return ret;
}

int sdls_recv(const sdls_cfg_t *cfg, fb_t *fb)
{
    int ret = 0;
    /* Create header and trailer structs based on configured SPI */
    struct __attribute__((packed)) sdls_hdr {
        uint16_t    spi;
        uint8_t     iv[cfg->iv_len];
        uint8_t     seq_num[cfg->seq_num_len];
        uint8_t     pad[cfg->pad_len];
    } *hdr;
    struct __attribute__((packed)) sdls_tlr {
        uint8_t     mac[cfg->mac_len];
    } *tlr;

    hdr = (struct sdls_hdr*)fb->data;
    tlr = (struct sdls_tlr*)(fb->tail - cfg->mac_len);

    if (__builtin_bswap16(hdr->spi) != cfg->spi) {
        return -1;
    }

    if (cfg->recv_func != NULL) {
        ret = cfg->recv_func(fb->data, fb->len - cfg->mac_len, hdr->iv, hdr->seq_num, tlr->mac, cfg->recv_arg);
    }

    fb_pull(fb, sizeof(struct sdls_hdr));
    fb_trim(fb, sizeof(struct sdls_tlr));
    return ret;
}

/** @} */
