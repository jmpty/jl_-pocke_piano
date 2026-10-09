/*
 * Pack .mid files into JieLi sydfile "dir_midi" resource.
 * Entry CRC = CRC16-CCITT(init 0) over bytes [2..31].
 * Bytes [2..3]  = CRC16-CCITT(init 0) of file payload (LE).
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#ifdef _WIN32
#include <io.h>
#endif

#define SYDF_MAGIC_MIDI   0x3998df26u
#define SYDF_ENTRY_SIZE   32u
#define SYDF_HDR_SIZE     32u
#define SYDF_FLAGS        0x0000ff03u
#define SYDF_DIR_NAME     "test_dir"
#define SYDF_FILE_TYPE    0xff02u
#define MAX_FILES         16
#define NAME_LEN          16

typedef struct {
    char name[NAME_LEN];
    uint8_t *data;
    uint32_t len;
} mid_file_t;

static uint16_t crc16_ccitt(const uint8_t *ptr, uint32_t len)
{
    uint16_t crc = 0;
    uint32_t i;
    int b;

    for (i = 0; i < len; i++) {
        crc ^= (uint16_t)ptr[i] << 8;
        for (b = 0; b < 8; b++) {
            crc = (crc & 0x8000) ? (uint16_t)((crc << 1) ^ 0x1021) : (uint16_t)(crc << 1);
        }
    }
    return crc;
}

static void put_u16_le(uint8_t *p, uint16_t v)
{
    p[0] = (uint8_t)(v & 0xff);
    p[1] = (uint8_t)((v >> 8) & 0xff);
}

static void put_u32_le(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)(v & 0xff);
    p[1] = (uint8_t)((v >> 8) & 0xff);
    p[2] = (uint8_t)((v >> 16) & 0xff);
    p[3] = (uint8_t)((v >> 24) & 0xff);
}

static int load_mid(const char *path, mid_file_t *out)
{
    FILE *fp;
    long sz;
    size_t n;
    const char *base;
    const char *slash;

    memset(out, 0, sizeof(*out));
    fp = fopen(path, "rb");
    if (!fp) {
        return -1;
    }
    if (fseek(fp, 0, SEEK_END) != 0) {
        fclose(fp);
        return -1;
    }
    sz = ftell(fp);
    if (sz <= 0 || sz > (512 * 1024)) {
        fclose(fp);
        return -1;
    }
    rewind(fp);
    out->data = (uint8_t *)malloc((size_t)sz);
    if (!out->data) {
        fclose(fp);
        return -1;
    }
    n = fread(out->data, 1, (size_t)sz, fp);
    fclose(fp);
    if (n != (size_t)sz) {
        free(out->data);
        out->data = NULL;
        return -1;
    }
    out->len = (uint32_t)sz;

    base = path;
    slash = path;
    while (*slash) {
        if (*slash == '/' || *slash == '\\') {
            base = slash + 1;
        }
        slash++;
    }
    strncpy(out->name, base, NAME_LEN - 1);

    if (out->len < 4 || memcmp(out->data, "MThd", 4) != 0) {
        fprintf(stderr, "skip non-midi: %s\n", path);
        free(out->data);
        out->data = NULL;
        return -1;
    }
    return 0;
}

static int cmp_name(const void *a, const void *b)
{
    return strcmp(((const mid_file_t *)a)->name, ((const mid_file_t *)b)->name);
}

static int scan_dir(const char *srcdir, mid_file_t *files, int max)
{
    char pattern[512];
    int count = 0;

#ifdef _WIN32
    struct _finddata_t fd;
    intptr_t h;

    snprintf(pattern, sizeof(pattern), "%s\\*.mid", srcdir);
    h = _findfirst(pattern, &fd);
    if (h == -1) {
        return 0;
    }
    do {
        snprintf(pattern, sizeof(pattern), "%s\\%s", srcdir, fd.name);
        if (load_mid(pattern, &files[count]) == 0) {
            count++;
            if (count >= max) {
                break;
            }
        }
    } while (_findnext(h, &fd) == 0);
    _findclose(h);
#endif
    return count;
}

static int pack_files(mid_file_t *files, int nfiles, const char *outpath)
{
    uint32_t total;
    uint32_t data_off;
    uint8_t *buf;
    uint8_t entry[SYDF_ENTRY_SIZE];
    FILE *fp;
    int i;

    if (nfiles <= 0) {
        return -1;
    }

    qsort(files, (size_t)nfiles, sizeof(mid_file_t), cmp_name);
    data_off = SYDF_HDR_SIZE + (uint32_t)nfiles * SYDF_ENTRY_SIZE;
    total = data_off;
    for (i = 0; i < nfiles; i++) {
        total += files[i].len;
    }
    /* 与原厂单文件包一致，末尾补 0xFF 对齐到 8 字节 */
    while ((total & 7u) != 0u) {
        total++;
    }

    buf = (uint8_t *)calloc(1, total);
    if (!buf) {
        return -1;
    }
    memset(buf, 0xff, total);

    put_u32_le(buf + 0, SYDF_MAGIC_MIDI);
    put_u32_le(buf + 4, SYDF_ENTRY_SIZE);
    put_u32_le(buf + 8, total);
    put_u32_le(buf + 12, SYDF_FLAGS);
    memset(buf + 16, 0xff, 16);
    memcpy(buf + 16, SYDF_DIR_NAME, strlen(SYDF_DIR_NAME) + 1);

    {
        uint32_t off = data_off;
        for (i = 0; i < nfiles; i++) {
            size_t nlen;

            memset(entry, 0xff, sizeof(entry));
            nlen = strlen(files[i].name);
            if (nlen >= NAME_LEN) {
                nlen = NAME_LEN - 1;
            }
            memcpy(entry + 16, files[i].name, nlen);
            entry[16 + nlen] = 0;
            put_u16_le(entry + 2, crc16_ccitt(files[i].data, files[i].len));
            put_u32_le(entry + 4, off);
            put_u32_le(entry + 8, files[i].len);
            put_u16_le(entry + 12, SYDF_FILE_TYPE);
            put_u16_le(entry + 14, (uint16_t)(i + 1));
            put_u16_le(entry + 0, crc16_ccitt(entry + 2, 30));
            memcpy(buf + SYDF_HDR_SIZE + (uint32_t)i * SYDF_ENTRY_SIZE, entry, SYDF_ENTRY_SIZE);
            memcpy(buf + off, files[i].data, files[i].len);
            off += files[i].len;
        }
    }

    fp = fopen(outpath, "wb");
    if (!fp) {
        free(buf);
        return -1;
    }
    fwrite(buf, 1, total, fp);
    fclose(fp);
    free(buf);

    printf("packed %d midi -> %s (%u bytes)\n", nfiles, outpath, total);
    for (i = 0; i < nfiles; i++) {
        printf("  [%d] %s (%u)\n", i + 1, files[i].name, files[i].len);
    }
    return 0;
}

int main(int argc, char **argv)
{
    mid_file_t files[MAX_FILES];
    int n;
    int i;

    if (argc != 3) {
        fprintf(stderr, "usage: %s <src_dir> <out_dir_midi>\n", argv[0]);
        return 1;
    }

    n = scan_dir(argv[1], files, MAX_FILES);
    if (n <= 0) {
        fprintf(stderr, "no .mid in %s\n", argv[1]);
        return 1;
    }
    if (pack_files(files, n, argv[2]) != 0) {
        fprintf(stderr, "pack failed\n");
        return 1;
    }

    for (i = 0; i < n; i++) {
        free(files[i].data);
    }
    return 0;
}
