// Deterministic ownership fixture. The runner inserts the production mip filter.
// Surface objects remain inspectable after an invalid final Release so baseline
// ownership failures are reported directly, without dereferencing freed memory.
#include <cstdint>
#include <cstdio>
#include <vector>

using UINT = unsigned;
using DWORD = unsigned;
using HRESULT = int32_t;
using PALETTEENTRY = int;
enum D3DRESOURCETYPE { D3DRTYPE_TEXTURE };
constexpr HRESULT D3D_OK = 0, D3DERR_INVALIDCALL = -1, TEST_ERROR = -2;
constexpr UINT D3DX_DEFAULT = UINT(-1), D3DX_FILTER_BOX = 5;
#define WINAPI
#define CONST const
#define FAILED(value) ((value) < 0)
struct D3DSURFACE_DESC { UINT Width = 8; };

struct IDirect3DSurface8 {
    UINT level;
    int refs = 1;
    void Release() { --refs; }
};
struct IDirect3DTexture8 {
    std::vector<IDirect3DSurface8> surfaces;
    int failAcquire = -1;
    bool failDescription = false;
    explicit IDirect3DTexture8(UINT levels) {
        for (UINT i = 0; i < levels; ++i) surfaces.push_back({i});
    }
    UINT GetLevelCount() const { return unsigned(surfaces.size()); }
    D3DRESOURCETYPE GetType() const { return D3DRTYPE_TEXTURE; }
    HRESULT GetLevelDesc(UINT, D3DSURFACE_DESC*) { return failDescription ? TEST_ERROR : D3D_OK; }
    HRESULT GetSurfaceLevel(UINT level, IDirect3DSurface8** out) {
        if (level == UINT(failAcquire)) return TEST_ERROR;
        if (level >= surfaces.size()) return D3DERR_INVALIDCALL;
        *out = &surfaces[level]; ++(*out)->refs;
        return D3D_OK;
    }
};
using LPDIRECT3DBASETEXTURE8 = IDirect3DTexture8*;
static int failCopy = -1, copies = 0, invalidCopies = 0;
HRESULT D3DXLoadSurfaceFromSurface(IDirect3DSurface8* destination, const void*, const void*,
    IDirect3DSurface8* source, const void*, const void*, DWORD, DWORD) {
    ++copies;
    if (source->refs < 2 || destination->refs < 2 || destination->level != source->level + 1) ++invalidCopies;
    return destination->level == UINT(failCopy) ? TEST_ERROR : D3D_OK;
}

// INSERT_PRODUCTION_METHODS

static int failures = 0, checks = 0;
static void check(bool condition, const char* label) {
    ++checks;
    if (!condition) { ++failures; std::fprintf(stderr, "FAIL: %s\n", label); }
}
static void run(UINT levels, UINT source, int acquisitionFailure, int copyFailure,
    bool descriptionFailure, HRESULT expected, int expectedCopies) {
    IDirect3DTexture8 texture(levels);
    texture.failAcquire = acquisitionFailure; texture.failDescription = descriptionFailure;
    failCopy = copyFailure; copies = invalidCopies = 0;
    check(D3DXFilterTexture(&texture, nullptr, source, D3DX_DEFAULT) == expected, "filter result propagates");
    check(copies == expectedCopies, "only requested consecutive mip levels are copied");
    check(invalidCopies == 0, "each copy holds both caller references");
    for (const auto& surface : texture.surfaces)
        check(surface.refs == 1, "each cache reference survives; no caller reference leaks");
}
int main(int argc, char**) {
    run(4, D3DX_DEFAULT, -1, -1, false, D3D_OK, 3);
    if (argc == 1) {
        run(4, 1, -1, -1, false, D3D_OK, 2);
        run(4, 3, -1, -1, false, D3D_OK, 0);
        run(1, 0, -1, -1, false, D3D_OK, 0);
        run(4, 4, -1, -1, false, D3DERR_INVALIDCALL, 0);
        run(0, D3DX_DEFAULT, -1, -1, false, D3DERR_INVALIDCALL, 0);
        run(4, 0, 0, -1, false, TEST_ERROR, 0);
        run(4, 0, 2, -1, false, TEST_ERROR, 1);
        run(4, 0, -1, 1, false, TEST_ERROR, 1);
        run(4, 0, -1, 3, false, TEST_ERROR, 3);
        run(4, 0, -1, -1, true, TEST_ERROR, 0);
        check(D3DXFilterTexture(nullptr, nullptr, 0, 0) == D3DERR_INVALIDCALL, "null texture rejected");
    }
    std::printf("%d/%d mip filter checks passed\n", checks - failures, checks);
    return failures ? 1 : 0;
}
