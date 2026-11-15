/* Minimal PNG writer: RGBA8, filter 0, stored (uncompressed) deflate blocks. No external libs. */
#include "png.h"
#include "util.h"

static uint32_t crc_table[256];
static int crc_ready;

static void crc_init(void) {
    for (uint32_t n = 0; n < 256; n++) {
        uint32_t c = n;
        for (int k = 0; k < 8; k++) c = (c & 1) ? 0xEDB88320u ^ (c >> 1) : c >> 1;
        crc_table[n] = c;
    }
    crc_ready = 1;
}
static uint32_t crc_update(uint32_t crc, const uint8_t *p, size_t n) {
    if (!crc_ready) crc_init();
    crc = ~crc;
    while (n--) crc = crc_table[(crc ^ *p++) & 0xFF] ^ (crc >> 8);
    return ~crc;
}
static void put32be(uint8_t *p, uint32_t v) {
    p[0] = v >> 24; p[1] = v >> 16; p[2] = v >> 8; p[3] = v;
}
static void chunk(FILE *f, const char *type, const uint8_t *data, uint32_t len) {
    uint8_t hdr[8];
    put32be(hdr, len);
    memcpy(hdr + 4, type, 4);
    fwrite(hdr, 1, 8, f);
    uint32_t crc = crc_update(0, (const uint8_t *)type, 4);
    if (len) {
        fwrite(data, 1, len, f);
        crc = crc_update(crc, data, len);
    }
    uint8_t tail[4];
    put32be(tail, crc);
    fwrite(tail, 1, 4, f);
}

int png_write_rgba(const char *path, int w, int h, const uint8_t *rgba) {
    FILE *f = fopen(path, "wb");
    if (!f) return -1;
    static const uint8_t sig[8] = {0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A};
    fwrite(sig, 1, 8, f);
    uint8_t ihdr[13];
    put32be(ihdr, (uint32_t)w);
    put32be(ihdr + 4, (uint32_t)h);
    ihdr[8] = 8; ihdr[9] = 6; ihdr[10] = 0; ihdr[11] = 0; ihdr[12] = 0;
    chunk(f, "IHDR", ihdr, 13);

    size_t rawlen = (size_t)h * (1 + (size_t)w * 4);
    uint8_t *raw = xmalloc(rawlen ? rawlen : 1);
    for (int y = 0; y < h; y++) {
        raw[(size_t)y * (1 + (size_t)w * 4)] = 0;
        memcpy(raw + (size_t)y * (1 + (size_t)w * 4) + 1, rgba + (size_t)y * w * 4, (size_t)w * 4);
    }
    size_t nblocks = rawlen / 65535 + 1;
    size_t zlen = 2 + rawlen + nblocks * 5 + 4;
    uint8_t *z = xmalloc(zlen);
    size_t zp = 0;
    z[zp++] = 0x78; z[zp++] = 0x01;
    size_t off = 0;
    uint32_t a = 1, b = 0;
    do {
        size_t n = rawlen - off;
        if (n > 65535) n = 65535;
        int last = (off + n >= rawlen);
        z[zp++] = last ? 1 : 0;
        z[zp++] = n & 0xFF; z[zp++] = n >> 8;
        z[zp++] = ~n & 0xFF; z[zp++] = (~n >> 8) & 0xFF;
        memcpy(z + zp, raw + off, n);
        for (size_t i = 0; i < n; i++) { a = (a + raw[off + i]) % 65521; b = (b + a) % 65521; }
        zp += n; off += n;
        if (last) break;
    } while (1);
    put32be(z + zp, (b << 16) | a); zp += 4;
    chunk(f, "IDAT", z, (uint32_t)zp);
    chunk(f, "IEND", NULL, 0);
    free(raw); free(z);
    fclose(f);
    return 0;
}
