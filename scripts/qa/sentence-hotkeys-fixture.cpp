// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 The War Powers authors
// Deterministic font/surface fixture for test-sentence-hotkeys.py.
// The runner inserts production layout, allocation and glyph-blitting methods.
// GPU uploads and font rasterization are deliberately outside this fixture.

#include <algorithm>
#include <cassert>
#include <cstdio>
#include <cstring>
#include <cwchar>
#include <memory>
#include <string>
#include <vector>

using std::min;
using std::max;
using WCHAR = wchar_t;
using uint16 = unsigned short;
#define TRUE true
#define TEXTURE_OFFSET 2
#define WW3D_FORMAT_A4R4G4B4 0
#define WWASSERT(value) assert(value)
#define NEW_REF(type, args) new type args
#define REF_PTR_RELEASE(ptr) do { if (ptr) { (ptr)->Release_Ref(); (ptr) = nullptr; } } while (0)

struct Vector2 {
    float X = 0, Y = 0;
    void Set(float x, float y) { X = x; Y = y; }
};
struct Vector2i {
    int I = 0, J = 0;
    void Set(int x, int y) { I = x; J = y; }
};
struct SurfaceClass {
    int width, refs = 1;
    bool locked = false;
    std::vector<uint16> pixels;
    SurfaceClass(int w, int h, int) : width(w), pixels(w * h) {}
    void Add_Ref() { ++refs; }
    void Release_Ref() { if (--refs == 0) delete this; }
    uint16* Lock(int* stride) {
        assert(!locked);
        locked = true;
        *stride = width * sizeof(uint16);
        return pixels.data();
    }
    void Unlock() { assert(locked); locked = false; }
};
struct FontCharsClassCharDataStruct { int Width; uint16* Buffer; };
struct FontCharsClass {
    int CharHeight = 16, PixelOverlap = 0;
    std::vector<uint16> glyph = std::vector<uint16>(16 * 10, 0xffff);
    FontCharsClassCharDataStruct data{10, glyph.data()}, zero{0, nullptr};
    std::wstring rendered;
    const FontCharsClassCharDataStruct* Get_Char_Data(WCHAR ch) {
        if (ch > L' ') rendered += ch;
        return ch ? &data : &zero;
    }
    int Get_Char_Spacing(WCHAR ch) { return ch ? 10 : 0; }
    int Get_Char_Height() { return CharHeight; }
    int Get_Extra_Overlap() { return 0; }
    void Blit_Char(WCHAR, uint16*, int, int, int);
};
struct PendingSurfaceStruct { SurfaceClass* Surface = nullptr; };
struct PendingVector : std::vector<PendingSurfaceStruct> {
    void Add(PendingSurfaceStruct surface) { push_back(surface); }
};
struct Render2DSentenceClass {
    FontCharsClass* Font;
    Vector2 Cursor;
    Vector2i TextureOffset;
    int TextureStartX = 0, CurrTextureSize = 0, TextureSizeHint = 0, LockedStride = 0;
    float WrapWidth = 0;
    SurfaceClass* CurSurface = nullptr;
    uint16* LockedPtr = nullptr;
    bool ParseHotKey = false, useHardWordWrap = false, Centered = false;
    PendingVector PendingSurfaces;
    int chunks = 0;
    explicit Render2DSentenceClass(FontCharsClass* font) : Font(font) {}
    ~Render2DSentenceClass() {
        if (LockedPtr) CurSurface->Unlock();
        REF_PTR_RELEASE(CurSurface);
        for (auto& pending : PendingSurfaces) REF_PTR_RELEASE(pending.Surface);
    }
    // Record observable layout activity without constructing GPU render objects.
    void Record_Sentence_Chunk() { ++chunks; }
    void Reset_Sentence_Data() { chunks = 0; }
    void Allocate_New_Surface(const WCHAR*, bool = false);
    Vector2 Build_Sentence_Not_Centered(const WCHAR*, int*, int*, bool = false);
    void Build_Sentence_Centered(const WCHAR*, int*, int*);
    void Build_Sentence(const WCHAR*, int*, int*);
};

// INSERT_PRODUCTION_METHODS

#line 1 "sentence-hotkeys-cases"
int main(int argc, char** argv) {
    struct Case {
        const char* name;
        const WCHAR* text;
        const WCHAR* rendered;
        bool centered = false;
        bool hotkeys = true;
        bool outputX = true;
        bool outputY = true;
        int hotkeyX = 0;
        int hotkeyY = 0;
    };
    const Case cases[] = {
        {"left-plain", L"Retry mission", L"Retrymission"},
        {"centered-plain", L"Retry mission", L"Retrymission", true},
        {"left-hotkey", L"&Retry", L"etry"},
        {"centered-hotkey", L"&Retry", L"etry", true},
        {"left-word-hotkey", L"Try &again", L"Trygain", false, true, true, true, 0, 16},
        {"centered-word-hotkey", L"Try &again", L"Trygain", true, true, true, true, 5, 16},
        {"left-trailing-word", L"text &", L"text&"},
        {"centered-trailing-word", L"text &", L"text&", true},
        {"left-single-ampersand", L"&", L"&"},
        {"centered-single-ampersand", L"&", L"&", true},
        {"left-ampersand-space", L"& word", L"&word"},
        {"centered-ampersand-space", L"& word", L"&word", true},
        {"left-ampersand-newline", L"&\nword", L"&word"},
        {"centered-ampersand-newline", L"&\nword", L"&word", true},
        {"left-literal-ampersand", L"&Retry &", L"&Retry&", false, false},
        {"centered-literal-ampersand", L"&Retry &", L"&Retry&", true, false},
        {"left-x-only", L"Re&try", L"Rery", false, true, true, false, 20, 0},
        {"centered-x-only", L"Re&try", L"Rery", true, true, true, false, 20, 0},
        {"left-y-only", L"Re&try", L"Rery", false, true, false, true, 20, 0},
        {"centered-y-only", L"Re&try", L"Rery", true, true, false, true, 20, 0},
        {"left-newline-hotkey", L"Retry\n&Go", L"Retryo", false, true, true, true, 0, 16},
        {"centered-newline-hotkey", L"Retry\n&Go", L"Retryo", true, true, true, true, 15, 16},
        {"left-newline-y-only", L"Retry\n&Go", L"Retryo", false, true, false, true, 0, 16},
        {"centered-newline-y-only", L"Retry\n&Go", L"Retryo", true, true, false, true, 15, 16},
        {"left-double-ampersand", L"&&", L""},
        {"centered-double-ampersand", L"&&", L"", true},
        {"left-double-trailing-ampersand", L"&&&", L"&"},
        {"centered-double-trailing-ampersand", L"&&&", L"&", true},
        {"left-no-output", L"&Retry", L"etry", false, true, false, false},
        {"centered-no-output", L"&Retry", L"etry", true, true, false, false},
        {"left-empty", L"", L""},
        {"centered-empty", L"", L"", true},
    };
    int total = 0, failed = 0;
    for (const auto& test : cases) {
        if (argc > 1 && std::strcmp(argv[1], test.name) != 0) continue;
        ++total;
        FontCharsClass font;
        Render2DSentenceClass sentence(&font);
        sentence.ParseHotKey = test.hotkeys;
        sentence.Centered = test.centered;
        sentence.WrapWidth = 80;
        // No string capacity/slack: a read after the terminator reaches ASAN's redzone.
        const auto length = std::wcslen(test.text);
        auto input = std::make_unique<WCHAR[]>(length + 1);
        std::copy_n(test.text, length + 1, input.get());
        int x = -1, y = -1;
        sentence.Build_Sentence(input.get(), test.outputX ? &x : nullptr, test.outputY ? &y : nullptr);
        const bool ok = font.rendered == test.rendered && sentence.chunks > 0 &&
                        x == (test.outputX ? test.hotkeyX : -1) &&
                        y == (test.outputY ? test.hotkeyY : -1);
        std::printf("%s %s\n", ok ? "PASS" : "FAIL", test.name);
        if (!ok) {
            ++failed;
            std::printf("  expected glyph count %zu; got %zu; expected hotkey=(%d,%d); got (%d,%d)\n",
                        std::wcslen(test.rendered), font.rendered.size(),
                        test.outputX ? test.hotkeyX : -1, test.outputY ? test.hotkeyY : -1, x, y);
        }
    }
    std::printf("%d/%d sentence checks passed\n", total - failed, total);
    return total > 0 && failed == 0 ? 0 : 1;
}
