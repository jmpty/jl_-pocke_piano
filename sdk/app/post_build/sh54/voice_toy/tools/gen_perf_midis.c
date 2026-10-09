/*
 * Generate extra perf-mode MIDI (monophonic). m8.mid comes from SDK original pack.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

typedef struct {
    uint8_t note;
    uint16_t dur;
} ev_t;

typedef struct {
    const char *fname;
    const ev_t *ev;
    int ev_cnt;
} song_t;

/* 女儿情（主旋律简化） */
static const ev_t song_nver[] = {
    {67,180},{64,180},{62,180},{64,180},{67,180},{64,180},{60,180},{62,180},
    {64,360},{60,180},{62,180},{64,360},{64,360},{62,180},{60,180},{62,360},
    {64,180},{67,180},{69,180},{67,180},{64,180},{62,180},{64,360},
    {67,180},{71,180},{69,180},{67,180},{64,180},{62,180},{60,360},
    {62,180},{64,180},{67,180},{69,180},{71,180},{72,360},
    {71,180},{69,180},{67,180},{64,180},{62,180},{60,480},
};

/* 你有没有真的爱过我（主旋律简化） */
static const ev_t song_love[] = {
    {60,200},{62,200},{64,200},{67,200},{65,200},{64,200},{62,400},
    {60,200},{57,200},{60,200},{62,200},{64,400},
    {65,200},{67,200},{69,200},{67,200},{65,200},{64,200},{62,400},
    {60,200},{62,200},{64,200},{67,200},{69,200},{71,200},{69,400},
    {67,200},{65,200},{64,200},{62,200},{60,200},{57,200},{55,600},
    {60,200},{64,200},{67,200},{72,200},{71,200},{69,200},{67,400},
    {65,200},{64,200},{62,200},{60,200},{57,200},{55,200},{53,600},
};

static const song_t songs[] = {
    { "s02_nver.mid",  song_nver, (int)(sizeof(song_nver)/sizeof(song_nver[0])) },
    { "s03_love.mid",  song_love, (int)(sizeof(song_love)/sizeof(song_love[0])) },
};

static void wr_be32(FILE *fp, uint32_t v)
{
    uint8_t b[4];
    b[0] = (uint8_t)((v >> 24) & 0xff);
    b[1] = (uint8_t)((v >> 16) & 0xff);
    b[2] = (uint8_t)((v >> 8) & 0xff);
    b[3] = (uint8_t)(v & 0xff);
    fwrite(b, 1, 4, fp);
}

static int write_song(const char *dir, const song_t *song)
{
    char path[512];
    FILE *fp;
    uint8_t *trk;
    uint32_t trk_len;
    uint32_t pos;
    int i;

    snprintf(path, sizeof(path), "%s\\%s", dir, song->fname);
    fp = fopen(path, "wb");
    if (!fp) {
        return -1;
    }

    trk = (uint8_t *)malloc(8192);
    if (!trk) {
        fclose(fp);
        return -1;
    }
    pos = 0;
    trk[pos++] = 0x00; trk[pos++] = 0xff; trk[pos++] = 0x51; trk[pos++] = 0x03;
    trk[pos++] = 0x07; trk[pos++] = 0xa1; trk[pos++] = 0x20;
    trk[pos++] = 0x00; trk[pos++] = 0xff; trk[pos++] = 0x58; trk[pos++] = 0x04;
    trk[pos++] = 0x04; trk[pos++] = 0x02; trk[pos++] = 0x18; trk[pos++] = 0x08;
    trk[pos++] = 0x00; trk[pos++] = 0xc0; trk[pos++] = 0x00;

    for (i = 0; i < song->ev_cnt; i++) {
        uint32_t delta = song->ev[i].dur;
        uint8_t tmp[8];
        int n = 0;

        tmp[3] = (uint8_t)(delta & 0x7f);
        delta >>= 7;
        while (delta) {
            tmp[2 - n] = (uint8_t)((delta & 0x7f) | 0x80);
            delta >>= 7;
            n++;
        }
        memcpy(trk + pos, tmp + (3 - n), (size_t)(n + 1));
        pos += (uint32_t)(n + 1);
        trk[pos++] = 0x90;
        trk[pos++] = song->ev[i].note;
        trk[pos++] = 0x70;

        delta = song->ev[i].dur;
        n = 0;
        tmp[3] = (uint8_t)(delta & 0x7f);
        delta >>= 7;
        while (delta) {
            tmp[2 - n] = (uint8_t)((delta & 0x7f) | 0x80);
            delta >>= 7;
            n++;
        }
        memcpy(trk + pos, tmp + (3 - n), (size_t)(n + 1));
        pos += (uint32_t)(n + 1);
        trk[pos++] = 0x80;
        trk[pos++] = song->ev[i].note;
        trk[pos++] = 0x40;
    }
    trk[pos++] = 0x00; trk[pos++] = 0xff; trk[pos++] = 0x2f; trk[pos++] = 0x00;
    trk_len = pos;

    fwrite("MThd", 1, 4, fp);
    wr_be32(fp, 6);
    fwrite("\x00\x01\x00\x04\x01\x80", 1, 6, fp);
    fwrite("MTrk", 1, 4, fp);
    wr_be32(fp, trk_len);
    fwrite(trk, 1, trk_len, fp);
    fclose(fp);
    free(trk);
    printf("wrote %s\n", path);
    return 0;
}

int main(int argc, char **argv)
{
    int i;

    if (argc != 2) {
        fprintf(stderr, "usage: %s <out_dir>\n", argv[0]);
        return 1;
    }
    for (i = 0; i < (int)(sizeof(songs)/sizeof(songs[0])); i++) {
        if (write_song(argv[1], &songs[i]) != 0) {
            return 1;
        }
    }
    return 0;
}
