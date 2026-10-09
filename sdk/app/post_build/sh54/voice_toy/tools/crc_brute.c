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
    uint8_t e[32] = {
        0x00,0x00,0x03,0xf4,0x40,0x00,0x00,0x00,0x0a,0x03,0x00,0x00,0x02,0xff,0x01,0x00,
        'm','8','.','m','i','d',0x00,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff
    };
    uint16_t want = 0x2dff;
    uint16_t c;

    c = c_ibm(e + 2, 30, 0); printf("ibm0=%04x\n", c);
    c = c_ibm(e + 2, 30, 0xffff); printf("ibmffff=%04x\n", c);
    c = c_ccitt(e + 2, 30, 0); printf("ccitt0=%04x\n", c);
    c = c_ccitt(e + 2, 30, 0xffff); printf("ccittffff=%04x\n", c);
    c = c_ibm(e, 32, 0); printf("ibm all=%04x\n", c);
    c = c_ibm(e + 2, 14, 0); printf("ibm hdr14=%04x\n", c);
    if (c == want) printf("match hdr14\n");
    return 0;
}
