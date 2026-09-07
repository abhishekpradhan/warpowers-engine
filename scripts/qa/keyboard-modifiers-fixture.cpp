// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 The War Powers authors
// Deterministic device/timer fixture for test-keyboard-modifiers.py.
// Production keyboard methods are inserted at the marker below by the runner.
// This fixture checks event state, not SDL delivery or game commands.

#include <cstdio>
#include <cstring>
#include <cwctype>
#include <initializer_list>
#include <vector>
using Int = int;
using Bool = bool;
using UnsignedByte = unsigned char;
using UnsignedInt = unsigned;
using WideChar = wchar_t;
using KeyDefType = int;
constexpr bool TRUE = true, FALSE = false;
#define BitIsSet(value, bits) (((value) & (bits)) != 0)
#define BitSet(value, bits) ((value) |= (bits))
#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))

// Minimal KeyDefs/KeyboardIO contract, without the platform/build dependencies.
constexpr int KEY_NONE = 0, KEY_1 = 2, KEY_TAB = 15, KEY_LCTRL = 29,
    KEY_LSHIFT = 42, KEY_RSHIFT = 54, KEY_LALT = 56, KEY_CAPS = 58,
    KEY_RCTRL = 157, KEY_RALT = 184, KEY_LOST = 255;
constexpr int KEY_STATE_UP = 1, KEY_STATE_DOWN = 2, KEY_STATE_LCONTROL = 4,
    KEY_STATE_RCONTROL = 8, KEY_STATE_LSHIFT = 16, KEY_STATE_RSHIFT = 32,
    KEY_STATE_LALT = 64, KEY_STATE_RALT = 128, KEY_STATE_AUTOREPEAT = 256,
    KEY_STATE_CAPSLOCK = 512, KEY_STATE_SHIFT2 = 1024;
static unsigned now = 1000;
unsigned timeGetTime() { return now; }

struct KeyboardIO {
    enum StatusType { STATUS_UNUSED, STATUS_USED };
    unsigned char key = 0, status = 0;
    unsigned short state = 0;
    unsigned keyDownTimeMsec = 0;
};

class Keyboard {
public:
    static constexpr int NUM_KEYS = 256, KEY_REPEAT_DELAY_MSEC = 333, KEY_REPEAT_INTERVAL_MSEC = 67;
    KeyboardIO m_keys[256]{}, m_keyStatus[256]{};
    struct Names { wchar_t stdKey = 0, shifted = 0, shifted2 = 0; } m_keyNames[256];
    unsigned short m_modifiers = 0;
    int m_shift2Key = 0;
    std::vector<KeyboardIO> input;
    unsigned position = 0;

    void feed(std::initializer_list<KeyboardIO> events) {
        input = events;
        position = 0;
        updateKeys();
    }
    void getKey(KeyboardIO* key) { *key = position < input.size() ? input[position++] : KeyboardIO{}; }
    void resetKeys() {
        memset(m_keyStatus, 0, sizeof(m_keyStatus));
        m_modifiers = 0;
    }
    bool getKeyStateBit(int key, int state) { return BitIsSet(m_keyStatus[key].state, state); }
    int getKeyStatusData(int key) { return m_keyStatus[key].status; }
    void setKeyStatusData(int key, KeyboardIO::StatusType status) { m_keyStatus[key].status = status; }
    bool getCapsState() { return BitIsSet(m_modifiers, KEY_STATE_CAPSLOCK); }
    bool isShift() { return (m_modifiers & (KEY_STATE_LSHIFT | KEY_STATE_RSHIFT | KEY_STATE_SHIFT2)) != 0; }

    void updateKeys();
    bool checkKeyRepeat();
    wchar_t translateKey(wchar_t);
};
KeyboardIO down(int key) { return {(unsigned char)key, 0, KEY_STATE_DOWN, now}; }
KeyboardIO up(int key) { return {(unsigned char)key, 0, KEY_STATE_UP, 0}; }

// INSERT_PRODUCTION_METHODS

int main() {
    int failed = 0, total = 0;
    auto check = [&](bool ok, const char* label) {
        ++total;
        std::printf("%s %s\n", ok ? "PASS" : "FAIL", label);
        if (!ok) ++failed;
    };
    const struct Modifier { int key, mask; const char* name; } modifiers[] = {
        {KEY_LCTRL, KEY_STATE_LCONTROL, "Left Ctrl"},
        {KEY_RCTRL, KEY_STATE_RCONTROL, "Right Ctrl"},
        {KEY_LSHIFT, KEY_STATE_LSHIFT, "Left Shift"},
        {KEY_RSHIFT, KEY_STATE_RSHIFT, "Right Shift"},
        {KEY_LALT, KEY_STATE_LALT, "Left Alt"},
        {KEY_RALT, KEY_STATE_RALT, "Right Alt"},
    };
    for (const auto& modifier : modifiers) {
        std::printf("Testing %s\n", modifier.name);
        Keyboard chord;
        chord.feed({down(modifier.key), down(KEY_1), up(KEY_1), up(modifier.key)});
        check((chord.m_keys[1].state & modifier.mask) && !(chord.m_keys[3].state & modifier.mask),
              "complete short chord retains its modifier only through release");
        Keyboard late;
        late.feed({down(KEY_1), up(KEY_1), down(modifier.key)});
        check(!(late.m_keys[0].state & modifier.mask), "later modifier press cannot alter earlier bare key");
    }

    Keyboard carried;
    carried.feed({down(KEY_LCTRL)});
    carried.feed({down(KEY_1), up(KEY_LCTRL), up(KEY_1)});
    check((carried.m_keys[0].state & KEY_STATE_LCONTROL) && !(carried.m_keys[2].state & KEY_STATE_LCONTROL),
          "modifier held across frames and released before keyup");

    Keyboard mixed;
    mixed.feed({down(KEY_LCTRL), down(KEY_LSHIFT), down(KEY_1), up(KEY_LCTRL), up(KEY_1), up(KEY_LSHIFT)});
    constexpr int combined = KEY_STATE_LCONTROL | KEY_STATE_LSHIFT;
    check((mixed.m_keys[2].state & combined) == combined && (mixed.m_keys[4].state & combined) == KEY_STATE_LSHIFT,
          "combined modifiers and partial release");

    Keyboard repeat;
    repeat.feed({down(KEY_1), down(KEY_LCTRL)});
    now += 400;
    repeat.feed({});
    constexpr int repeatedCtrl = KEY_STATE_AUTOREPEAT | KEY_STATE_LCONTROL;
    check(repeat.m_keys[0].key == KEY_1 && (repeat.m_keys[0].state & repeatedCtrl) == repeatedCtrl,
          "held-key repeat uses current modifier");
    repeat.feed({up(KEY_LCTRL)});
    now += 100;
    repeat.feed({});
    check(repeat.m_keys[0].key == KEY_1 && (repeat.m_keys[0].state & KEY_STATE_AUTOREPEAT) &&
          !(repeat.m_keys[0].state & KEY_STATE_LCONTROL), "repeat drops released modifier");
    std::printf("%d/%d checks passed\n", total - failed, total);
    return failed ? 1 : 0;
}
