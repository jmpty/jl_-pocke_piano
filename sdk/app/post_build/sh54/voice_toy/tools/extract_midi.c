/* Extract embedded .mid from existing dir_midi pack. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

int main(int argc, char **argv)
{
    FILE *fp;
    long sz;
    uint8_t *buf;
    uint32_t off;
    uint32_t len;
    const char *out;

    if (argc != 3) {
        fprintf(stderr, "usage: %s <dir_midi> <out.mid>\n", argv[0]);
        return 1;
    }

    fp = fopen(argv[1], "rb");
    if (!fp) {
        return 1;
    }
    fseek(fp, 0, SEEK_END);
    sz = ftell(fp);
    rewind(fp);
    buf = (uint8_t *)malloc((size_t)sz);
    fread(buf, 1, (size_t)sz, fp);
    fclose(fp);

    if (sz < 64) {
        return 1;
    }

    off = (uint32_t)buf[4] | ((uint32_t)buf[5] << 8) |
          ((uint32_t)buf[6] << 16) | ((uint32_t)buf[7] << 24);
    len = (uint32_t)buf[8] | ((uint32_t)buf[9] << 8) |
          ((uint32_t)buf[10] << 16) | ((uint32_t)buf[11] << 24);

    /* first file entry at offset 32 */
    off = (uint32_t)buf[36] | ((uint32_t)buf[37] << 8) |
          ((uint32_t)buf[38] << 16) | ((uint32_t)buf[39] << 24);
    len = (uint32_t)buf[40] | ((uint32_t)buf[41] << 8) |
          ((uint32_t)buf[42] << 16) | ((uint32_t)buf[43] << 24);

    if (off + len > (uint32_t)sz) {
        fprintf(stderr, "bad entry off=%u len=%u size=%ld\n", off, len, sz);
        free(buf);
        return 1;
    }

    out = argv[2];
    fp = fopen(out, "wb");
    if (!fp) {
        free(buf);
        return 1;
    }
    fwrite(buf + off, 1, len, fp);
    fclose(fp);
    free(buf);
    printf("extracted %u bytes -> %s\n", len, out);
    return 0;
}
