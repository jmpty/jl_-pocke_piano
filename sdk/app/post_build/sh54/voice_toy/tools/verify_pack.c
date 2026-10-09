#include <stdio.h>
#include <stdint.h>
#include <string.h>

static uint16_t crc16(const uint8_t *ptr, uint32_t len)
{
    uint16_t crc = 0;
    uint32_t i;
    int b;
    for (i = 0; i < len; i++) {
        crc ^= ptr[i];
        for (b = 0; b < 8; b++) {
            crc = (crc & 1) ? (crc >> 1) ^ 0xA001 : (crc >> 1);
        }
    }
    return crc;
}

int main(void)
{
    FILE *fp = fopen("../dir_midi.bak", "rb");
    uint8_t e[32];
    uint16_t c;
    if (!fp) return 1;
    fseek(fp, 32, SEEK_SET);
    fread(e, 1, 32, fp);
    fclose(fp);
    c = crc16(e + 2, 30);
    printf("entry crc stored=%04x calc=%04x\n", e[0]|(e[1]<<8), c);
    printf("b2-3=%02x%02x off=%08x len=%08x type=%04x idx=%04x name=%.16s\n",
           e[2],e[3], e[4]|(e[5]<<8)|(e[6]<<16)|(e[7]<<24),
           e[8]|(e[9]<<8)|(e[10]<<16)|(e[11]<<24),
           e[12]|(e[13]<<8), e[14]|(e[15]<<8), e+16);
    return 0;
}
