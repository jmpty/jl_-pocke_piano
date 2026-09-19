#ifndef __AW9523_H__
#define __AW9523_H__

#include "typedef.h"
#include "gpio.h"

/* AW9523BTQR: AD1=AD0=GND -> 7bit 0x58, 写0xB0 读0xB1 */
#define AW9523_IIC_IDX          0
#define AW9523_ADDR_W           0xB0
#define AW9523_ADDR_R           0xB1

#define AW9523_REG_IN0          0x00
#define AW9523_REG_IN1          0x01
#define AW9523_REG_OUT0         0x02
#define AW9523_REG_OUT1         0x03
#define AW9523_REG_CFG0         0x04    /* 1=输入 0=输出 */
#define AW9523_REG_CFG1         0x05
#define AW9523_REG_INT0         0x06    /* 0=开中断 1=关 */
#define AW9523_REG_INT1         0x07
#define AW9523_REG_ID           0x10    /* 固定 0x23 */
#define AW9523_REG_GCR          0x11
#define AW9523_REG_MODE0        0x12    /* 1=GPIO 0=LED */
#define AW9523_REG_MODE1        0x13
#define AW9523_REG_RSTN         0x7F

#define AW9523_CHIP_ID          0x23

/*
 * 硬件：P00~P07、P10~P15 各接 10K 上拉 + 按键对地；P16/P17 未用。
 * 固件：高阻输入读 IN，松开 IN=1，按下 IN=0。
 */

/* 14 键：P00~P07(bit0~7)、P10~P15(bit8~13)；不含 P16/P17 */
#define AW9523_KEY_NUM          14
#define AW9523_KEY_MASK         0x3FFF
#define AW9523_KEY_CFG0         0xFF    /* P00~P07 输入 */
#define AW9523_KEY_CFG1         0xFF    /* P10~P17 输入，未用脚在软件掩掉 */
#define AW9523_KEY_IDLE_PORT    0x3FFF  /* 全松开：IN 低 14 位为 1 */

/* AD142A0 封装丝印 PA0/PA13，按 GPIO 用 PA0 */
#define AW9523_INT_IO           IO_PORTA_00
/* 原理图 RSTN 已硬件接高；1=不再驱动 PA2 */
#define AW9523_RST_HW_HIGH      1
#if !AW9523_RST_HW_HIGH
/* 原理图 RSTN -> U4 PA2/PA13，低有效，须 MCU 拉高释放复位 */
#define AW9523_RST_IO           IO_PORTA_02
#endif

int aw9523_init(void);
int aw9523_write_reg(u8 reg, u8 val);
int aw9523_read_reg(u8 reg, u8 *val);
u8 aw9523_int_pending(void);
/* 读 IN 原始电平，bit=1 高/松开，bit=0 低/按下 */
int aw9523_read_port(u16 *port);
/* bit=1 按下；bit0=P00 ... bit13=P15 */
int aw9523_read_keys(u16 *pressed);
void aw9523_int_isr(void);
u8 aw9523_int_take(void);
u8 aw9523_int_pin_low(void);

#endif
