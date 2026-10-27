#include "sfc_udisk.h"
#include "string.h"

extern const struct device_operations sfc_dev_ops;

struct device_operations sfc_udisk_dev_ops;

static int sfc_udisk_bulk_read(struct device *device, void *buf, u32 len, u32 offset);
static int sfc_udisk_bulk_write(struct device *device, void *buf, u32 len, u32 offset);

void sfc_udisk_ops_init(void)
{
    memcpy(&sfc_udisk_dev_ops, &sfc_dev_ops, sizeof(sfc_udisk_dev_ops));
    sfc_udisk_dev_ops.bulk_read = sfc_udisk_bulk_read;
    sfc_udisk_dev_ops.bulk_write = sfc_udisk_bulk_write;
}



static int sfc_udisk_bulk_read(struct device *device, void *buf, u32 len, u32 offset)
{
    u32 byte_off;
    u32 byte_len;
    int ret;

    if (!len || !device) {
        return 0;
    }

    byte_off = offset * 512;
    byte_len = len * 512;
    ret = sfc_dev_ops.read(device, buf, byte_len, byte_off);
    if ((u32)ret != byte_len) {
        return 0;
    }
    return len;
}

static int sfc_udisk_bulk_write(struct device *device, void *buf, u32 len, u32 offset)
{
    u32 byte_off;
    u32 byte_len;
    int ret;

    if (!len || !device) {
        return 0;
    }

    byte_off = offset * 512;
    byte_len = len * 512;
    ret = sfc_dev_ops.write(device, buf, byte_len, byte_off);
    if ((u32)ret != byte_len) {
        return 0;
    }
    return len;
}
