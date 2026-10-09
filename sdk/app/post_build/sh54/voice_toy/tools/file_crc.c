#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>

static uint16_t c_ccitt(const uint8_t *p, uint32_t n, uint16_t init)
{
    uint16_t crc = init;
    uint32_t i;
    int b;
    for (i = 0; i < n; i++) {
        crc ^= (uint16_t)p[i] << 8;
        for (b = 0; b < 8; b++) {
            crc = (crc & 0x8000) ? (uint16_t)((crc << 1) ^ 0x1021) : (uint16_t)(crc << 1);
        }
    }
    return crc;
}

static uint16_t c_ibm(const uint8_t *p, uint32_t n, uint16_t init)
{
    uint16_t crc = init;
    uint32_t i;
    int b;
    for (i = 0; i < n; i++) {
        crc ^= p[i];
        for (b = 0; b < 8; b++) {
            crc = (crc & 1) ? (crc >> 1) ^ 0xA001 : (crc >> 1);
        }
    }
    return crc;
}

int main(int argc, char **argv)
{
    FILE *fp;
    long sz;
    uint8_t *d;
    if (argc < 2) return 1;
    fp = fopen(argv[1], "rb");
    fseek(fp, 0, SEEK_END);
    sz = ftell(fp);
    rewind(fp);
    d = malloc(sz);
    fread(d, 1, sz, fp);
    fclose(fp);
    printf("size=%ld ccitt0=%04x ibm0=%04x ccittffff=%04x\n",
           sz, c_ccitt(d, (uint32_t)sz, 0), c_ibm(d, (uint32_t)sz, 0),
           c_ccitt(d, (uint32_t)sz, 0xffff));
    free(d);
    return 0;
}
