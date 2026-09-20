#ifndef __SFC_UDISK_H__
#define __SFC_UDISK_H__

#include "device.h"

extern struct device_operations sfc_udisk_dev_ops;

void sfc_udisk_ops_init(void);

#endif
