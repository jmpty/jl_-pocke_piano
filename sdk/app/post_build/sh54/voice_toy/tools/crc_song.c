#include <stdio.h>
#include <stdint.h>

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

int main(void)
{
    uint8_t e[32];
    FILE *fp = fopen("../dir_song", "rb");
    uint16_t want, c0, c1, c2;

    fseek(fp, 32, SEEK_SET);
    fread(e, 1, 32, fp);
    fclose(fp);
    want = (uint16_t)(e[0] | (e[1] << 8));
    c0 = c_ibm(e + 2, 30, 0);
    c1 = c_ccitt(e + 2, 30, 0);
    c2 = c_ccitt(e + 2, 30, 0xffff);
    printf("want=%04x ibm=%04x ccitt0=%04x ccittffff=%04x\n", want, c0, c1, c2);
    return 0;
}
