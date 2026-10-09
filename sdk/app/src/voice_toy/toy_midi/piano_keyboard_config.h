#ifndef __PIANO_KEYBOARD_CONFIG_H__
#define __PIANO_KEYBOARD_CONFIG_H__

#include "midi_prog.h"

/*
 * 14 键琴：两排各 7 键
 * 下排 key13→key3 低音→高音；上排 key8→key2 低音→高音；自下往上音高升
 * 上电配置（按住再上电，松开后生效，写入 VM 掉电保存）：
 *   上排 7 键（key8~key2）：选乐器 1~7
 *   下排 key7/key5：选乐器 8~9
 *   下排 key6：按住上电进入内置音乐演奏模式（可同时按其他键选起始曲目）；
 *              正常运行时仅在演奏模式中按 key6 退出，不能进入演奏模式
 *   下排最左 key13：移调 -1 半音；次左 key12：移调 +1 半音
 *   下排右二 key4：当前乐器音量降 1 挡；最右 key3：音量升 1 挡
 */

/* ---------- 音高移调（半音，叠加在 piano_note_tab 之上） ---------- */
typedef enum {
    PIANO_KB_PITCH_M24 = -24,
    PIANO_KB_PITCH_M12 = -12,
    PIANO_KB_PITCH_M7  = -7,
    PIANO_KB_PITCH_M5  = -5,
    PIANO_KB_PITCH_M3  = -3,
    PIANO_KB_PITCH_0   = 0,
    PIANO_KB_PITCH_P3  = 3,
    PIANO_KB_PITCH_P5  = 5,
    PIANO_KB_PITCH_P7  = 7,
    PIANO_KB_PITCH_P12 = 12,
    PIANO_KB_PITCH_P24 = 24,
} piano_kb_pitch_t;

#define PIANO_KB_PITCH_MIN        (-24)
#define PIANO_KB_PITCH_MAX        (24)
#define PIANO_KB_DEFAULT_PITCH    PIANO_KB_PITCH_0

/* ---------- 乐器列表（共 9 种，已去掉竖笛/排笛） ---------- */
typedef enum {
    PIANO_KB_INSTR_PIANO       = 0,
    PIANO_KB_INSTR_FLUTE       = 1,
    PIANO_KB_INSTR_GUITAR      = 2,
    PIANO_KB_INSTR_VIOLIN      = 3,
    PIANO_KB_INSTR_TRUMPET     = 4,
    PIANO_KB_INSTR_SAX         = 5,
    PIANO_KB_INSTR_HARMONICA   = 6,
    PIANO_KB_INSTR_MUSIC_BOX   = 7,
    PIANO_KB_INSTR_ORGAN       = 8,
    PIANO_KB_INSTR_MAX,
} piano_kb_instr_id_t;

#define PIANO_KB_BOOT_INSTR_TOP_NUM   7
#define PIANO_KB_BOOT_INSTR_BOT_NUM   2
#define PIANO_KB_BOOT_INSTR_NUM       PIANO_KB_INSTR_MAX
#define PIANO_KB_DEFAULT_INSTR        PIANO_KB_INSTR_PIANO

#define PIANO_KB_INSTR_PROG_TABLE { \
    MIDI_PROG_ACOUSTIC_GRAND_PIANO, \
    MIDI_PROG_FLUTE_PIPE, \
    MIDI_PROG_NYLON_ACOUSTIC_GUITAR, \
    MIDI_PROG_VIOLIN_SOLO_STRINGS, \
    MIDI_PROG_TRUMPET_BRASS, \
    MIDI_PROG_ALTO_SAX_REED, \
    MIDI_PROG_HARMONICA_ORGAN, \
    MIDI_PROG_MUSIC_BOX_CHROMATIC_PERCUSSION, \
    MIDI_PROG_CHURCH_ORGAN, \
}

#define PIANO_KB_INSTR_NAME_TABLE { \
    "Piano", "Flute", "Guitar", "Violin", "Trumpet", \
    "Sax", "Harmonica", "MusicBox", "Organ", \
}

/* ---------- 上电按键（按住上电） ---------- */
#define PIANO_KB_BOOT_KEY_SEL_EN      1
#define PIANO_KB_VM_EN                1

/* 上排左→右 key8~key2：乐器 1~7 */
#define PIANO_KB_BOOT_INSTR_TOP_KEY_TABLE { 8, 9, 10, 11, 0, 1, 2 }
/* 下排 key7/key5：乐器 8~9（key6 备用，不参与选乐器） */
#define PIANO_KB_BOOT_INSTR_BOT_KEY_TABLE { 7, 5 }
/* 下排：移调 / 音量 */
#define PIANO_KB_BOOT_PITCH_DOWN_KEY  13
#define PIANO_KB_BOOT_PITCH_UP_KEY    12
#define PIANO_KB_BOOT_VOL_DOWN_KEY    4
#define PIANO_KB_BOOT_VOL_UP_KEY      3
#define PIANO_KB_BOOT_PITCH_STEP      1    /* 半音 */

/* 音量 6 挡：2~7 挡，每乐器独立保存 */
#define PIANO_KB_VOL_LEVEL_MIN        2
#define PIANO_KB_VOL_LEVEL_MAX        7
#define PIANO_KB_VOL_LEVEL_DEF        PIANO_KB_VOL_LEVEL_MAX  /* 默认最大音量 */
#define PIANO_KB_VOL_LEVEL_CNT        (PIANO_KB_VOL_LEVEL_MAX - PIANO_KB_VOL_LEVEL_MIN + 1)

#define PIANO_KB_RUNTIME_ADJ_EN       0

/*
 * 抬起延长音：松键后仍满音量保持，延时后再 NoteOff（与 SDK note_off time 无关）
 */
#define PIANO_NOTE_RELEASE_MS         150
/* tick_timer 下 jiffies 约 10ms 递增一次（同 delay_10ms） */
#define PIANO_NOTE_RELEASE_JIF        ((PIANO_NOTE_RELEASE_MS + 9) / 10)

/* ---------- 内置音乐演奏模式（下排 key6 上电进入；演奏中再按 key6 退出） ---------- */
#define PIANO_KB_PERF_MODE_EN         1
#define PIANO_KB_PERF_MODE_KEY        6
#define PIANO_KB_PERF_DEFAULT_SONG      1
/*
 * 演奏模式：播放 Flash 里 SDK 示例语音（F1A/F1B/UMP3，非 MIDI）
 *   曲1~3 dir_song  曲4~6 dir_eng  曲7~8 dir_story  曲9~13 dir_poetry
 * 选曲：除 key6 外 key0->曲1、key1->曲2…；key6 退出演奏模式
 * 音量：key3 音量+、key4 音量-（6 挡，掉电保存）
 */

/*
 * 低功耗：平时全速轮询；连续空闲 PIANO_KB_IDLE_SLEEP_MS 后进入深睡一次。
 * PA0/AW9523 INT 唤醒后播放当前按住键；冷上电仍走乐器/移调/音量选择，不变。
 */
#define PIANO_KB_IDLE_SLEEP_EN        1
#define PIANO_KB_IDLE_SLEEP_MS        15000

#endif /* __PIANO_KEYBOARD_CONFIG_H__ */
