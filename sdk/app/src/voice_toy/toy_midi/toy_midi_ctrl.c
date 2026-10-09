
#include "toy_midi_ctrl.h"
#include "piano_keyboard_config.h"
#include "app_config.h"
#include "app_modules.h"
#include "common.h"
#include "msg.h"
#include "vfs.h"
#include "MIDI_DEC_API.h"
#include "midi_prog.h"
#include "list/midi_ctrl_api.h"
#include "bsp_loop.h"
#include "toy_main.h"
#include "decoder_api.h"
#include "decoder_msg_tab.h"
#include "Limiter.h"
#include "vm_api.h"
#include "device.h"
#include "ioctl_cmds.h"
#include "flash_wp.h"
#include "init.h"
#include "aw9523.h"
#include "iic_soft.h"
#include "string.h"
#include "dac_api.h"
#include "wdt.h"
#if TCFG_PC_ENABLE
#include "usb/otg.h"
#endif
#if PIANO_KB_PERF_MODE_EN && SIMPLE_DEC_EN
#include "simple_play_file.h"
#include "device_mge.h"
#endif

#if defined(DECODER_MIDI_KEYBOARD_EN) && (DECODER_MIDI_KEYBOARD_EN)

extern void mdelay(u32 ms);

#define LOG_TAG_CONST       NORM
#define LOG_TAG             "[midi_dec]"
#include "log.h"
extern volatile u8 work_mode;
#define MIDI_VIBRATO_ENABLE     0
#define USER_CONFIG_LIMITER       0
#define PIANO_CHROMATIC_EN        0
#define PIANO_SCAN_JIF            2
#define PIANO_NOTE_VEL            127
#define PIANO_INSTR_CHN           0

static const u8 piano_kb_instr_prog[] = PIANO_KB_INSTR_PROG_TABLE;
static const char *const piano_kb_instr_name[] = PIANO_KB_INSTR_NAME_TABLE;
#define PIANO_INSTR_NUM           PIANO_KB_INSTR_MAX

#if PIANO_CHROMATIC_EN
static const u8 piano_note_tab[AW9523_KEY_NUM] = {
    60, 61, 62, 63, 64, 65, 66, 67, 68, 69, 70, 71, 72, 73,
};
#else
/*
 * 两排各 7 键，下排 key13~3、上排 key8~2，各排内低音→高音
 * C 大调 14 白键（移调前 MIDI 音高）
 */
static const u8 piano_note_tab[AW9523_KEY_NUM] = {
    79,  /* key0  上排 */
    81,  /* key1  上排 */
    83,  /* key2  上排最右 */
    71,  /* key3  下排最右 / 音量+ */
    69,  /* key4  下排 / 音量- */
    67,  /* key5  下排 / 乐器9 */
    65,  /* key6  下排 / 上电进演奏，演奏中按退出 */
    64,  /* key7  下排 / 乐器8 */
    72,  /* key8  上排最左 / 乐器1 */
    74,  /* key9  上排 / 乐器2 */
    76,  /* key10 上排 / 乐器3 */
    77,  /* key11 上排 / 乐器4 */
    62,  /* key12 下排 / 移调+ */
    60,  /* key13 下排最左 / 移调- */
};
#endif

extern volatile u32 jiffies;

static dec_obj *midi_keyboard_obj   AT(.midi_ctrl_buf);
static u16 midi_keyboard_idle_cnt   AT(.midi_ctrl_buf);
int midi_key_board_function_switch  AT(.midi_ctrl_buf);

static s8 piano_pitch_offset;       /* 半音移调 */
static u8 piano_cur_instr;
static u8 piano_cur_vol_level = PIANO_KB_VOL_LEVEL_DEF;
static u8 piano_aw9523_ok;
static u16 piano_stable_port;
static u16 piano_boot_port_snap;    /* 上电瞬间按键快照，须在 decoder 初始化前采集 */
static u32 piano_scan_jif;
static u32 piano_key_release_jif[AW9523_KEY_NUM]; /* 0=无待关断；非0=松键时刻 jiffies */
#if PIANO_KB_BOOT_KEY_SEL_EN
static const u8 piano_kb_boot_instr_top_key[] = PIANO_KB_BOOT_INSTR_TOP_KEY_TABLE;
static const u8 piano_kb_boot_instr_bot_key[] = PIANO_KB_BOOT_INSTR_BOT_KEY_TABLE;
#endif
#if PIANO_KB_VM_EN
#define PIANO_KB_VM_MAGIC           0x504B4231UL

typedef struct {
    u32 magic;
    s8  pitch;
    u8  vol[PIANO_KB_BOOT_INSTR_NUM];
    u8  last_instr;
    u8  perf_vol;     /* 演奏模式 DAC 音量挡位 */
} piano_kb_vm_t;

static piano_kb_vm_t piano_kb_vm;
static const u8 piano_kb_dac_vol_map[PIANO_KB_VOL_LEVEL_CNT] = {
    8, 13, 18, 23, 28, 31,
};
#endif
#if PIANO_KB_RUNTIME_ADJ_EN
static u16 piano_hold_jif[AW9523_KEY_NUM];
static u8 piano_long_done[AW9523_KEY_NUM];
#endif
#if PIANO_KB_IDLE_SLEEP_EN
static u16 piano_idle_500ms_cnt;
#endif
#if PIANO_KB_PERF_MODE_EN && SIMPLE_DEC_EN
static play_control piano_perf_pctl          AT(.midi_ctrl_buf);
static u8 piano_perf_mode;
static u8 piano_perf_findex = PIANO_KB_PERF_DEFAULT_SONG;
static u8 piano_perf_vol_level = PIANO_KB_VOL_LEVEL_DEF;
static u8 piano_perf_mounted;

typedef struct {
    u8 dir_idx;
    u8 findex;
} piano_perf_track_t;

static const char *const piano_perf_dir_tab[] = {
    "/dir_song",
    "/dir_eng",
    "/dir_poetry",
    "/dir_story",
};

/* 示例资源：dir_song×3 + dir_eng×3 + dir_story×2 + dir_poetry×5 = 13 曲 */
static const piano_perf_track_t piano_perf_track_tab[] = {
    {0, 1}, {0, 2}, {0, 3},
    {1, 1}, {1, 2}, {1, 3},
    {3, 1}, {3, 2},
    {2, 1}, {2, 2}, {2, 3}, {2, 4}, {2, 5},
};
#define PIANO_KB_PERF_TRACK_NUM  ((u8)(sizeof(piano_perf_track_tab) / sizeof(piano_perf_track_tab[0])))
#endif

static u32 midi_ctrl_melody_trigger(void *priv, u8 key, u8 vel);
static u32 midi_ctrl_melody_stop_trigger(void *priv, u8 key, u8 chn);
static void midi_on_off_callback_init(dec_obj *obj,
                                      u32(*melody_callback)(void *, u8, u8),
                                      u32(*melody_stop_callback)(void *, u8, u8));
static void piano_kb_decoder_apply_cfg(void);
static u8 piano_kb_decoder_start(void);
static void piano_kb_decoder_stop(void);

static u32 toy_midi_ctrl_set_prog(dec_obj *obj, u8 prog, u8 chn)
{
    if (NULL == obj) {
        return -1;
    }
    void *work_buf = obj->p_dbuf;
    return midi_ctrl_set_prog(work_buf, prog, chn);
}

static u32 toy_midi_ctrl_note_on(dec_obj *obj, u8 nkey, u8 nvel, u8 chn)
{
    if (NULL == obj) {
        return -1;
    }
    void *work_buf = obj->p_dbuf;
    return midi_ctrl_note_on(work_buf, nkey, nvel, chn);
}

static u32 toy_midi_ctrl_note_off(dec_obj *obj, u8 nkey, u8 chn, u16 decay_time)
{
    if (NULL == obj) {
        return -1;
    }
    void *work_buf = obj->p_dbuf;
    return midi_ctrl_note_off(work_buf, nkey, chn, decay_time);
}

static u32 toy_midi_ctrl_vel_vibrate(dec_obj *obj, u8 nkey, u8 vel_step, u8 vel_rate, u8 chn)
{
    if (NULL == obj) {
        return -1;
    }
    void *work_buf = obj->p_dbuf;
    return midi_ctrl_vel_vibrate(work_buf, nkey, vel_step, vel_rate, chn);
}

static u32 toy_midi_ctrl_pitch_bend(dec_obj *obj, u16 pitch_val, u8 chn)
{
    if (NULL == obj) {
        return -1;
    }
    void *work_buf = obj->p_dbuf;
    return midi_ctrl_pitch_bend(work_buf, pitch_val, chn);
}

static u32 toy_midi_ctrl_dec_config_api(dec_obj *obj, u32 cmd, void *parm)
{
    if (NULL == obj) {
        return -1;
    }
    void *work_buf = obj->p_dbuf;
    decoder_ops_t *ops = (decoder_ops_t *)obj->dec_ops;
    return ops->dec_confing(work_buf, cmd, parm);
}

static u8 piano_note_get(u8 key)
{
    u8 note;
    int sum;

    if (key >= AW9523_KEY_NUM) {
        return 60;
    }
    note = piano_note_tab[key];
    sum = (int)note + (int)piano_pitch_offset;
    if (sum < 0) {
        sum = 0;
    }
    if (sum > 127) {
        sum = 127;
    }
    return (u8)sum;
}

static void piano_note_off_immediate(u8 key)
{
    if (key >= AW9523_KEY_NUM) {
        return;
    }
    piano_key_release_jif[key] = 0;
    if (midi_keyboard_obj) {
        toy_midi_ctrl_note_off(midi_keyboard_obj,
                               piano_note_get(key),
                               PIANO_INSTR_CHN, 0);
    }
}

static void piano_key_release_poll(void)
{
    u8 i;

#if PIANO_NOTE_RELEASE_MS == 0
    return;
#endif
    for (i = 0; i < AW9523_KEY_NUM; i++) {
        if (piano_key_release_jif[i] == 0) {
            continue;
        }
        if ((u32)(jiffies - piano_key_release_jif[i]) < PIANO_NOTE_RELEASE_JIF) {
            continue;
        }
        piano_note_off_immediate(i);
    }
}

static void piano_note_on(u8 key)
{
    u8 note;

    if (key >= AW9523_KEY_NUM) {
        return;
    }
    piano_key_release_jif[key] = 0;
    if (midi_keyboard_obj) {
        midi_keyboard_obj->sound.enable &= ~B_DEC_PAUSE;
    }
    note = piano_note_get(key);
    /* 同键快速再按：先立即关断再触发，避免释音占配音源产生空档 */
    toy_midi_ctrl_note_off(midi_keyboard_obj, note, PIANO_INSTR_CHN, 0);
    toy_midi_ctrl_note_on(midi_keyboard_obj, note, PIANO_NOTE_VEL, PIANO_INSTR_CHN);
}

static void piano_note_off(u8 key)
{
    if (key >= AW9523_KEY_NUM) {
        return;
    }
#if PIANO_NOTE_RELEASE_MS == 0
    piano_note_off_immediate(key);
#else
    /* SDK 的 note_off(time) 会改变按住时的衰减，不是松手尾音；延后再发 NoteOff */
    piano_key_release_jif[key] = jiffies;
#endif
}

static void piano_all_note_off(void)
{
    u8 i;

    for (i = 0; i < AW9523_KEY_NUM; i++) {
        if ((piano_stable_port & BIT(i)) == 0) {
            piano_note_off_immediate(i);
        }
    }
}

static void piano_set_instr(u8 index)
{
    if (index >= PIANO_INSTR_NUM) {
        index = 0;
    }
    piano_cur_instr = index;
    piano_all_note_off();
    toy_midi_ctrl_set_prog(midi_keyboard_obj, piano_kb_instr_prog[piano_cur_instr], PIANO_INSTR_CHN);
    log_info("instr %s prog %d\n", piano_kb_instr_name[piano_cur_instr],
             piano_kb_instr_prog[piano_cur_instr]);
}

static void piano_set_pitch(s8 semitone)
{
    if (semitone < PIANO_KB_PITCH_MIN) {
        semitone = PIANO_KB_PITCH_MIN;
    }
    if (semitone > PIANO_KB_PITCH_MAX) {
        semitone = PIANO_KB_PITCH_MAX;
    }
    piano_pitch_offset = semitone;
    piano_all_note_off();
    log_info("pitch %d semitone\n", (int)piano_pitch_offset);
}

#if PIANO_KB_VM_EN
static u8 piano_vol_level_clamp(u8 level)
{
    if (level < PIANO_KB_VOL_LEVEL_MIN || level > PIANO_KB_VOL_LEVEL_MAX) {
        return PIANO_KB_VOL_LEVEL_DEF;
    }
    return level;
}

static u16 piano_vol_level_to_cc(u8 level)
{
    u8 step;

    level = piano_vol_level_clamp(level);
    step = level - PIANO_KB_VOL_LEVEL_MIN;
    /* 2~7 挡映射到 682~4096，最低挡也保留可听音量 */
    return (u16)(682U + (3414U * step) / (PIANO_KB_VOL_LEVEL_CNT - 1));
}

static u8 piano_vol_level_to_dac(u8 level)
{
    u8 idx;

    level = piano_vol_level_clamp(level);
    idx = level - PIANO_KB_VOL_LEVEL_MIN;
    return piano_kb_dac_vol_map[idx];
}

static void piano_kb_vm_default(void)
{
    u8 i;

    memset(&piano_kb_vm, 0, sizeof(piano_kb_vm));
    piano_kb_vm.magic = PIANO_KB_VM_MAGIC;
    piano_kb_vm.pitch = (s8)PIANO_KB_DEFAULT_PITCH;
    piano_kb_vm.last_instr = (u8)PIANO_KB_DEFAULT_INSTR;
    for (i = 0; i < PIANO_KB_BOOT_INSTR_NUM; i++) {
        piano_kb_vm.vol[i] = PIANO_KB_VOL_LEVEL_DEF;
    }
    piano_kb_vm.perf_vol = PIANO_KB_VOL_LEVEL_DEF;
}

static u32 piano_kb_eeprom_data_addr(void)
{
    struct vfs_attr *ee = get_eeprom_attr_p();

    return ee->sclust + ee->fsize - sizeof(piano_kb_vm_t);
}

static int piano_kb_eeprom_save(void)
{
    void *dev;
    u32 addr;
    u32 sec;
    int ret;

    addr = piano_kb_eeprom_data_addr();
    sec = addr & ~0xFFFUL;
    dev = dev_open(__SFC_NANE, 0);
    if (!dev) {
        return -1;
    }
    norflash_set_write_protect(0);
    dev_ioctl(dev, IOCTL_ERASE_SECTOR, sec);
    ret = dev_byte_write(dev, &piano_kb_vm, addr, sizeof(piano_kb_vm));
    norflash_set_write_protect(1);
    dev_close(dev);
    return (ret == (int)sizeof(piano_kb_vm)) ? 0 : -1;
}

static int piano_kb_eeprom_load(piano_kb_vm_t *out)
{
    void *dev;
    u32 addr;
    int ret;

    addr = piano_kb_eeprom_data_addr();
    dev = dev_open(__SFC_NANE, 0);
    if (!dev) {
        return -1;
    }
    ret = dev_byte_read(dev, out, addr, sizeof(*out));
    dev_close(dev);
    return (ret == (int)sizeof(*out)) ? 0 : -1;
}

static void piano_kb_vm_save(void)
{
    piano_kb_vm.magic = PIANO_KB_VM_MAGIC;
    if (piano_kb_eeprom_save()) {
        log_info("ee save fail addr 0x%x\n", piano_kb_eeprom_data_addr());
        return;
    }
    log_info("ee save ok @0x%x instr %s pitch %d\n",
             piano_kb_eeprom_data_addr(),
             piano_kb_instr_name[piano_kb_vm.last_instr],
             (int)piano_kb_vm.pitch);
}

static void piano_kb_vm_load(void)
{
    piano_kb_vm_t tmp;
    u8 i;

    piano_kb_vm_default();
    if (piano_kb_eeprom_load(&tmp)) {
        log_info("ee load fail addr 0x%x, use default\n", piano_kb_eeprom_data_addr());
        return;
    }
    if (tmp.magic != PIANO_KB_VM_MAGIC) {
        log_info("ee magic bad 0x%x @0x%x, use default\n",
                 tmp.magic, piano_kb_eeprom_data_addr());
        return;
    }
    if (tmp.last_instr >= PIANO_KB_BOOT_INSTR_NUM) {
        tmp.last_instr = (u8)PIANO_KB_DEFAULT_INSTR;
    }
    if (tmp.pitch < PIANO_KB_PITCH_MIN || tmp.pitch > PIANO_KB_PITCH_MAX) {
        tmp.pitch = (s8)PIANO_KB_DEFAULT_PITCH;
    }
    for (i = 0; i < PIANO_KB_BOOT_INSTR_NUM; i++) {
        tmp.vol[i] = piano_vol_level_clamp(tmp.vol[i]);
    }
    tmp.perf_vol = piano_vol_level_clamp(tmp.perf_vol);
    piano_kb_vm = tmp;
}

static void piano_apply_volume(void)
{
    u8 dac_level;
    u16 cc_vol;

    piano_cur_vol_level = piano_vol_level_clamp(piano_cur_vol_level);
    dac_level = piano_vol_level_to_dac(piano_cur_vol_level);
    cc_vol = piano_vol_level_to_cc(piano_cur_vol_level);
    dac_vol(0, dac_level);
    if (midi_keyboard_obj) {
        EX_CH_VOL_PARM vol_parm;
        u8 ci;

        for (ci = 0; ci < CTRL_CHANNEL_NUM; ci++) {
            vol_parm.cc_vol[ci] = cc_vol;
        }
        vol_parm.ex_vol_use_chn = 1;
        toy_midi_ctrl_dec_config_api(midi_keyboard_obj, CMD_MIDI_SET_EX_VOL, &vol_parm);
    }
    log_info("vol instr %s lvl %d dac %d cc %d\n",
             piano_kb_instr_name[piano_cur_instr],
             piano_cur_vol_level, dac_level, cc_vol);
}
#endif

static void piano_kb_decoder_apply_cfg(void)
{
    if (NULL == midi_keyboard_obj) {
        return;
    }

    {
        EX_CH_VOL_PARM vol_parm;
        u8 ci;

        for (ci = 0; ci < CTRL_CHANNEL_NUM; ci++) {
            vol_parm.cc_vol[ci] = 4096;
        }
        vol_parm.ex_vol_use_chn = 1;
        toy_midi_ctrl_dec_config_api(midi_keyboard_obj, CMD_MIDI_SET_EX_VOL, &vol_parm);
        midi_key_board_function_switch |= EX_VOL_ENABLE;
        toy_midi_ctrl_dec_config_api(midi_keyboard_obj, CMD_MIDI_SET_SWITCH,
                                     &midi_key_board_function_switch);
    }

    {
        MIDI_PLAY_CTRL_TEMPO tempo;
        u8 ci;
        u16 decay_idx;

        decay_idx = PIANO_NOTE_HOLD_DECAY_IDX;
        if (decay_idx > 31) {
            decay_idx = 31;
        }
        tempo.tempo_val = 1042;
        tempo.mute_threshold = (u32)1L << 29;
        for (ci = 0; ci < CTRL_CHANNEL_NUM; ci++) {
            tempo.decay_val[ci] = (u16)((decay_idx << 11) | 1024);
        }
        toy_midi_ctrl_dec_config_api(midi_keyboard_obj, CMD_MIDI_CTRL_TEMPO, &tempo);
    }

    midi_on_off_callback_init(midi_keyboard_obj,
                              midi_ctrl_melody_trigger,
                              midi_ctrl_melody_stop_trigger);
    toy_midi_ctrl_set_prog(midi_keyboard_obj,
                           piano_kb_instr_prog[piano_cur_instr], PIANO_INSTR_CHN);
#if PIANO_KB_VM_EN
    piano_apply_volume();
#endif
}

static u8 piano_kb_decoder_start(void)
{
    midi_keyboard_obj = decoder_io(NULL, BIT_MIDI_CTRL, NULL, 0);
    if (NULL == midi_keyboard_obj) {
        log_info("kb decoder start fail\n");
        return 0;
    }
    piano_kb_decoder_apply_cfg();
    return 1;
}

static void piano_kb_decoder_stop(void)
{
    if (midi_keyboard_obj) {
        piano_all_note_off();
        decoder_stop(midi_keyboard_obj, NEED_WAIT);
        midi_keyboard_obj = NULL;
    }
}

#if PIANO_KB_BOOT_KEY_SEL_EN
static u8 piano_boot_port_valid(u16 port)
{
    if (port == AW9523_KEY_IDLE_PORT) {
        return 0;
    }
    /* 冷启动未稳定时常误读为 0（全按下），不能当真 */
    if (port == AW9523_KEY_BAD_PORT) {
        return 0;
    }
    return 1;
}

static void piano_boot_port_capture(void)
{
    piano_boot_port_snap = AW9523_KEY_IDLE_PORT;
    piano_aw9523_ok = (aw9523_init() == 0);
    if (!piano_aw9523_ok) {
        log_info("AW9523 init fail, key scan off\n");
        return;
    }
    mdelay(5);
    if (aw9523_read_port_stable(&piano_boot_port_snap) != 0) {
        log_info("boot port read fail\n");
        piano_boot_port_snap = AW9523_KEY_IDLE_PORT;
        return;
    }
    if (!piano_boot_port_valid(piano_boot_port_snap)) {
        if (piano_boot_port_snap == AW9523_KEY_BAD_PORT) {
            log_info("boot port 0 invalid, ignore\n");
        }
        piano_boot_port_snap = AW9523_KEY_IDLE_PORT;
        return;
    }
    log_info("boot port snap 0x%04x\n", piano_boot_port_snap);
}

static u8 piano_boot_instr_from_port(u16 port)
{
    u8 i;

    for (i = 0; i < PIANO_KB_BOOT_INSTR_TOP_NUM; i++) {
        u8 key = piano_kb_boot_instr_top_key[i];

        if ((port & BIT(key)) == 0) {
            return i;
        }
    }
    for (i = 0; i < PIANO_KB_BOOT_INSTR_BOT_NUM; i++) {
        u8 key = piano_kb_boot_instr_bot_key[i];

        if ((port & BIT(key)) == 0) {
            return (u8)(PIANO_KB_BOOT_INSTR_TOP_NUM + i);
        }
    }
    return 0xFF;
}

static u8 piano_boot_apply_port(u16 port)
{
    u8 instr;
    u8 vm_dirty = 0;

    instr = piano_kb_vm.last_instr;
    if (instr >= PIANO_KB_BOOT_INSTR_NUM) {
        instr = (u8)PIANO_KB_DEFAULT_INSTR;
    }

    {
        u8 boot_instr = piano_boot_instr_from_port(port);

        if (boot_instr != 0xFF) {
            instr = boot_instr;
            piano_kb_vm.last_instr = instr;
            vm_dirty = 1;
            log_info("boot instr %s\n", piano_kb_instr_name[instr]);
        }
    }

    if ((port & BIT(PIANO_KB_BOOT_PITCH_DOWN_KEY)) == 0) {
        piano_kb_vm.pitch -= PIANO_KB_BOOT_PITCH_STEP;
        if (piano_kb_vm.pitch < PIANO_KB_PITCH_MIN) {
            piano_kb_vm.pitch = PIANO_KB_PITCH_MIN;
        }
        vm_dirty = 1;
        log_info("boot pitch down -> %d\n", (int)piano_kb_vm.pitch);
    } else if ((port & BIT(PIANO_KB_BOOT_PITCH_UP_KEY)) == 0) {
        piano_kb_vm.pitch += PIANO_KB_BOOT_PITCH_STEP;
        if (piano_kb_vm.pitch > PIANO_KB_PITCH_MAX) {
            piano_kb_vm.pitch = PIANO_KB_PITCH_MAX;
        }
        vm_dirty = 1;
        log_info("boot pitch up -> %d\n", (int)piano_kb_vm.pitch);
    }

    if ((port & BIT(PIANO_KB_BOOT_VOL_DOWN_KEY)) == 0) {
        if (piano_kb_vm.vol[instr] > PIANO_KB_VOL_LEVEL_MIN) {
            piano_kb_vm.vol[instr]--;
            vm_dirty = 1;
            log_info("boot vol down instr %s -> %d\n",
                     piano_kb_instr_name[instr], piano_kb_vm.vol[instr]);
        }
    } else if ((port & BIT(PIANO_KB_BOOT_VOL_UP_KEY)) == 0) {
        if (piano_kb_vm.vol[instr] < PIANO_KB_VOL_LEVEL_MAX) {
            piano_kb_vm.vol[instr]++;
            vm_dirty = 1;
            log_info("boot vol up instr %s -> %d\n",
                     piano_kb_instr_name[instr], piano_kb_vm.vol[instr]);
        }
    }

    if (vm_dirty) {
        piano_kb_vm_save();
    }
    return instr;
}

static u16 piano_boot_pressed_mask(void)
{
    if (!piano_boot_port_valid(piano_boot_port_snap)) {
        return 0;
    }
    return (~piano_boot_port_snap) & AW9523_KEY_MASK;
}

static u8 piano_boot_has_instr_key(u16 pressed)
{
    u8 i;

    for (i = 0; i < PIANO_KB_BOOT_INSTR_TOP_NUM; i++) {
        if (pressed & BIT(piano_kb_boot_instr_top_key[i])) {
            return 1;
        }
    }
    for (i = 0; i < PIANO_KB_BOOT_INSTR_BOT_NUM; i++) {
        if (pressed & BIT(piano_kb_boot_instr_bot_key[i])) {
            return 1;
        }
    }
    return 0;
}
#endif

static void piano_cfg_init(void)
{
    u8 instr = (u8)PIANO_KB_DEFAULT_INSTR;

#if PIANO_KB_VM_EN
    piano_kb_vm_load();
    piano_pitch_offset = piano_kb_vm.pitch;
    instr = piano_kb_vm.last_instr;
#else
    piano_pitch_offset = (s8)PIANO_KB_DEFAULT_PITCH;
#endif

#if PIANO_KB_BOOT_KEY_SEL_EN
    if (piano_aw9523_ok && piano_boot_port_valid(piano_boot_port_snap)) {
        instr = piano_boot_apply_port(piano_boot_port_snap);
    }
#endif

    if (instr >= PIANO_KB_BOOT_INSTR_NUM) {
        instr = (u8)PIANO_KB_DEFAULT_INSTR;
    }
    piano_cur_instr = instr;
#if PIANO_KB_VM_EN
    piano_cur_vol_level = piano_kb_vm.vol[instr];
    piano_kb_vm.last_instr = instr;
#endif
    toy_midi_ctrl_set_prog(midi_keyboard_obj,
                           piano_kb_instr_prog[piano_cur_instr], PIANO_INSTR_CHN);
#if PIANO_KB_VM_EN
    piano_apply_volume();
#endif
    log_info("cfg pitch %d semitone\n", (int)piano_pitch_offset);
    log_info("cfg instr %s prog %d vol %d\n",
             piano_kb_instr_name[piano_cur_instr],
             piano_kb_instr_prog[piano_cur_instr], piano_cur_vol_level);
}

#if PIANO_KB_RUNTIME_ADJ_EN
static void piano_key_longpress(u8 key)
{
    if (key == PIANO_KB_KEY_INSTR_PREV) {
        u8 idx = piano_cur_instr;

        if (idx == 0) {
            idx = PIANO_INSTR_NUM - 1;
        } else {
            idx--;
        }
        piano_set_instr(idx);
    } else if (key == PIANO_KB_KEY_INSTR_NEXT) {
        piano_set_instr((u8)((piano_cur_instr + 1) % PIANO_INSTR_NUM));
    } else if (key == PIANO_KB_KEY_PITCH_DOWN) {
        piano_set_pitch(piano_pitch_offset - PIANO_KB_RUNTIME_PITCH_STEP);
    } else if (key == PIANO_KB_KEY_PITCH_UP) {
        piano_set_pitch(piano_pitch_offset + PIANO_KB_RUNTIME_PITCH_STEP);
    }
}

static void piano_key_hold_scan(u16 port)
{
    u8 i;

    for (i = 0; i < AW9523_KEY_NUM; i++) {
        if (0 == (port & BIT(i))) {
            if (piano_hold_jif[i] == 0) {
                piano_hold_jif[i] = (u16)jiffies;
            } else if (!piano_long_done[i] &&
                       (u32)(jiffies - piano_hold_jif[i]) >= PIANO_KB_LONGPRESS_JIF) {
                piano_long_done[i] = 1;
                piano_key_longpress(i);
            }
        } else {
            piano_hold_jif[i] = 0;
            piano_long_done[i] = 0;
        }
    }
}
#endif

#if PIANO_KB_PERF_MODE_EN && SIMPLE_DEC_EN
static void piano_perf_exit(void);
static void piano_perf_stop(void);
static void piano_perf_select_key(u8 key);

static void piano_perf_apply_volume(void)
{
    u8 dac_level;

    piano_perf_vol_level = piano_vol_level_clamp(piano_perf_vol_level);
    dac_level = piano_vol_level_to_dac(piano_perf_vol_level);
    dac_vol(0, dac_level);
    log_info("perf vol lvl %d dac %d\n", piano_perf_vol_level, dac_level);
}

static void piano_perf_vol_change(s8 delta)
{
    u8 level = piano_perf_vol_level;

#if PIANO_KB_VM_EN
    level = piano_kb_vm.perf_vol;
#endif
    if (delta > 0) {
        if (level < PIANO_KB_VOL_LEVEL_MAX) {
            level++;
        }
    } else if (delta < 0) {
        if (level > PIANO_KB_VOL_LEVEL_MIN) {
            level--;
        }
    }
    piano_perf_vol_level = level;
#if PIANO_KB_VM_EN
    piano_kb_vm.perf_vol = level;
    piano_kb_vm_save();
#endif
    piano_perf_apply_volume();
}

static u8 piano_perf_handle_key(u8 key)
{
    if (key == PIANO_KB_BOOT_VOL_UP_KEY) {
        piano_perf_vol_change(1);
        return 1;
    }
    if (key == PIANO_KB_BOOT_VOL_DOWN_KEY) {
        piano_perf_vol_change(-1);
        return 1;
    }
    if (key == PIANO_KB_PERF_MODE_KEY) {
        piano_perf_exit();
        return 1;
    }
    piano_perf_select_key(key);
    return 1;
}

static u8 piano_perf_key_to_findex(u8 key)
{
    if (key >= AW9523_KEY_NUM || key == PIANO_KB_PERF_MODE_KEY) {
        return 0;
    }
    return (key < PIANO_KB_PERF_MODE_KEY) ? (u8)(key + 1) : key;
}

static u32 piano_perf_play_song(u8 track_idx)
{
    u32 err;
    const piano_perf_track_t *tr;

    if (!piano_perf_mounted || track_idx == 0 || track_idx > PIANO_KB_PERF_TRACK_NUM) {
        log_info("perf skip track %d\n", track_idx);
        return 1;
    }
    tr = &piano_perf_track_tab[track_idx - 1];
    piano_kb_decoder_stop();
    piano_perf_pctl.dir_index = tr->dir_idx;
    piano_perf_pctl.findex = tr->findex;
    piano_perf_findex = track_idx;
    piano_perf_apply_volume();
    err = play_one_file(&piano_perf_pctl);
    if (err) {
        log_info("perf play fail track %d dir %d idx %d err 0x%x\n",
                 track_idx, tr->dir_idx, tr->findex, err);
    } else {
        log_info("perf playing track %d dir %s idx %d\n",
                 track_idx, piano_perf_dir_tab[tr->dir_idx], tr->findex);
        kick_decoder();
    }
    return err;
}

static u8 piano_perf_ensure_mount(void)
{
    if (piano_perf_mounted) {
        return 1;
    }
    memset(&piano_perf_pctl, 0, sizeof(piano_perf_pctl));
    piano_perf_pctl.dev_index = INNER_FLASH_RO;
    piano_perf_pctl.findex = PIANO_KB_PERF_DEFAULT_SONG;
    piano_perf_pctl.dec_type = BIT_F1A1 | BIT_UMP3;
    piano_perf_pctl.pdir = (void *)&piano_perf_dir_tab[0];
    piano_perf_pctl.dir_total = sizeof(piano_perf_dir_tab) / sizeof(piano_perf_dir_tab[0]);
    if (simple_dev_fs_mount(&piano_perf_pctl)) {
        piano_perf_mounted = 0;
        log_info("perf dir mount fail\n");
        return 0;
    }
    piano_perf_mounted = 1;
#if PIANO_KB_VM_EN
    piano_perf_vol_level = piano_vol_level_clamp(piano_kb_vm.perf_vol);
#endif
    log_info("perf tracks %d vol %d (f1a demo)\n", PIANO_KB_PERF_TRACK_NUM, piano_perf_vol_level);
    return 1;
}

static void piano_perf_unmount(void)
{
    piano_perf_stop();
    if (piano_perf_mounted) {
        simple_dev_fs_close(&piano_perf_pctl);
        piano_perf_mounted = 0;
    }
}

static void piano_perf_stop(void)
{
    if (piano_perf_pctl.p_dec_obj) {
        decoder_stop(piano_perf_pctl.p_dec_obj, NEED_WAIT);
        piano_perf_pctl.p_dec_obj = NULL;
    }
    if (piano_perf_pctl.pfile) {
        vfs_file_close(&piano_perf_pctl.pfile);
        piano_perf_pctl.pfile = NULL;
    }
}

static void piano_perf_enter(void)
{
    u8 song = piano_perf_findex;

    if (!piano_perf_ensure_mount() || PIANO_KB_PERF_TRACK_NUM == 0) {
        log_info("perf enter fail, demo audio missing\n");
        return;
    }
    if (song == 0 || song > PIANO_KB_PERF_TRACK_NUM) {
        song = PIANO_KB_PERF_DEFAULT_SONG;
    }
    log_info("perf mode enter\n");
    if (0 == piano_perf_play_song(song)) {
        piano_perf_mode = 1;
    } else {
        log_info("perf enter fail, play err\n");
        piano_kb_decoder_start();
    }
}

static void piano_perf_exit(void)
{
    piano_perf_mode = 0;
    piano_perf_unmount();
    piano_kb_decoder_start();
    log_info("perf mode exit\n");
}

#if PIANO_KB_BOOT_KEY_SEL_EN
static u8 piano_boot_perf_song_from_mask(u16 boot_mask)
{
    u8 i;
    u8 findex;
    u8 song = 0;

    for (i = 0; i < AW9523_KEY_NUM; i++) {
        if (i == PIANO_KB_PERF_MODE_KEY) {
            continue;
        }
        if (0 == (boot_mask & BIT(i))) {
            continue;
        }
        findex = piano_perf_key_to_findex(i);
        if (findex != 0) {
            song = findex;
        }
    }
    return song;
}

static void piano_boot_perf_apply(void)
{
    u16 pressed = piano_boot_pressed_mask();
    u8 song;

    if (0 == (pressed & BIT(PIANO_KB_PERF_MODE_KEY))) {
        return;
    }
    if (piano_boot_has_instr_key(pressed)) {
        log_info("boot perf skip, instr key held\n");
        return;
    }
    song = piano_boot_perf_song_from_mask(pressed);
    if (song != 0) {
        piano_perf_findex = song;
    }
    log_info("boot perf enter song %d\n", piano_perf_findex);
    piano_perf_enter();
}
#endif

static void piano_perf_select_key(u8 key)
{
    u8 findex = piano_perf_key_to_findex(key);

    if (findex == 0) {
        return;
    }
    piano_perf_play_song(findex);
}

static void piano_perf_cleanup(void)
{
    if (piano_perf_mode) {
        piano_perf_mode = 0;
        piano_perf_stop();
    }
    piano_perf_unmount();
}
#endif

static void piano_key_baseline_sync(void)
{
    u16 port = AW9523_KEY_IDLE_PORT;

    if (!piano_aw9523_ok) {
        return;
    }
    if (aw9523_read_port_stable(&port)) {
        log_info("baseline read fail\n");
        piano_stable_port = AW9523_KEY_IDLE_PORT;
        return;
    }
    port &= AW9523_KEY_MASK;
    if (port == AW9523_KEY_BAD_PORT) {
        log_info("baseline port 0 invalid, use idle\n");
        port = AW9523_KEY_IDLE_PORT;
    }
    piano_stable_port = port;
    piano_scan_jif = jiffies;
    log_info("port baseline 0x%x\n", piano_stable_port);
    if (piano_stable_port != AW9523_KEY_IDLE_PORT) {
        log_info("idle warn expect 0x%x\n", AW9523_KEY_IDLE_PORT);
    }
}

#if PIANO_KB_IDLE_SLEEP_EN
static void piano_kb_idle_reset(void)
{
    piano_idle_500ms_cnt = 0;
}

static u8 piano_kb_can_sleep(void)
{
    if (!piano_aw9523_ok) {
        return 0;
    }
#if TCFG_PC_ENABLE
    if (usb_otg_online(0) != DISCONN_MODE) {
        return 0;
    }
#endif
    if (midi_keyboard_idle_cnt) {
        return 0;
    }
    if (piano_stable_port != AW9523_KEY_IDLE_PORT) {
        return 0;
    }
#if PIANO_KB_PERF_MODE_EN && SIMPLE_DEC_EN
    if (piano_perf_mode && piano_perf_pctl.p_dec_obj &&
        (MUSIC_PLAY == get_decoder_status(piano_perf_pctl.p_dec_obj))) {
        return 0;
    }
#endif
    return 1;
}

/*
 * 休眠由按键 INT 唤醒：将当前按下键视为新按下并发声，避免 baseline 同步后
 * 边沿为 0 导致“醒着但没声、需松再按”。
 */
static void piano_kb_wake_key_play(void)
{
    u16 port;
    u16 pressed;
    u8 i;

    if (!piano_aw9523_ok) {
        return;
    }
    if (aw9523_read_port_stable(&port)) {
        log_info("wake port read fail\n");
        piano_stable_port = AW9523_KEY_IDLE_PORT;
        piano_scan_jif = jiffies;
        return;
    }
    port &= AW9523_KEY_MASK;
    if (port == AW9523_KEY_BAD_PORT) {
        log_info("wake port 0 invalid\n");
        piano_stable_port = AW9523_KEY_IDLE_PORT;
        piano_scan_jif = jiffies;
        return;
    }

    pressed = (~port) & AW9523_KEY_MASK;
    piano_stable_port = AW9523_KEY_IDLE_PORT;

    if (!pressed) {
        piano_stable_port = port;
        piano_scan_jif = jiffies;
        log_info("wake no key port 0x%x\n", port);
        return;
    }

    log_info("wake key play port 0x%x pressed 0x%x\n", port, pressed);

#if PIANO_KB_PERF_MODE_EN && SIMPLE_DEC_EN
    if (piano_perf_mode) {
        for (i = 0; i < AW9523_KEY_NUM; i++) {
            if (0 == (pressed & BIT(i))) {
                continue;
            }
            piano_perf_handle_key(i);
        }
        piano_stable_port = port;
        piano_scan_jif = jiffies;
        return;
    }
#endif

    for (i = 0; i < AW9523_KEY_NUM; i++) {
        if (pressed & BIT(i)) {
            piano_note_on(i);
        }
    }
    piano_stable_port = port;
    piano_scan_jif = jiffies;
}

static void piano_kb_wake_resume(void)
{
    log_init(1000000);
    aw9523_resume();
    piano_kb_decoder_start();
#if PIANO_KB_VM_EN
    piano_apply_volume();
#endif
    piano_kb_wake_key_play();
    piano_kb_idle_reset();
    log_info("sleep wake\n");
}

static void piano_enter_sleep(void)
{
    log_info("sleep enter\n");
    piano_all_note_off();
#if PIANO_KB_PERF_MODE_EN && SIMPLE_DEC_EN
    if (piano_perf_mode) {
        piano_perf_mode = 0;
        piano_perf_stop();
    }
    piano_perf_unmount();
#endif
    piano_kb_decoder_stop();
    aw9523_sleep_enter();
    vm_pre_erase();
    sys_idle_deal(-2);
    piano_kb_wake_resume();
}
#endif

/* IN: 1=松开 0=按下，对 port 做边沿检测 */
static void piano_key_scan(void)
{
    u16 port;
    u16 changed;
    u8 i;

    if (!piano_aw9523_ok) {
        return;
    }
    if ((u32)(jiffies - piano_scan_jif) < PIANO_SCAN_JIF) {
        return;
    }
    piano_scan_jif = jiffies;
    wdt_clear();

    if (aw9523_read_port(&port)) {
        return;
    }
    port &= AW9523_KEY_MASK;
    if (port == AW9523_KEY_BAD_PORT) {
        return;
    }

    piano_key_release_poll();

#if PIANO_KB_RUNTIME_ADJ_EN
    piano_key_hold_scan(port);
#endif

    changed = port ^ piano_stable_port;
    if (!changed) {
        return;
    }

#if PIANO_KB_IDLE_SLEEP_EN
    piano_kb_idle_reset();
#endif
    log_info("port 0x%x\n", port);
    for (i = 0; i < AW9523_KEY_NUM; i++) {
        if (0 == (changed & BIT(i))) {
            continue;
        }
        if (0 == (port & BIT(i))) {
#if PIANO_KB_RUNTIME_ADJ_EN
            if (piano_long_done[i]) {
                continue;
            }
#endif
#if PIANO_KB_PERF_MODE_EN && SIMPLE_DEC_EN
            if (piano_perf_mode) {
                piano_perf_handle_key(i);
                continue;
            }
#endif
            piano_note_on(i);
        } else {
#if PIANO_KB_PERF_MODE_EN && SIMPLE_DEC_EN
            if (piano_perf_mode) {
                continue;
            }
#endif
            piano_note_off(i);
        }
    }
    piano_stable_port = port;
}

#if USER_CONFIG_LIMITER
u16 ctl_work_buf[60 / 2] AT(.midi_ctrl_buf);
#endif

void toy_midi_keyboard_app(void)
{
    log_info("toy midi keyboard mode!\n");
    int msg[2], err;
    u8 scale = 5;
    u8 key_offset = 0;
    u8 cur_channal = 0;
    u16 pitch_val = 256;

    midi_keyboard_idle_cnt = 0;

    key_table_sel((void *)midi_keyboard_key_msg_filter);

#if PIANO_KB_BOOT_KEY_SEL_EN
    piano_boot_port_capture();
#else
    piano_aw9523_ok = (aw9523_init() == 0);
    if (!piano_aw9523_ok) {
        log_info("AW9523 init fail, key scan off\n");
    }
#endif

    decoder_init();
    midi_key_board_function_switch = 0;
    midi_ctrl_decode_init();
    if (!piano_kb_decoder_start()) {
        log_info("midi_keyboard init fail!\n");
        return;
    }

    piano_cfg_init();
    if (piano_aw9523_ok) {
        piano_key_baseline_sync();
    }
#if PIANO_KB_PERF_MODE_EN && SIMPLE_DEC_EN
#if PIANO_KB_BOOT_KEY_SEL_EN
    piano_boot_perf_apply();
#endif
#endif

    while (1) {
        piano_key_scan();
        err = get_msg(2, &msg[0]);
        bsp_loop();

        if (MSG_NO_ERROR != err) {
            msg[0] = NO_MSG;
        }

        switch (msg[0]) {
        case MSG_MIDICTRL_NOTE_ON_DO:
            key_offset = 0;
            goto __midikey_note_on;
        case MSG_MIDICTRL_NOTE_ON_RE:
            key_offset = 2;
            goto __midikey_note_on;
        case MSG_MIDICTRL_NOTE_ON_MI:
            key_offset = 4;
            goto __midikey_note_on;
        case MSG_MIDICTRL_NOTE_ON_FA:
            key_offset = 5;
__midikey_note_on:
            toy_midi_ctrl_note_on(midi_keyboard_obj,
                                  key_offset + scale * 12, 127, cur_channal);
            break;

        case MSG_MIDICTRL_NOTE_OFF_DO:
            key_offset = 0;
            goto __midikey_note_off;
        case MSG_MIDICTRL_NOTE_OFF_RE:
            key_offset = 2;
            goto __midikey_note_off;
        case MSG_MIDICTRL_NOTE_OFF_MI:
            key_offset = 4;
            goto __midikey_note_off;
        case MSG_MIDICTRL_NOTE_OFF_FA:
            key_offset = 5;
__midikey_note_off:
            toy_midi_ctrl_note_off(midi_keyboard_obj,
                                   key_offset + scale * 12, cur_channal, 0);
            break;

        case MSG_MIDICTRL_PITCH_BEND_DOWN:
            pitch_val -= 16;
            goto __midikeypitch_bend;
        case MSG_MIDICTRL_PITCH_BEND_UP:
            pitch_val += 16;
__midikeypitch_bend:
            toy_midi_ctrl_pitch_bend(midi_keyboard_obj, pitch_val, cur_channal);
            break;

#if PIANO_KB_PERF_MODE_EN && SIMPLE_DEC_EN
        case MSG_F1A1_FILE_END:
        case MSG_F1A1_FILE_ERR:
        case MSG_F1A1_LOOP:
            if (piano_perf_mode) {
                log_info("perf file end, replay track %d\n", piano_perf_findex);
                decoder_stop(piano_perf_pctl.p_dec_obj, NEED_WAIT);
                piano_perf_pctl.p_dec_obj = NULL;
                piano_perf_play_song(piano_perf_findex);
                break;
            }
            break;
#endif
        case MSG_CHANGE_WORK_MODE:
            goto __midi_decode_app_exit;
        case MSG_500MS:
#if PIANO_KB_IDLE_SLEEP_EN
            if (piano_kb_can_sleep()) {
                piano_idle_500ms_cnt++;
                if (piano_idle_500ms_cnt >= (PIANO_KB_IDLE_SLEEP_MS / 500U)) {
                    piano_enter_sleep();
                }
            } else {
                piano_kb_idle_reset();
            }
#endif
            break;
        default:
            common_msg_deal(&msg[0]);
            break;
        }
    }
__midi_decode_app_exit:
#if PIANO_KB_PERF_MODE_EN && SIMPLE_DEC_EN
    piano_perf_cleanup();
#endif
    piano_kb_decoder_stop();
    key_table_sel(NULL);
}

static u32 midi_ctrl_melody_trigger(void *priv, u8 key, u8 vel)
{
    log_info("ON %d %d\n", key, vel);
    midi_keyboard_idle_cnt++;
    return 0;
}

static u32 midi_ctrl_melody_stop_trigger(void *priv, u8 key, u8 chn)
{
    log_info("OFF %d %d\n", key, chn);
    if (midi_keyboard_idle_cnt) {
        midi_keyboard_idle_cnt--;
    }
    return 0;
}

static void midi_on_off_callback_init(dec_obj *obj,
                                      u32(*melody_callback)(void *, u8, u8),
                                      u32(*melody_stop_callback)(void *, u8, u8))
{
    audio_decoder_ops *ops = (audio_decoder_ops *)obj->dec_ops;
    EX_MELODY_STRUCT melody_parm;
    EX_MELODY_STOP_STRUCT melody_stop_parm;

    melody_parm.priv = obj;
    melody_parm.melody_trigger = melody_callback;
    ops->dec_confing(obj->p_dbuf, CMD_MIDI_MELODY_TRIGGER, &melody_parm);

    melody_stop_parm.priv = obj;
    melody_stop_parm.main_chn_enable = 0;
    melody_stop_parm.melody_stop_trigger = melody_stop_callback;
    ops->dec_confing(obj->p_dbuf, CMD_MIDI_STOP_MELODY_TRIGGER, &melody_stop_parm);

    midi_key_board_function_switch |= MELODY_ENABLE | MELODY_STOP_ENABLE;
    ops->dec_confing(obj->p_dbuf, CMD_MIDI_SET_SWITCH, &midi_key_board_function_switch);
}

#endif
