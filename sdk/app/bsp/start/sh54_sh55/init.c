
#include "config.h"
#include "common.h"
#include "boot.h"
#include "tick_timer_driver.h"
#include "audio.h"
#include "msg.h"
#include "dac.h"
#include "dac_api.h"
#include "audio_adc.h"
#include "vfs.h"
#include "midi_api.h"
#include "src_api.h"
#include "device.h"
#include "ioctl_cmds.h"
#include "vm_api.h"
#include "vm_sfc.h"
#include "nor_fs/nor_fs.h"
#include "key.h"
#include "init.h"
#include "adc_drv.h"
#include "clock.h"
#include "app_power_mg.h"
#include "efuse.h"
#include "my_malloc.h"
#include "app_config.h"
#if TCFG_USB_SFC_UDISK_ENABLE
#include "sfc_udisk.h"
#endif
#include "mio_api.h"
#include "sine_play.h"
#include "list/midi_ctrl_api.h"
#include "efuse_trim_value.h"
#include "flash_wp.h"

#define LOG_TAG_CONST       NORM
#define LOG_TAG             "[normal]"
#include "log.h"



extern void exception_irq_handler(void);
void all_init_isr(void)
{
    u32 i;
    unsigned int *israddr = (unsigned int *)IRQ_MEM_ADDR;
    for (i = 0; i < 32; i++) {
        israddr[i] = (u32)exception_irq_handler;
    }
}

static struct vfs_attr vm_attr;
static struct vfs_attr eeprom_attr;

static int load_eeprom_attr(void)
{
    u32 err;
    void *pvfs = 0;
    void *pvfile = 0;

    err = vfs_mount(&pvfs, (void *)NULL, (void *)NULL);
    ASSERT(!err, "eeprom vfs mount : 0x%x\n", err)
    err = vfs_openbypath(pvfs, &pvfile, "/app_area_head/EEPROM");
    ASSERT(!err, "eeprom vfs openbypath : 0x%x\n", err)
    err = vfs_ioctl(pvfile, FS_IOCTL_FILE_ATTR, (int)&eeprom_attr);
    ASSERT(!err, "eeprom vfs ioctl : 0x%x\n", err)
    vfs_file_close(&pvfile);
    vfs_fs_close(&pvfs);
    return 0;
}

int flash_info_init(void)
{
    u32 err;
    void *pvfs = 0;
    void *pvfile = 0;
    void *device = 0;
    u32 capacity = 0;

    err = vfs_mount(&pvfs, (void *)NULL, (void *) NULL);
    ASSERT(!err, "fii vfs mount : 0x%x\n", err)
    err = vfs_openbypath(pvfs, &pvfile, "/app_area_head/VM");
    ASSERT(!err, "fii vfs openbypath : 0x%x\n", err)
    err = vfs_ioctl(pvfile, FS_IOCTL_FILE_ATTR, (int)&vm_attr);
    ASSERT(!err, "fii vfs ioctl : 0x%x\n", err)
    log_info("file size : 0x%x\nfile sclust : 0x%x\n", vm_attr.fsize, vm_attr.sclust);
    vfs_file_close(&pvfile);
    vfs_fs_close(&pvfs);

    load_eeprom_attr();
    log_info("EEPROM size : 0x%x\nEEPROM sclust : 0x%x\n", eeprom_attr.fsize, eeprom_attr.sclust);

    boot_info.vm.vm_saddr = vm_attr.sclust;
    boot_info.vm.vm_size = vm_attr.fsize;

    device = dev_open(__SFC_NANE, 0);
    dev_ioctl(device, IOCTL_GET_CAPACITY, (u32)&capacity);
    boot_info.flash_size = capacity;
    dev_ioctl(device, IOCTL_SET_VM_INFO, (u32)&boot_info);
    dev_ioctl(device, IOCTL_SET_PROTECT_INFO, (u32)flash_code_protect_callback);
    dev_close(device);

    //该函数位置必须放在flash_info_init()之后，且isd_config.ini文件中VM区域大小不可为0
    norflash_set_write_protect(1);
    return 0;
}
struct vfs_attr *get_vm_attr_p(void)
{
    return &vm_attr;
}

AT_SPI_CODE/*该函数放置段不可更改*/
u32 flash_code_protect_callback(u32 offset, u32 len)
{
    u32 limit_addr = vm_attr.sclust;
    u32 ee_start = eeprom_attr.sclust;
    u32 ee_end = ee_start + eeprom_attr.fsize;

    /* EEPROM 区始终允许写（虚拟U盘可能破坏分区元数据导致 limit_addr 异常） */
    if (ee_start && (offset >= ee_start) && ((offset + len) <= ee_end)) {
        return 0;
    }
    if ((offset < limit_addr) || ((offset + len) > boot_info.flash_size)) {
        return 1;
    }
    return 0;
}

void vm_init_api(void)
{
    int err;

    err = syscfg_vm_init(eeprom_attr.sclust, eeprom_attr.fsize);
    if (err) {
        /* repair 会格式化整块 EEPROM，导致下次开机配置全丢，此处仅打日志 */
        log_info("vm init fail %d (piano cfg uses raw eeprom tail)\n", err);
    }
    /*
     *demo
    u8 data_buf[10];
    memset(data_buf,0xaa,10);
    vm_write(VM_INDEX_DEMO, data_buf, 10);
    memset(data_buf,0x00,10);
    vm_read(VM_INDEX_DEMO, data_buf, 10);
    put_buf(data_buf,10);
    */
}
struct vfs_attr *get_eeprom_attr_p(void)
{
    return &eeprom_attr;
}

sec_used(.version)
u8 const lib_update_version[] = "\x7c\x4f\x94\x0aUPDATE-@20210816-$9c89ae0";

void system_init(void)
{
    my_malloc_init();

#if 0 //运行160M以上时钟，需要执行以下函数提高内核电压，否则可能导致异常
    VDC13_VOL_SEL(VDC13_VOL_SEL_140V);
    SYSVDD_VOL_SEL(SYSVDD_VOL_SEL_129V);
#endif
    pll_sel(PLL_320M, PLL_DIV2, PLL_B_DIV2);
    efuse_trim_value_init();
    /* sys_clock_get(); */
    //--------------------------
    d_key_voice_init();
    message_init();
    adc_init();
    app_power_init();
    key_init();

    //------
    tick_timer_init();
    emu_init();

    //-------------------------------
    vfs_resource_init();
    vfs_init();
#if DECODER_MIDI_EN
    midi_decode_init();
#endif
#if DECODER_MIDI_KEYBOARD_EN
    midi_ctrl_decode_init();
#endif

    dac_mode_init(16, 0);
    audio_adc_mode_init();
    dac_power_on(SR_DEFAULT, 1);

    //--src----------
    src_mode_init();
    /* device */
#if TCFG_USB_SFC_UDISK_ENABLE
    sfc_udisk_ops_init();
#endif
    devices_init();
    flash_info_init();
    vm_init_api();
#if defined(NORFS_EN) && (NORFS_EN)
    norfs_init_api();
#endif
    //----------------------
    d_mio_module_init();
    /* dac_vol(0, 16); */
}

