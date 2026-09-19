#ifndef __PIANO_KEYBOARD_CONFIG_H__
#define __PIANO_KEYBOARD_CONFIG_H__

#include "midi_prog.h"

/*
 * 14 键琴：两排各 7 键
 * 下排 key13→key3 低音→高音；上排 key8→key2 低音→高音；自下往上音高升
 * 上电配置（按住再上电，松开后生效，写入 VM 掉电保存）：
 *   上排 7 键：选乐器 1~7
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

/* ---------- 上排 7 键对应乐器（枚举前 7 个） ---------- */
typedef enum {
    PIANO_KB_INSTR_PIANO       = 0,
    PIANO_KB_INSTR_FLUTE       = 1,
    PIANO_KB_INSTR_RECORDER    = 2,
    PIANO_KB_INSTR_PAN_FLUTE   = 3,
    PIANO_KB_INSTR_GUITAR      = 4,
    PIANO_KB_INSTR_VIOLIN      = 5,
    PIANO_KB_INSTR_TRUMPET     = 6,
    PIANO_KB_INSTR_SAX         = 7,
    PIANO_KB_INSTR_HARMONICA   = 8,
    PIANO_KB_INSTR_MUSIC_BOX   = 9,
    PIANO_KB_INSTR_ORGAN       = 10,
    PIANO_KB_INSTR_MAX,
} piano_kb_instr_id_t;

#define PIANO_KB_BOOT_INSTR_NUM       7
#define PIANO_KB_DEFAULT_INSTR        PIANO_KB_INSTR_PIANO

#define PIANO_KB_INSTR_PROG_TABLE { \
    MIDI_PROG_ACOUSTIC_GRAND_PIANO, \
    MIDI_PROG_FLUTE_PIPE, \
    MIDI_PROG_RECORDER_PIPE, \
    MIDI_PROG_PAN_FLUTE_PIPE, \
    MIDI_PROG_NYLON_ACOUSTIC_GUITAR, \
    MIDI_PROG_VIOLIN_SOLO_STRINGS, \
    MIDI_PROG_TRUMPET_BRASS, \
    MIDI_PROG_ALTO_SAX_REED, \
    MIDI_PROG_HARMONICA_ORGAN, \
    MIDI_PROG_MUSIC_BOX_CHROMATIC_PERCUSSION, \
    MIDI_PROG_CHURCH_ORGAN, \
}

#define PIANO_KB_INSTR_NAME_TABLE { \
    "Piano", "Flute", "Recorder", "PanFlute", "Guitar", \
    "Violin", "Trumpet", "Sax", "Harmonica", "MusicBox", "Organ", \
}

/* ---------- 上电按键（按住上电） ---------- */
#define PIANO_KB_BOOT_KEY_SEL_EN      1
#define PIANO_KB_VM_EN                1

/* 上排左→右：乐器 1~7 */
#define PIANO_KB_BOOT_INSTR_KEY_TABLE { 8, 9, 10, 11, 0, 1, 2 }
/* 下排：移调 / 音量 */
#define PIANO_KB_BOOT_PITCH_DOWN_KEY  13
#define PIANO_KB_BOOT_PITCH_UP_KEY    12
#define PIANO_KB_BOOT_VOL_DOWN_KEY    4
#define PIANO_KB_BOOT_VOL_UP_KEY      3
#define PIANO_KB_BOOT_PITCH_STEP      1    /* 半音 */

/* 音量 6 挡：2~7 挡，每乐器独立保存 */
#define PIANO_KB_VOL_LEVEL_MIN        2
#define PIANO_KB_VOL_LEVEL_MAX        7
#define PIANO_KB_VOL_LEVEL_DEF        4
#define PIANO_KB_VOL_LEVEL_CNT        (PIANO_KB_VOL_LEVEL_MAX - PIANO_KB_VOL_LEVEL_MIN + 1)

#define PIANO_KB_RUNTIME_ADJ_EN       0

#endif /* __PIANO_KEYBOARD_CONFIG_H__ */
