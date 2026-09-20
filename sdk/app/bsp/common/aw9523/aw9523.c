#include "aw9523.h"
#include "iic_soft.h"
#include "gpio.h"

#define LOG_TAG_CONST       NORM
#define LOG_TAG             "[aw9523]"
#include "log.h"

extern void mdelay(u32 ms);

static volatile u8 aw9523_irq_flag;
static u8 aw9523_addr_w = AW9523_ADDR_W;
static u8 aw9523_addr_r = AW9523_ADDR_R;
static u32 aw9523_iic_scl = IO_PORTB_00;
static u32 aw9523_iic_sda = IO_PORTB_01;

void aw9523_int_isr(void)
{
    aw9523_irq_flag = 1;
}

u8 aw9523_int_take(void)
{
    u8 flag = aw9523_irq_flag;
    aw9523_irq_flag = 0;
    return flag;
}

u8 aw9523_int_pin_low(void)
{
    return (gpio_read(AW9523_INT_IO) == 0);
}

u8 aw9523_int_pending(void)
{
    return (aw9523_irq_flag || aw9523_int_pin_low());
}

int aw9523_write_reg(u8 reg, u8 val)
{
    u8 ack;

    soft_iic_start(AW9523_IIC_IDX);
    ack = soft_iic_tx_byte(AW9523_IIC_IDX, aw9523_addr_w);
    if (!ack) {
        soft_iic_stop(AW9523_IIC_IDX);
        return -1;
    }
    ack = soft_iic_tx_byte(AW9523_IIC_IDX, reg);
    if (!ack) {
        soft_iic_stop(AW9523_IIC_IDX);
        return -1;
    }
    ack = soft_iic_tx_byte(AW9523_IIC_IDX, val);
    soft_iic_stop(AW9523_IIC_IDX);
    return ack ? 0 : -1;
}

int aw9523_read_reg(u8 reg, u8 *val)
{
    u8 ack;

    if (val == NULL) {
        return -1;
    }

    soft_iic_start(AW9523_IIC_IDX);
    ack = soft_iic_tx_byte(AW9523_IIC_IDX, aw9523_addr_w);
    if (!ack) {
        soft_iic_stop(AW9523_IIC_IDX);
        return -1;
    }
    ack = soft_iic_tx_byte(AW9523_IIC_IDX, reg);
    if (!ack) {
        soft_iic_stop(AW9523_IIC_IDX);
        return -1;
    }
    soft_iic_stop(AW9523_IIC_IDX);
    soft_iic_start(AW9523_IIC_IDX);
    ack = soft_iic_tx_byte(AW9523_IIC_IDX, aw9523_addr_r);
    if (!ack) {
        soft_iic_stop(AW9523_IIC_IDX);
        return -1;
    }
    *val = soft_iic_rx_byte(AW9523_IIC_IDX, 0);
    soft_iic_stop(AW9523_IIC_IDX);
    return 0;
}

int aw9523_read_port(u16 *port)
{
    u8 p0 = 0;
    u8 p1 = 0;

    if (port == NULL) {
        return -1;
    }

    /* 分开读 IN0/IN1，避免连续读时 IN1 异常 */
    if (aw9523_read_reg(AW9523_REG_IN0, &p0)) {
        return -1;
    }
    if (aw9523_read_reg(AW9523_REG_IN1, &p1)) {
        return -1;
    }

    aw9523_irq_flag = 0;
    *port = ((u16)p1 << 8 | p0) & AW9523_KEY_MASK;
    return 0;
}

int aw9523_read_port_stable(u16 *port)
{
    u16 sample_a;
    u16 sample_b;
    u8 retry;

    if (port == NULL) {
        return -1;
    }

    for (retry = 0; retry < 6; retry++) {
        if (aw9523_read_port(&sample_a)) {
            mdelay(2);
            continue;
        }
        mdelay(1);
        if (aw9523_read_port(&sample_b)) {
            mdelay(2);
            continue;
        }
        if (sample_a == sample_b) {
            *port = sample_a;
            return 0;
        }
        mdelay(2);
    }
    return -1;
}

int aw9523_read_keys(u16 *pressed)
{
    u16 port;

    if (pressed == NULL) {
        return -1;
    }
    if (aw9523_read_port(&port)) {
        return -1;
    }
    /* IN=0 按下，IN=1 松开（外部上拉） */
    *pressed = (~port) & AW9523_KEY_MASK;
    return 0;
}

static u8 aw9523_key_hw_config(void)
{
    u8 cfg0 = 0;
    u8 cfg1 = 0;
    u8 retry;

    /*
     * 上电默认 P1 为输出低，必须先切输入再读键。
     * 顺序：软复位 -> GPIO 模式 -> 方向 -> 其它。
     */
    aw9523_write_reg(AW9523_REG_RSTN, 0x00);
    mdelay(2);

    aw9523_write_reg(AW9523_REG_MODE0, 0xFF);
    aw9523_write_reg(AW9523_REG_MODE1, 0xFF);
    aw9523_write_reg(AW9523_REG_CFG0, AW9523_KEY_CFG0);
    aw9523_write_reg(AW9523_REG_CFG1, AW9523_KEY_CFG1);
    mdelay(1);
    aw9523_write_reg(AW9523_REG_GCR, 0x00);
    aw9523_write_reg(AW9523_REG_OUT0, 0xFF);
    aw9523_write_reg(AW9523_REG_OUT1, 0xFF);
    aw9523_write_reg(AW9523_REG_INT0, 0xFF);
    aw9523_write_reg(AW9523_REG_INT1, 0xFF);

    for (retry = 0; retry < 3; retry++) {
        aw9523_read_reg(AW9523_REG_CFG0, &cfg0);
        aw9523_read_reg(AW9523_REG_CFG1, &cfg1);
        if ((cfg0 == AW9523_KEY_CFG0) && (cfg1 == AW9523_KEY_CFG1)) {
            return 0;
        }
        aw9523_write_reg(AW9523_REG_CFG0, AW9523_KEY_CFG0);
        aw9523_write_reg(AW9523_REG_CFG1, AW9523_KEY_CFG1);
        mdelay(1);
    }
    log_info("CFG warn got 0x%x/0x%x want 0x%x/0x%x\n",
             cfg0, cfg1, AW9523_KEY_CFG0, AW9523_KEY_CFG1);
    return 1;
}

static void aw9523_dump_cfg(void)
{
    u8 mode0 = 0, mode1 = 0, cfg0 = 0, cfg1 = 0;
    u8 in0 = 0, in1 = 0;
    u16 port = 0;

    aw9523_read_reg(AW9523_REG_MODE0, &mode0);
    aw9523_read_reg(AW9523_REG_MODE1, &mode1);
    aw9523_read_reg(AW9523_REG_CFG0, &cfg0);
    aw9523_read_reg(AW9523_REG_CFG1, &cfg1);
    aw9523_read_reg(AW9523_REG_IN0, &in0);
    aw9523_read_reg(AW9523_REG_IN1, &in1);
    aw9523_read_port(&port);
    log_info("CFG=0x%x/0x%x IN0=0x%x IN1=0x%x port=0x%x\n",
             cfg0, cfg1, in0, in1, port);
    if ((port & AW9523_KEY_MASK) != AW9523_KEY_IDLE_PORT) {
        log_info("P1 stuck? IN1=0x%x expect bit0~5 high\n", in1 & 0x3F);
    }
}

static u8 aw9523_quick_ping(u32 scl, u32 sda, u8 addr_w)
{
    u8 ack;

    soft_iic_set_pins(AW9523_IIC_IDX, scl, sda);
    soft_iic_init(AW9523_IIC_IDX);
    soft_iic_start(AW9523_IIC_IDX);
    ack = soft_iic_tx_byte(AW9523_IIC_IDX, addr_w);
    soft_iic_stop(AW9523_IIC_IDX);
    return ack;
}

static u8 aw9523_pin_hunt(u32 *scl, u32 *sda)
{
    static const u8 addr_try[] = {0xB0, 0xB2, 0xB4, 0xB6};
    static const struct {
        u32 scl;
        u32 sda;
    } pairs[] = {
        { IO_PORTB_00, IO_PORTB_01 }, /* 原理图 PB0/PB1 */
        { IO_PORTB_01, IO_PORTB_00 },
        { IO_PORTA_01, IO_PORTA_02 }, /* SH54 硬件 I2C B 组 */
        { IO_PORTA_02, IO_PORTA_01 },
        { IO_PORTA_01, IO_PORTA_03 }, /* fw-AD15N 默认 soft I2C */
        { IO_PORTA_03, IO_PORTA_01 },
        { IO_PORTA_08, IO_PORTA_09 }, /* 硬件 I2C D 组 */
        { IO_PORTA_09, IO_PORTA_08 },
    };
    u8 i, j, ack;

    log_info("I2C pin hunt (0xB0~0xB6)\n");
    for (i = 0; i < (sizeof(pairs) / sizeof(pairs[0])); i++) {
        for (j = 0; j < sizeof(addr_try); j++) {
            ack = aw9523_quick_ping(pairs[i].scl, pairs[i].sda, addr_try[j]);
            if (ack) {
                log_info("HIT scl=%d sda=%d addr=0x%x\n",
                         pairs[i].scl, pairs[i].sda, addr_try[j]);
                aw9523_addr_w = addr_try[j];
                aw9523_addr_r = (u8)(addr_try[j] | 0x01);
                *scl = pairs[i].scl;
                *sda = pairs[i].sda;
                return 1;
            }
        }
    }
    log_info("I2C pin hunt miss\n");
    return 0;
}

static u8 aw9523_bus_probe(u32 scl, u32 sda)
{
    u16 addr;
    u8 ack, found, n;

    soft_iic_set_pins(AW9523_IIC_IDX, scl, sda);
    soft_iic_init(AW9523_IIC_IDX);
    mdelay(2);

    log_info("I2C scan scl=%d sda=%d idle %d %d\n",
             scl, sda, gpio_read(scl) ? 1 : 0, gpio_read(sda) ? 1 : 0);

    found = 0;
    n = 0;
    /* 写地址 0x00~0xFE，步进 2，覆盖全部 7bit */
    for (addr = 0; addr <= 0xFE; addr += 2) {
        soft_iic_start(AW9523_IIC_IDX);
        ack = soft_iic_tx_byte(AW9523_IIC_IDX, (u8)addr);
        soft_iic_stop(AW9523_IIC_IDX);
        if (ack) {
            n++;
            log_info("I2C HIT 8bit=0x%x 7bit=0x%x\n", addr, addr >> 1);
            if (!found) {
                aw9523_addr_w = (u8)addr;
                aw9523_addr_r = (u8)(addr | 0x01);
                found = 1;
            }
        }
    }
    log_info("I2C scan done, hit=%d first=0x%x\n", n, aw9523_addr_w);
    return found;
}

static void aw9523_hw_reset_release(void)
{
#if AW9523_RST_HW_HIGH
    log_info("RSTN hardwired high\n");
#else
    gpio_set_die(AW9523_RST_IO, 1);
    gpio_set_dieh(AW9523_RST_IO, 1);
    gpio_set_pull_up(AW9523_RST_IO, 0);
    gpio_set_pull_down(AW9523_RST_IO, 0);
    gpio_set_direction(AW9523_RST_IO, 0);
    gpio_write(AW9523_RST_IO, 1);
    mdelay(2);
    log_info("RSTN PA2=1\n");
#endif
}

static void aw9523_int_io_init(void)
{
    gpio_set_direction(AW9523_INT_IO, 1);
    gpio_set_die(AW9523_INT_IO, 1);
    gpio_set_dieh(AW9523_INT_IO, 1);
    gpio_set_pull_up(AW9523_INT_IO, 1);
    gpio_set_pull_down(AW9523_INT_IO, 0);
}

static void aw9523_i2c_restore(void)
{
    aw9523_int_io_init();
    soft_iic_set_pins(AW9523_IIC_IDX, aw9523_iic_scl, aw9523_iic_sda);
    soft_iic_init(AW9523_IIC_IDX);
}

static void aw9523_int_enable(void)
{
    aw9523_write_reg(AW9523_REG_INT0, 0x00);
    aw9523_write_reg(AW9523_REG_INT1, AW9523_INT_MASK1);
}

static void aw9523_int_disable(void)
{
    aw9523_write_reg(AW9523_REG_INT0, 0xFF);
    aw9523_write_reg(AW9523_REG_INT1, 0xFF);
}

void aw9523_sleep_enter(void)
{
    u16 port;

    aw9523_int_enable();
    aw9523_read_port(&port);
    aw9523_irq_flag = 0;
}

int aw9523_resume(void)
{
    aw9523_i2c_restore();
    aw9523_int_disable();
    aw9523_irq_flag = 0;
    return 0;
}

static u8 aw9523_i2c_setup(u32 *scl, u32 *sda)
{
    *scl = IO_PORTB_00;
    *sda = IO_PORTB_01;
    if (aw9523_quick_ping(*scl, *sda, AW9523_ADDR_W)) {
        aw9523_addr_w = AW9523_ADDR_W;
        aw9523_addr_r = AW9523_ADDR_R;
        log_info("AW9523 on PB0/PB1 addr 0x%x\n", aw9523_addr_w);
        return 1;
    }
    log_info("PB0/PB1 no ack, pin hunt\n");
    return aw9523_pin_hunt(scl, sda);
}

int aw9523_init(void)
{
    u8 id = 0;
    u32 scl = IO_PORTB_00;
    u32 sda = IO_PORTB_01;

    aw9523_hw_reset_release();
    aw9523_int_io_init();

    if (!aw9523_i2c_setup(&scl, &sda)) {
        log_info("AW9523 no ack, check SCL/SDA/VCC/RSTN\n");
        return -1;
    }

    aw9523_iic_scl = scl;
    aw9523_iic_sda = sda;
    soft_iic_set_pins(AW9523_IIC_IDX, scl, sda);
    soft_iic_init(AW9523_IIC_IDX);

    if (aw9523_read_reg(AW9523_REG_ID, &id) || (id != AW9523_CHIP_ID)) {
        log_info("AW9523 id fail: 0x%x addr 0x%x (expect 0x23)\n", id, aw9523_addr_w);
        return -1;
    }
    log_info("AW9523 id 0x%x addr 0x%x\n", id, aw9523_addr_w);

    aw9523_key_hw_config();
    mdelay(5);
    aw9523_dump_cfg();

    aw9523_irq_flag = 0;
    log_info("AW9523 ok, 14key input\n");
    return 0;
}
