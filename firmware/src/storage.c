/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* Persistent storage on the SPI NOR.
 *
 * Every object has an A/B pair of copies, one sector each (a project: ST_PROJ_SPAN
 * sectors in a row). A save goes to the copy that is not the current one: erase
 * its sectors, program the payload (from offset 256), then the 32-byte header at
 * offset 0 last. The header is the commit record; on load the valid copy with the
 * highest seq wins, so a torn write leaves the previous copy in charge.
 *
 * Flash access goes through three hooks (also used by the host test):
 *   st_read(off, dst, n)   st_erase(off)   st_prog(off, src, n)
 */
#define ST_MAGIC 0x554C4546u                   /* "FELU" */
#define ST_SECTOR 4096u
#define ST_PAYLOAD_OFF 256u
#define ST_PAYLOAD_MAX (ST_SECTOR - ST_PAYLOAD_OFF)   /* of a one-sector object */
#define ST_PROJ_SPAN 5u                        /* sectors in a copy of a project (four tracks of patterns) */

/* flash map (FL_DATA 0x97000..0xDFFFF, FL_GLOB 0xFC000..): settings 0xFC000, the user sample slot
 * 0xA0000..0xB3FFF (eng_sample.c), projects 0xB4000..0xDBFFF (where sample slots 2 and 3 were),
 * user preset banks 0xDC000..0xDFFFF (upreset.c), the FM6 user bank in 0x9F000 and 0xFE000
 * (fm6_store.c). 0x97000..0x9EFFF: the one-sector projects of formats 1..5 (OBJ_LEGACY0), read
 * only (project.c converts them; a save goes to OBJ_PROJECT0). The numbers are the header types. */
enum { OBJ_SETTINGS, OBJ_LEGACY0, OBJ_UPRESET0 = OBJ_LEGACY0 + 4, OBJ_FM6 = OBJ_UPRESET0 + 2,
       OBJ_PROJECT0, OBJ_COUNT = OBJ_PROJECT0 + 4 };

typedef struct {
    uint32_t magic;
    uint16_t type, slot;
    uint32_t seq, len, crc, rsv[2];
    uint32_t hcrc;
} st_hdr_t;

static int st_read(uint32_t off, void *dst, uint32_t n);
static int st_erase(uint32_t off);
static int st_prog(uint32_t off, const void *src, uint32_t n);

static uint32_t st_crc_add(uint32_t c, const void *p, uint32_t n)   /* zlib CRC-32, 4 bits per step */
{
    static const uint32_t T[16] = {
        0x00000000u, 0x1DB71064u, 0x3B6E20C8u, 0x26D930ACu, 0x76DC4190u, 0x6B6B51F4u, 0x4DB26158u, 0x5005713Cu,
        0xEDB88320u, 0xF00F9344u, 0xD6D6A3E8u, 0xCB61B38Cu, 0x9B64C2B0u, 0x86D3D2D4u, 0xA00AE278u, 0xBDBDF21Cu};
    const uint8_t *b = p;
    while (n--) {
        c ^= *b++;
        c = (c >> 4) ^ T[c & 15u];
        c = (c >> 4) ^ T[c & 15u];
    }
    return c;
}
static uint32_t st_crc32(const void *p, uint32_t n) { return ~st_crc_add(0xFFFFFFFFu, p, n); }

static uint32_t st_span(uint32_t obj) { return obj >= OBJ_PROJECT0 ? ST_PROJ_SPAN : 1u; }
static uint32_t st_max(uint32_t obj) { return st_span(obj) * ST_SECTOR - ST_PAYLOAD_OFF; }   /* payload bytes */

static uint32_t st_sector(uint32_t obj, uint32_t copy)  /* flash offset of copy A (0) / B (1): its first sector */
{
    if (obj >= OBJ_PROJECT0)
        return 0xB4000u + ((obj - OBJ_PROJECT0) * 2u + copy) * ST_PROJ_SPAN * ST_SECTOR;
    if (obj == OBJ_SETTINGS)
        return 0xFC000u + copy * ST_SECTOR;
    if (obj == OBJ_FM6)
        return copy ? 0xFE000u : 0x9F000u;
    if (obj >= OBJ_UPRESET0)
        return 0xDC000u + (obj - OBJ_UPRESET0) * 2u * ST_SECTOR + copy * ST_SECTOR;
    return 0x97000u + (obj - OBJ_LEGACY0) * 2u * ST_SECTOR + copy * ST_SECTOR;
}

static uint8_t st_buf[256] __attribute__((aligned(4)));   /* one page: CRC reads, and program sources (RAM) */

static int st_head(uint32_t obj, uint32_t copy, st_hdr_t *h)   /* commit record valid: 0 */
{
    if (st_read(st_sector(obj, copy), h, sizeof *h))
        return -1;
    if (h->magic != ST_MAGIC || h->type != obj || h->len > st_max(obj) ||
        h->hcrc != st_crc32(h, sizeof *h - 4u))
        return -1;
    return 0;
}

static int st_body(uint32_t obj, uint32_t copy, const st_hdr_t *h)   /* payload CRC ok: 0 */
{
    uint32_t off, base = st_sector(obj, copy) + ST_PAYLOAD_OFF, c = 0xFFFFFFFFu;
    for (off = 0; off < h->len; off += sizeof st_buf) {
        uint32_t n = h->len - off > sizeof st_buf ? sizeof st_buf : h->len - off;
        if (st_read(base + off, st_buf, n))
            return -1;
        c = st_crc_add(c, st_buf, n);
    }
    return ~c == h->crc ? 0 : -1;
}

/* the current copy: the valid one with the highest seq (A on a tie), -1 when
 * neither is valid. Headers first, so only the winner's payload is checked;
 * *h gets its header. */
static int st_current(uint32_t obj, st_hdr_t *h)
{
    st_hdr_t a, b;
    int va = st_head(obj, 0, &a) == 0, vb = st_head(obj, 1, &b) == 0;
    if (vb && (!va || b.seq > a.seq)) {
        if (st_body(obj, 1, &b) == 0) {
            *h = b;
            return 1;
        }
        vb = 0;
    }
    if (va && st_body(obj, 0, &a) == 0) {
        *h = a;
        return 0;
    }
    if (vb && st_body(obj, 1, &b) == 0) {
        *h = b;
        return 1;
    }
    return -1;
}

/* load object into dst (up to max bytes); returns the length, or -1 */
static int st_load(uint32_t obj, void *dst, uint32_t max)
{
    st_hdr_t h;
    int c = st_current(obj, &h);
    if (c < 0)
        return -1;
    if (h.len > max)
        h.len = max;
    if (st_read(st_sector(obj, (uint32_t)c) + ST_PAYLOAD_OFF, dst, h.len))
        return -1;
    return (int)h.len;
}

static int st_save(uint32_t obj, const void *src, uint32_t len)
{
    uint32_t seq, base, off, crc = 0xFFFFFFFFu;
    int cur, rc;
    st_hdr_t h;
    if (len > st_max(obj))
        return -1;
    cur = st_current(obj, &h);
    seq = cur < 0 ? 0u : h.seq;
    base = st_sector(obj, cur == 0 ? 1u : 0u);       /* write the other copy */
    for (off = 0; off < st_span(obj); off++)         /* (its header sector first: no longer valid) */
        if ((rc = st_erase(base + off * ST_SECTOR)) != 0)
            return rc;
    for (off = 0; off < len; off += sizeof st_buf) {
        uint32_t n = len - off > sizeof st_buf ? sizeof st_buf : len - off, i;
        for (i = 0; i < n; i++)
            st_buf[i] = ((const uint8_t *)src)[off + i];   /* the driver wants RAM sources */
        crc = st_crc_add(crc, st_buf, n);
        if ((rc = st_prog(base + ST_PAYLOAD_OFF + off, st_buf, n)) != 0)
            return rc;
    }
    h.magic = ST_MAGIC;
    h.type = (uint16_t)obj;
    h.slot = (uint16_t)(cur == 0 ? 1 : 0);
    h.seq = seq + 1u;
    h.len = len;
    h.crc = ~crc;
    h.rsv[0] = h.rsv[1] = 0xFFFFFFFFu;
    h.hcrc = st_crc32(&h, sizeof h - 4u);
    if ((rc = st_prog(base, &h, sizeof h)) != 0)       /* the commit record, last */
        return rc;
    {   /* read back: a write-protected or failing part must not report SAVED */
        st_hdr_t chk;
        uint32_t c = cur == 0 ? 1u : 0u;
        if (st_head(obj, c, &chk) || chk.seq != h.seq || st_body(obj, c, &chk))
            return -7;
    }
    return 0;
}
