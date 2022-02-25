/**
 * @file    uslp.c
 * @brief   Unified Space Data Link Protocol (USLP) support library.
 *
 * @addtogroup CCSDS
 * @{
 */
#include <string.h>

#include "uslp.h"
#include "cop.h"

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

/**
 * Generate USLP TFPH ID field
 */
static inline uint32_t uslp_gen_id(uint16_t scid, uint8_t vcid, uint8_t mapid, bool owner)
{
    return __builtin_bswap32(
                USLP_TFVN   << USLP_TFPH_ID_TFVN_Pos |
                scid        << USLP_TFPH_ID_SCID_Pos |
                vcid        << USLP_TFPH_ID_VCID_Pos |
                mapid       << USLP_TFPH_ID_MAPID_Pos|
                (owner ? 0 : USLP_TFPH_ID_SRC_DST));
}

/**
 * Parse USLP TFPH ID field into component pieces
 */
static inline bool uslp_parse_id(uint32_t id, uint16_t scid_match, uint16_t *scid, uint8_t *vcid, uint8_t *mapid, bool owner)
{
    id = __builtin_bswap32(id);
    /* Must be USLP frame */
    if ((id & USLP_TFPH_ID_TFVN) >> USLP_TFPH_ID_TFVN_Pos != USLP_TFVN)
        return false;
    /* Must match SCID */
    *scid = (id & USLP_TFPH_ID_SCID) >> USLP_TFPH_ID_SCID_Pos;
    if (*scid != scid_match)
        return false;
    if (owner != !!(id & USLP_TFPH_ID_SRC_DST))
        return false;
    /* Parse VC and MAP */
    *vcid = (id & USLP_TFPH_ID_VCID) >> USLP_TFPH_ID_VCID_Pos;
    *mapid = (id & USLP_TFPH_ID_MAPID) >> USLP_TFPH_ID_MAPID_Pos;
    return true;
}

/**
 * Parse USLP TFDF Header into component pieces
 */
static inline void uslp_parse_tfdf_hdr(uslp_tfdf_hdr_t *hdr, uint8_t *rules, uslp_pid_t *upid, uint16_t *offset)
{
    *rules = (hdr->flags & USLP_TFDF_HDR_TFDZ_RULES) >> USLP_TFDF_HDR_TFDZ_RULES_Pos;
    *upid = (hdr->flags & USLP_TFDF_HDR_UPID) >> USLP_TFDF_HDR_UPID_Pos;
    *offset = __builtin_bswap16(hdr->offset);
}

/**
 * Generate MAP components of frame
 */
static void *uslp_map_gen(const uslp_map_t *map, fb_t *fb)
{
    uslp_tfdf_hdr_t *tfdf_hdr;

    /* TODO: Currently only handles unsegmented data, implement remaining rules */
    if (fb->len > map->max_pkt_len)
        return NULL;

    tfdf_hdr = fb_push(fb, 1);
    tfdf_hdr->flags = map->upid << USLP_TFDF_HDR_UPID_Pos;
    if (map->sdu == SDU_MAP_OCTET_STREAM) {
        tfdf_hdr->flags |= USLP_TFDZ_RULES_OCTET_STREAM << USLP_TFDF_HDR_TFDZ_RULES_Pos;
    } else {
        tfdf_hdr->flags |= USLP_TFDZ_RULES_NO_SEG << USLP_TFDF_HDR_TFDZ_RULES_Pos;
    }

    return tfdf_hdr;
}

/**
 * Generate VC components of frame
 */
static void *uslp_vc_gen(const uslp_vc_t *vc, fb_t *fb, bool expedite)
{
    uslp_tfph_t *tfph;
    uint8_t flags = 0;

    switch (vc->cop) {
    case COP_1:
        flags = cop_fop1(vc, fb, expedite);
        break;
    case COP_P:
        break;
    case COP_NONE:
    default:
        break;
    }
    tfph = fb_push(fb, sizeof(uslp_tfph_t));
    tfph->flags = flags;

    return tfph;
}

static uint16_t uslp_fecf_gen(const uslp_pc_t *pc, fb_t *fb)
{
    uint16_t len = fb->len;
    uint32_t *crc;
    switch (pc->fecf) {
    case FECF_SW:
        crc = fb_put(fb, pc->fecf_len);
        memset(crc, 0, pc->fecf_len);
        if (pc->crc) {
            /* TODO: Verify this implements correctly */
            pc->crc(fb->data, len, crc);
            if (pc->fecf_len == 2) {
                *crc = __builtin_bswap16(*crc);
            } else if (pc->fecf_len == 4) {
                *crc = __builtin_bswap32(*crc);
            }
        }
        __attribute__((fallthrough));
    case FECF_HW:
        len += pc->fecf_len;
    case FECF_NONE:
    default:
        break;
    }
    return __builtin_bswap16(len);
}

static bool uslp_fecf_recv(const uslp_pc_t *pc, fb_t *fb)
{
    uint16_t len = fb->len - pc->fecf_len;
    uint32_t crc = 0;
    switch (pc->fecf) {
    case FECF_SW:
        /* TODO: Verify this implements correctly */
        if (pc->crc) {
            pc->crc(fb->data, len, &crc);
            if (pc->fecf_len == 2) {
                crc = __builtin_bswap16(crc);
            } else if (pc->fecf_len == 4) {
                crc = __builtin_bswap32(crc);
            }
            if (memcmp(&crc, &fb->data[len], pc->fecf_len)) {
                return false;
            }
        }
        __attribute__((fallthrough));
    case FECF_HW:
        fb_trim(fb, pc->fecf_len);
    case FECF_NONE:
    default:
        break;
    }
    return true;
}

static void uslp_vc_recv(const uslp_vc_t *vc, fb_t *fb)
{
    (void)vc;
    uslp_tfph_t *tfph = (uslp_tfph_t*)fb->data;
    size_t vcf_cnt_len;

    if (__builtin_bswap32(tfph->id) & USLP_TFPH_ID_EOFPH) {
        fb_pull(fb, sizeof(uint32_t));
        return;
    }

    vcf_cnt_len = (tfph->flags & USLP_TFPH_FLAGS_VCF_CNT_LEN) >> USLP_TFPH_FLAGS_VCF_CNT_LEN_Pos;

#if (0)
    /* TODO: Implement FARM */

#endif

    fb_pull(fb, sizeof(uslp_tfph_t) + vcf_cnt_len);
}

static void uslp_map_recv(const uslp_map_t *map, fb_t *fb)
{
    uslp_tfdf_hdr_t *tfdf_hdr = (uslp_tfdf_hdr_t*)fb->data;
    uslp_pid_t upid;
    uint16_t offset;
    uint8_t rules;

    /* Extract TFDF Header */
    uslp_parse_tfdf_hdr(tfdf_hdr, &rules, &upid, &offset);
    if (upid != map->upid)
        return;

    switch (rules) {
    case USLP_TFDZ_RULES_OCTET_STREAM:
    case USLP_TFDZ_RULES_NO_SEG:
        fb_pull(fb, 1);
        break;
    default:
        /* TODO: Currently only handles unsegmented data, implement remaining rules */
        return;
    }

    if (map->map_recv)
        map->map_recv(fb, map->map_arg);
}

/*===========================================================================*/
/* Interface implementation.                                                 */
/*===========================================================================*/

/*===========================================================================*/
/* Exported functions.                                                       */
/*===========================================================================*/

int uslp_map_send(const uslp_link_t *link, fb_t *fb, uint8_t vcid, uint8_t mapid, bool expedite)
{
    uslp_tfph_t *tfph;
    const uslp_pc_t *pc = link->pc_tx;
    const uslp_mc_t *mc = link->mc;
    const uslp_vc_t *vc = mc->vcid[vcid];
    const uslp_map_t *map = vc->mapid[mapid];

    uslp_map_gen(map, fb);
#if USLP_USE_SDLS == 1
    if (vc->sdls_cfg != NULL) {
        sdls_send(vc->sdls_cfg, fb);
    }
#endif
    tfph = uslp_vc_gen(vc, fb, expedite);
    tfph->id = uslp_gen_id(mc->scid, vcid, mapid, mc->owner);
    tfph->len = uslp_fecf_gen(pc, fb);
    pc->phy_send(fb, pc->send_arg);

    return 0;
}

int uslp_mc_ocf_send(const uslp_link_t *link, fb_t *fb, uint8_t vcid)
{
    const uslp_pc_t *pc = link->pc_tx;
    const uslp_mc_t *mc = link->mc;
    const uslp_vc_t *vc = mc->vcid[vcid];
    (void)pc;
    (void)mc;
    (void)vc;
    (void)fb;

    return 0;
}

int uslp_cop_send(const uslp_link_t *link, fb_t *fb, uint8_t vcid)
{
    const uslp_pc_t *pc = link->pc_tx;
    const uslp_mc_t *mc = link->mc;
    const uslp_vc_t *vc = mc->vcid[vcid];
    (void)pc;
    (void)mc;
    (void)vc;
    (void)fb;

    return 0;
}

int uslp_vcf_send(const uslp_link_t *link, fb_t *fb, uint8_t vcid)
{
    const uslp_pc_t *pc = link->pc_tx;
    const uslp_mc_t *mc = link->mc;
    const uslp_vc_t *vc = mc->vcid[vcid];
    (void)pc;
    (void)mc;
    (void)vc;
    (void)fb;

    return 0;
}

int uslp_mcf_send(const uslp_link_t *link, fb_t *fb)
{
    const uslp_pc_t *pc = link->pc_tx;
    const uslp_mc_t *mc = link->mc;
    (void)pc;
    (void)mc;
    (void)fb;

    return 0;
}

bool uslp_recv(const uslp_link_t *link, fb_t *fb)
{
    uslp_tfph_t *tfph = (uslp_tfph_t*)fb->data;
    const uslp_pc_t *pc = link->pc_rx;
    const uslp_mc_t *mc = link->mc;
    const uslp_vc_t *vc;
    const uslp_map_t *map;
    uint16_t scid;
    uint8_t vcid, mapid;

    if (fb->len < sizeof(uint32_t) + pc->fecf_len)
        return false;

    /* Verify CRC */
    if (!uslp_fecf_recv(pc, fb))
        return false;

    /* Parse out IDs */
    if (!uslp_parse_id(tfph->id, mc->scid, &scid, &vcid, &mapid, mc->owner))
        return false;

    /* Insert Service */
    if (pc->insert_zone) {
        /* TODO: Extract IN_SDU */
        if (mc->insert_recv)
            mc->insert_recv(fb, mc->insert_arg);
    }

    /* Demux MC */
    /* TODO: Handle multiple master channels */

    /* MCF Service */
    if (mc->mcf_recv)
        mc->mcf_recv(fb, mc->mcf_arg);

    /* USLP_MC_OCF Service */
    /* TODO: Handle MC_OCF */

    /* Demux VC */
    vc = mc->vcid[vcid];
    if (vc == NULL)
        return false;

    /* VCF Service */
    if (vc->vcf_recv)
        vc->vcf_recv(fb, vc->vcf_arg);

    /* VC Reception */
    uslp_vc_recv(vc, fb);

#if USLP_USE_SDLS == 1
    /* SDLS Receive */
    if (vc->sdls_cfg != NULL) {
        if (sdls_recv(vc->sdls_cfg, fb) != 0) {
            /* TODO: Real error codes */
            return false;
        }
    }
#endif

    /* Demux MAP */
    map = vc->mapid[mapid];
    if (map == NULL)
        return false;

    /* MAP Reception/Extraction */
    uslp_map_recv(map, fb);
    return true;
}
/** @} */
