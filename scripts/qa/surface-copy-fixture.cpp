// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 The War Powers authors
// Deterministic surface contracts for test-surface-copy.py. The runner inserts
// the production function, not a copied algorithm. Real GLI is optional; no GPU.
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>
#include <initializer_list>
#include "WPTrace.h"  // the production function's IG_TRACE gate (engine Core/Libraries/Include)
#ifdef TEST_GLI
#include <gli/texture2d.hpp>
#include <gli/generate_mipmaps.hpp>
#undef __APPLE__
#undef __EMSCRIPTEN__
#else
#define __EMSCRIPTEN__ 1
#endif

#define WINAPI
#define CONST const
using HRESULT = int32_t;
using UINT = uint32_t;
using DWORD = uint32_t;
using LONG = int32_t;
using D3DCOLOR = uint32_t;
constexpr HRESULT D3D_OK = 0, D3DERR_INVALIDCALL = -1, TEST_FAILURE = -42;
constexpr DWORD D3DLOCK_READONLY = 16, D3DX_FILTER_BOX = 5, D3DX_FILTER_NONE = 1;
#define FAILED(hr) ((hr) < 0)
struct PALETTEENTRY { uint8_t red, green, blue, flags; };
struct RECT { LONG left, top, right, bottom; };
enum D3DFORMAT {
    D3DFMT_A8R8G8B8, D3DFMT_X8R8G8B8, D3DFMT_R8G8B8, D3DFMT_A1R5G5B5,
    D3DFMT_X1R5G5B5, D3DFMT_R5G6B5, D3DFMT_A4R4G4B4, D3DFMT_A8L8,
    D3DFMT_V8U8, D3DFMT_L6V5U5, D3DFMT_X8L8V8U8, D3DFMT_A8, D3DFMT_L8,
    D3DFMT_DXT1, D3DFMT_DXT2, D3DFMT_DXT3, D3DFMT_DXT4, D3DFMT_DXT5,
    D3DFMT_UNKNOWN
};
struct D3DSURFACE_DESC { D3DFORMAT Format; UINT Width, Height; };
struct D3DLOCKED_RECT { int Pitch; void* pBits; };

class Surface {
public:
    D3DSURFACE_DESC desc;
    int pitch;
    std::vector<uint8_t> bytes;
    HRESULT descResult = D3D_OK, lockResult = D3D_OK, unlockResult = D3D_OK;
    int describes = 0, locks = 0, unlocks = 0, badUnlocks = 0;
    bool locked = false, nullBits = false;
    Surface(UINT w, UINT h, D3DFORMAT fmt, int rowPitch, UINT rows = 0)
        : desc{fmt, w, h}, pitch(rowPitch), bytes(size_t(rowPitch) * (rows ? rows : h), 0xcd) {}
    HRESULT GetDesc(D3DSURFACE_DESC* out) { ++describes; if (FAILED(descResult)) return descResult; *out = desc; return D3D_OK; }
    HRESULT LockRect(D3DLOCKED_RECT* out, const RECT*, DWORD) {
        ++locks;
        if (FAILED(lockResult)) return lockResult;
        if (locked) return D3DERR_INVALIDCALL;
        locked = true;
        *out = {pitch, nullBits ? nullptr : bytes.data()};
        return D3D_OK;
    }
    HRESULT UnlockRect() {
        ++unlocks;
        if (!locked) { ++badUnlocks; return D3DERR_INVALIDCALL; }
        locked = false;
        return unlockResult;
    }
    void pixel(UINT x, UINT y, uint32_t value, int bpp = 4) { std::memcpy(bytes.data() + y * pitch + x * bpp, &value, bpp); }
    uint32_t readPixel(UINT x, UINT y, int bpp = 4) const {
        uint32_t value = 0; std::memcpy(&value, bytes.data() + y * pitch + x * bpp, bpp); return value;
    }
};
using LPDIRECT3DSURFACE8 = Surface*;

// INSERT_PRODUCTION_FUNCTION

static int failed = 0, total = 0;
static void check(bool ok, const char* label) {
    ++total;
    std::printf("%s %s\n", ok ? "PASS" : "FAIL", label);
    if (!ok) ++failed;
}
static HRESULT copy(Surface& dest, Surface& src, const RECT* destArea = nullptr, const RECT* srcArea = nullptr) {
    return D3DXLoadSurfaceFromSurface(&dest, nullptr, destArea, &src, nullptr, srcArea, D3DX_FILTER_BOX, 0);
}
static bool released(const Surface& s) { return !s.locked && s.unlocks == 1 && s.badUnlocks == 0; }

int main(int argc, char**) {
    // First case safely fails the old memcpy(srcPitch * Height) implementation:
    // the destination is larger, so failure is a wrong row layout, not a crash.
    {
        Surface src(2, 3, D3DFMT_A8R8G8B8, 8), dest(2, 3, D3DFMT_A8R8G8B8, 16);
        for (unsigned y = 0; y < 3; ++y) for (unsigned x = 0; x < 2; ++x) src.pixel(x, y, 0x11223300 + y * 2 + x);
        auto expected = dest.bytes;
        for (unsigned y = 0; y < 3; ++y) std::memcpy(expected.data() + y * 16, src.bytes.data() + y * 8, 8);
        check(copy(dest, src) == D3D_OK && dest.bytes == expected && released(src) && released(dest),
              "equal-size copy respects larger destination pitch and preserves padding");
    }
    if (argc > 1) return failed ? 1 : 0;
    {
        Surface src(2, 3, D3DFMT_A8R8G8B8, 16), dest(2, 3, D3DFMT_A8R8G8B8, 8);
        for (unsigned y = 0; y < 3; ++y) for (unsigned x = 0; x < 2; ++x) src.pixel(x, y, 0xaabbcc00 + y * 2 + x);
        auto expected = dest.bytes;
        for (unsigned y = 0; y < 3; ++y) std::memcpy(expected.data() + y * 8, src.bytes.data() + y * 16, 8);
        check(copy(dest, src) == D3D_OK && dest.bytes == expected, "larger source pitch does not overflow a tightly packed destination");
    }
    {
        Surface src(8, 4, D3DFMT_A8R8G8B8, 36), dest(5, 4, D3DFMT_A8R8G8B8, 24);
        for (unsigned y = 0; y < 4; ++y) for (unsigned x = 0; x < 8; ++x) src.pixel(x, y, 0x12340000 + y * 8 + x);
        RECT s{2, 1, 4, 3}, d{1, 0, 3, 2};
        auto expected = dest.bytes;
        for (unsigned y = 0; y < 2; ++y) std::memcpy(expected.data() + y * 24 + 4, src.bytes.data() + (y + 1) * 36 + 8, 8);
        check(copy(dest, src, &d, &s) == D3D_OK && dest.bytes == expected, "font/bitmap subrect copy preserves all pixels outside the destination");
    }
    for (D3DFORMAT fmt : {D3DFMT_DXT1, D3DFMT_DXT5}) {
        int rowBytes = fmt == D3DFMT_DXT1 ? 16 : 32;
        Surface src(7, 5, fmt, rowBytes + 8, 2), dest(7, 5, fmt, rowBytes + 16, 2);
        for (size_t i = 0; i < src.bytes.size(); ++i) src.bytes[i] = uint8_t(i);
        auto expected = dest.bytes;
        for (unsigned y = 0; y < 2; ++y) std::memcpy(expected.data() + y * dest.pitch, src.bytes.data() + y * src.pitch, rowBytes);
        check(copy(dest, src) == D3D_OK && dest.bytes == expected, "compressed copy uses ceil(height / 4) block rows and independent pitches");
    }
    {
        Surface src(12, 8, D3DFMT_DXT1, 24, 2), dest(8, 8, D3DFMT_DXT1, 16, 2);
        RECT s{4, 4, 12, 8}, d{0, 0, 8, 4};
        std::memset(src.bytes.data(), 0x41, src.bytes.size());
        auto expected = dest.bytes; std::memset(expected.data(), 0x41, 16);
        check(copy(dest, src, &d, &s) == D3D_OK && dest.bytes == expected, "aligned compressed region uses block offsets");
        s.left = 1;
        check(copy(dest, src, &d, &s) == D3DERR_INVALIDCALL && src.locks == 1, "misaligned compressed region is rejected before locking");
    }
    {
        Surface src(4, 4, D3DFMT_A8R8G8B8, 20), dest(2, 2, D3DFMT_A8R8G8B8, 12);
        const uint32_t colors[] = {0xff102030, 0xff405060, 0xff708090, 0xffa0b0c0};
        auto expected = dest.bytes;
        for (unsigned y = 0; y < 4; ++y) for (unsigned x = 0; x < 4; ++x) src.pixel(x, y, colors[(y / 2) * 2 + x / 2]);
        for (unsigned y = 0; y < 2; ++y) for (unsigned x = 0; x < 2; ++x) std::memcpy(expected.data() + y * 12 + x * 4, &colors[y * 2 + x], 4);
        check(copy(dest, src) == D3D_OK && dest.bytes == expected && released(src) && released(dest), "32-bit mip reads level zero rows and writes only destination pixels");
    }
    for (D3DFORMAT fmt : {D3DFMT_A1R5G5B5, D3DFMT_A4R4G4B4, D3DFMT_R5G6B5}) {
        Surface src(2, 2, fmt, 8), dest(1, 1, fmt, 6);
        for (unsigned y = 0; y < 2; ++y) for (unsigned x = 0; x < 2; ++x) src.pixel(x, y, 0x9c63, 2);
        check(copy(dest, src) == D3D_OK && dest.readPixel(0, 0, 2) == 0x9c63 && dest.bytes[2] == 0xcd,
              "16-bit mip preserves the supported format and destination padding");
    }
    for (D3DFORMAT fmt : {D3DFMT_A1R5G5B5, D3DFMT_A4R4G4B4, D3DFMT_R5G6B5,
                          D3DFMT_A8R8G8B8, D3DFMT_X8R8G8B8}) {
        const int bpp = fmt == D3DFMT_A8R8G8B8 || fmt == D3DFMT_X8R8G8B8 ? 4 : 2;
        for (bool vertical : {false, true}) {
            Surface src(vertical ? 1 : 4, vertical ? 4 : 1, fmt, (vertical ? 1 : 4) * bpp);
            Surface dest(vertical ? 1 : 2, vertical ? 2 : 1, fmt, (vertical ? 1 : 2) * bpp);
            Surface tail(1, 1, fmt, bpp);
            for (UINT y = 0; y < src.desc.Height; ++y)
                for (UINT x = 0; x < src.desc.Width; ++x) src.pixel(x, y, bpp == 4 ? 0xff2468ac : 0x9c63, bpp);
            const HRESULT first = copy(dest, src), last = copy(tail, dest);
            check(first == D3D_OK && last == D3D_OK &&
                  tail.readPixel(0, 0, bpp) == (bpp == 4 ? 0xff2468ac : 0x9c63),
                  "rectangular mip tail preserves a one-pixel axis through 1x1 without overreading");
        }
    }
    {
        Surface src(2, 2, D3DFMT_A8R8G8B8, 8), dest(1, 1, D3DFMT_A8R8G8B8, 4);
        src.pixel(0, 0, 0x04080c10); src.pixel(1, 0, 0x080c1014);
        src.pixel(0, 1, 0x0c101418); src.pixel(1, 1, 0x1014181c);
        const HRESULT result = copy(dest, src);
#ifdef TEST_GLI
        // Retain the existing GLI library's edge sampling for a one-pixel mip.
        // Matching this behavior to the manual box filter is a separate change.
        check(result == D3D_OK && dest.readPixel(0, 0) == 0x04080c10,
              "Linux mip retains the existing GLI one-pixel sampling");
#else
        check(result == D3D_OK && dest.readPixel(0, 0) == 0x0a0e1216,
              "2:1 mip performs the existing four-pixel channel average");
#endif
    }
    {
        Surface src(4, 4, D3DFMT_A8R8G8B8, 16), dest(2, 2, D3DFMT_A8R8G8B8, 8);
        for (unsigned y = 0; y < 4; ++y) for (unsigned x = 0; x < 4; ++x) src.pixel(x, y, 0xff2468ac);
        check(copy(dest, src) == D3D_OK, "tightly allocated mip source is never read past level zero (ASan)");
    }
    {
        Surface src(2, 2, D3DFMT_A8R8G8B8, 8), dest(2, 2, D3DFMT_A8R8G8B8, 8);
        check(D3DXLoadSurfaceFromSurface(&dest, nullptr, nullptr, nullptr, nullptr, nullptr, 0, 0) == D3DERR_INVALIDCALL,
              "null source is rejected");
        check(D3DXLoadSurfaceFromSurface(nullptr, nullptr, nullptr, &src, nullptr, nullptr, 0, 0) == D3DERR_INVALIDCALL,
              "null destination is rejected");
        PALETTEENTRY palette{};
        check(D3DXLoadSurfaceFromSurface(&dest, &palette, nullptr, &src, nullptr, nullptr, 0, 0) == D3DERR_INVALIDCALL &&
              D3DXLoadSurfaceFromSurface(&dest, nullptr, nullptr, &src, &palette, nullptr, 0, 0) == D3DERR_INVALIDCALL &&
              D3DXLoadSurfaceFromSurface(&dest, nullptr, nullptr, &src, nullptr, nullptr, 0, 1) == D3DERR_INVALIDCALL && !src.locks,
              "unsupported palettes and color key are rejected without modifying surfaces");
        RECT bad{-1, 0, 1, 2};
        check(copy(dest, src, nullptr, &bad) == D3DERR_INVALIDCALL && !src.locks, "invalid region is rejected before locking");
        src.descResult = TEST_FAILURE;
        check(copy(dest, src) == TEST_FAILURE && !src.locks && !dest.locks, "source GetDesc failure propagates before locking");
        src.descResult = D3D_OK; dest.descResult = TEST_FAILURE;
        check(copy(dest, src) == TEST_FAILURE && !src.locks && !dest.locks, "destination GetDesc failure propagates before locking");
    }
    {
        Surface src(2, 2, D3DFMT_A8R8G8B8, 8), dest(2, 2, D3DFMT_A8R8G8B8, 8);
        src.lockResult = TEST_FAILURE;
        check(copy(dest, src) == TEST_FAILURE && !src.unlocks && !dest.locks, "source lock failure neither unlocks nor touches destination");
    }
    {
        Surface src(2, 2, D3DFMT_A8R8G8B8, 8), dest(2, 2, D3DFMT_A8R8G8B8, 8);
        dest.lockResult = TEST_FAILURE;
        check(copy(dest, src) == TEST_FAILURE && released(src) && !dest.unlocks, "destination lock failure releases only the successful source lock");
    }
    for (int invalid = 0; invalid < 3; ++invalid) {
        Surface src(2, 2, D3DFMT_A8R8G8B8, 8), dest(2, 2, D3DFMT_A8R8G8B8, 8);
        if (invalid == 0) src.nullBits = true;
        if (invalid == 1) src.pitch = 4;
        if (invalid == 2) dest.pitch = -8;
        auto expected = dest.bytes;
        check(copy(dest, src) == D3DERR_INVALIDCALL && released(src) && released(dest) && dest.bytes == expected,
              "invalid locked pointer/pitch releases both locks without copying");
    }
    for (int errorTarget = 0; errorTarget < 2; ++errorTarget) {
        Surface src(2, 2, D3DFMT_A8R8G8B8, 8), dest(2, 2, D3DFMT_A8R8G8B8, 8);
        (errorTarget ? src : dest).unlockResult = TEST_FAILURE;
        check(copy(dest, src) == TEST_FAILURE && released(src) && released(dest), "unlock failure is returned after both locks are released");
    }
    {
        Surface src(4, 4, D3DFMT_A8R8G8B8, 16), dest(3, 3, D3DFMT_A8R8G8B8, 12);
        check(copy(dest, src) == D3DERR_INVALIDCALL && !src.locks && !dest.locks, "unsupported scaling is rejected before acquiring locks");
        dest.desc = {D3DFMT_R5G6B5, 2, 2};
        check(copy(dest, src) == D3DERR_INVALIDCALL && !src.locks, "unsupported format conversion is rejected");
        check(copy(src, src) == D3D_OK && !src.locks, "exact self-copy is a no-op without double locking");
        RECT s{0, 0, 2, 2}, d{1, 1, 3, 3};
        check(copy(src, src, &d, &s) == D3DERR_INVALIDCALL && !src.locks, "overlapping in-place region is explicitly rejected");
    }
    std::printf("Surface-copy checks: %d/%d passed\n", total - failed, total);
    return failed ? 1 : 0;
}
