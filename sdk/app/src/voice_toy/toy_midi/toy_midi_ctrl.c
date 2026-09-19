
#include "toy_midi_ctrl.h"
#include "piano_keyboard_config.h"
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
#include "aw9523.h"
#include "iic_soft.h"
#include "string.h"
#include "dac_api.h"
#include "wdt.h"

#if defined(DECODER_MIDI_KEYBOARD_EN) && (DECODER_MIDI_KEYBOARD_EN)

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
    67,  /* key5  下排 */
    65,  /* key6  下排 */
    64,  /* key7  下排 */
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
static u16 piano_boot_mask;         /* 上电配置键按住时屏蔽发声 */
static u32 piano_scan_jif;
#if PIANO_KB_BOOT_KEY_SEL_EN
static const u8 piano_kb_boot_instr_key[] = PIANO_KB_BOOT_INSTR_KEY_TABLE;
#endif
#if PIANO_KB_VM_EN
#define PIANO_KB_VM_MAGIC           0x504B4231UL

typedef struct {
    u32 magic;
    s8  pitch;
    u8  vol[PIANO_KB_BOOT_INSTR_NUM];
    u8  last_instr;
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


static u32 midi_ctrl_melody_trigger(void *priv, u8 key, u8 vel);
static u32 midi_ctrl_melody_stop_trigger(void *priv, u8 key, u8 chn);
static void midi_on_off_callback_init(dec_obj *obj,
                                      u32(*melody_callback)(void *, u8, u8),
                                      u32(*melody_stop_callback)(void *, u8, u8));

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

static u32 toy_midi_ctrl_note_off(dec_obj *obj, u8 nkey, u8 nvel, u16 decay_time)
{
    if (NULL == obj) {
        return -1;
    }
    void *work_buf = obj->p_dbuf;
    return midi_ctrl_note_off(work_buf, nkey, nvel, decay_time);
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

static void piano_note_on(u8 key)
{
    if (key >= AW9523_KEY_NUM) {
        return;
    }
    if (midi_keyboard_obj) {
        midi_keyboard_obj->sound.enable &= ~B_DEC_PAUSE;
    }
    toy_midi_ctrl_note_on(midi_keyboard_obj,
                          piano_note_get(key),
                          PIANO_NOTE_VEL, PIANO_INSTR_CHN);
}

static void piano_note_off(u8 key)
{
    if (key >= AW9523_KEY_NUM) {
        return;
    }
    toy_midi_ctrl_note_off(midi_keyboard_obj,
                           piano_note_get(key),
                           PIANO_INSTR_CHN, 0);
}

static void piano_all_note_off(void)
{
    u8 i;

    for (i = 0; i < AW9523_KEY_NUM; i++) {
        if ((piano_stable_port & BIT(i)) == 0) {
            piano_note_off(i);
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
}

static void piano_kb_vm_save(void)
{
    piano_kb_vm.magic = PIANO_KB_VM_MAGIC;
    vm_write(VM_INDEX_DEMO, (u8 *)&piano_kb_vm, sizeof(piano_kb_vm));
}

static void piano_kb_vm_load(void)
{
    piano_kb_vm_t tmp;
    u8 i;
    int ret;

    piano_kb_vm_default();
    ret = vm_read(VM_INDEX_DEMO, (u8 *)&tmp, sizeof(tmp));
    if (ret != (int)sizeof(tmp)) {
        piano_kb_vm_save();
        return;
    }
    if (tmp.magic != PIANO_KB_VM_MAGIC) {
        piano_kb_vm_save();
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

#if PIANO_KB_BOOT_KEY_SEL_EN
static u8 piano_boot_instr_from_port(u16 port)
{
    u8 i;

    for (i = 0; i < PIANO_KB_BOOT_INSTR_NUM; i++) {
        u8 key = piano_kb_boot_instr_key[i];

        if ((port & BIT(key)) == 0) {
            return i;
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
#endif

static void piano_cfg_init(void)
{
    u8 instr = (u8)PIANO_KB_DEFAULT_INSTR;

    piano_boot_mask = 0;

#if PIANO_KB_VM_EN
    piano_kb_vm_load();
    piano_pitch_offset = piano_kb_vm.pitch;
    instr = piano_kb_vm.last_instr;
#else
    piano_pitch_offset = (s8)PIANO_KB_DEFAULT_PITCH;
#endif

#if PIANO_KB_BOOT_KEY_SEL_EN
    if (piano_aw9523_ok) {
        u16 port = AW9523_KEY_IDLE_PORT;

        if (!aw9523_read_port(&port)) {
            port &= AW9523_KEY_MASK;
            if (port != AW9523_KEY_IDLE_PORT) {
                instr = piano_boot_apply_port(port);
                piano_boot_mask = (~port) & AW9523_KEY_MASK;
            }
        }
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
        if (piano_boot_mask & BIT(i)) {
            continue;
        }
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

static void piano_key_baseline_sync(void)
{
    u16 port = 0;

    if (!piano_aw9523_ok || aw9523_read_port(&port)) {
        return;
    }
    piano_stable_port = port & AW9523_KEY_MASK;
    piano_scan_jif = jiffies;
    log_info("port baseline 0x%x\n", piano_stable_port);
    if (piano_stable_port != AW9523_KEY_IDLE_PORT) {
        log_info("idle warn expect 0x%x\n", AW9523_KEY_IDLE_PORT);
    }
}

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

#if PIANO_KB_RUNTIME_ADJ_EN
    piano_key_hold_scan(port);
#endif

    changed = port ^ piano_stable_port;
    if (!changed) {
        return;
    }

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
            if (piano_boot_mask & BIT(i)) {
                continue;
            }
            piano_note_on(i);
        } else {
            piano_boot_mask &= ~BIT(i);
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

    decoder_init();
    midi_key_board_function_switch = 0;
    midi_ctrl_decode_init();
    midi_keyboard_obj = decoder_io(NULL, BIT_MIDI_CTRL, NULL, 0);
    if (NULL == midi_keyboard_obj) {
        log_info("midi_keyboard init fail!\n");
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
        toy_midi_ctrl_dec_config_api(midi_keyboard_obj, CMD_MIDI_SET_SWITCH, &midi_key_board_function_switch);
    }

    midi_on_off_callback_init(midi_keyboard_obj,
                              midi_ctrl_melody_trigger,
                              midi_ctrl_melody_stop_trigger);

    piano_aw9523_ok = (aw9523_init() == 0);
    if (!piano_aw9523_ok) {
        log_info("AW9523 init fail, key scan off\n");
    }

    piano_cfg_init();
    if (piano_aw9523_ok) {
        piano_key_baseline_sync();
    }

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

        case MSG_CHANGE_WORK_MODE:
            goto __midi_decode_app_exit;
        case MSG_500MS:
            break;
        default:
            common_msg_deal(&msg[0]);
            break;
        }
    }
__midi_decode_app_exit:
    decoder_stop(midi_keyboard_obj, NEED_WAIT);
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
