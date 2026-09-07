// GLM_ENABLE_EXPERIMENTAL is passed via target_compile_definitions in CMakeLists.txt.
// Do NOT redefine it here -- that causes -Wmacro-redefined with Clang.

#include "d3dx8core.h"

// Igroteka wasm: boot trace logs are off by default — thousands per boot,
// each crossing wasm->JS. Enable with IG_TRACE=1 (WPTrace.h; the browser
// shell sets it under ?debug=1).
#include "WPTrace.h"

// GeneralsX @build felipebraz 20/06/2025 GLI causes make_vec4 ambiguity with Apple Clang (GLM version mismatch).
// On macOS, exclude GLI and use stub implementations for the surface scaling path.
#if !defined(__APPLE__) && !defined(__EMSCRIPTEN__)
#include <gli/gli.hpp>
#include <gli/generate_mipmaps.hpp>
#endif

HRESULT WINAPID3DXGetErrorStringA(HRESULT hr, LPSTR pBuffer, UINT BufferLen)
{
	return D3DERR_INVALIDCALL;
}

HRESULT WINAPI
D3DXCreateTexture(LPDIRECT3DDEVICE8 pDevice,
	UINT Width,
	UINT Height,
	UINT MipLevels,
	DWORD Usage,
	D3DFORMAT Format,
	D3DPOOL Pool,
	LPDIRECT3DTEXTURE8 *ppTexture)
{
	// GeneralsX @bugfix fbraz 04/05/2026 Headless replay can request texture creation before DX8 device initialization.
	if (ppTexture == nullptr || pDevice == nullptr)
	{
		if (ppTexture != nullptr)
		{
			*ppTexture = nullptr;
		}

		return D3DERR_INVALIDCALL;
	}

	return pDevice->CreateTexture(Width, Height, MipLevels, Usage, Format, Pool, ppTexture);
}

#include <iostream>

HRESULT WINAPI
D3DXCreateTextureFromFileExA(
	LPDIRECT3DDEVICE8 pDevice,
	LPCSTR pSrcFile,
	UINT Width,
	UINT Height,
	UINT MipLevels,
	DWORD Usage,
	D3DFORMAT Format,
	D3DPOOL Pool,

	DWORD Filter,
	DWORD MipFilter,
	D3DCOLOR ColorKey,
	D3DXIMAGE_INFO *pSrcInfo,
	PALETTEENTRY *pPalette,

	LPDIRECT3DTEXTURE8 *ppTexture)
{
	HRESULT hr;

	return D3DERR_INVALIDCALL;
}

HRESULT WINAPI
D3DXLoadSurfaceFromSurface(
	LPDIRECT3DSURFACE8 pDestSurface,
	CONST PALETTEENTRY *pDestPalette,
	CONST RECT *pDestRect,
	LPDIRECT3DSURFACE8 pSrcSurface,
	CONST PALETTEENTRY *pSrcPalette,
	CONST RECT *pSrcRect,
	DWORD Filter,
	D3DCOLOR ColorKey)
{
	// WarPowers @fix 05/09/2026 Validate surface copies and retain each lock until cleanup.
	if (!pSrcSurface || !pDestSurface || pSrcPalette || pDestPalette || ColorKey)
	{
		return D3DERR_INVALIDCALL;
	}
	D3DSURFACE_DESC descSrc = {}, descDest = {};
	HRESULT hr = pSrcSurface->GetDesc(&descSrc);
	if (FAILED(hr)) return hr;
	hr = pDestSurface->GetDesc(&descDest);
	if (FAILED(hr)) return hr;

#ifdef __EMSCRIPTEN__
	if (wpTraceEnabled() && (descSrc.Width >= 512 || descDest.Width >= 256))
		fprintf(stderr, "[LSFS] src=%ux%u fmt=%u dst=%ux%u fmt=%u\n",
		        descSrc.Width, descSrc.Height, (unsigned)descSrc.Format,
		        descDest.Width, descDest.Height, (unsigned)descDest.Format);
#endif

	if (descSrc.Format != descDest.Format)
	{
		// Currently we only support scaling between formats of the same type
		return D3DERR_INVALIDCALL;
	}

	// Equal-size region copies are used by bitmap tiling and font image packing.
	// Compressed regions must cover complete blocks, except at the surface edge.
	UINT blockSize = 1, blockBytes = 0;
	switch (descSrc.Format)
	{
		case D3DFMT_A8R8G8B8: case D3DFMT_X8R8G8B8: blockBytes = 4; break;
		case D3DFMT_R8G8B8: blockBytes = 3; break;
		case D3DFMT_A1R5G5B5: case D3DFMT_X1R5G5B5: case D3DFMT_R5G6B5:
		case D3DFMT_A4R4G4B4: case D3DFMT_A8L8: case D3DFMT_V8U8:
		case D3DFMT_L6V5U5: blockBytes = 2; break;
		case D3DFMT_X8L8V8U8: blockBytes = 4; break;
		case D3DFMT_A8: case D3DFMT_L8: blockBytes = 1; break;
		case D3DFMT_DXT1: blockSize = 4; blockBytes = 8; break;
		case D3DFMT_DXT2: case D3DFMT_DXT3: case D3DFMT_DXT4: case D3DFMT_DXT5:
			blockSize = 4; blockBytes = 16; break;
		default: return D3DERR_INVALIDCALL;
	}
	auto region = [blockSize](const RECT* requested, const D3DSURFACE_DESC& desc, RECT& result) -> bool {
		// RECT coordinates are signed. Reject dimensions that cannot be represented.
		if (!desc.Width || !desc.Height || desc.Width > 0x7fffffffU || desc.Height > 0x7fffffffU) return false;
		result = requested ? *requested : RECT{0, 0, (LONG)desc.Width, (LONG)desc.Height};
		return result.left >= 0 && result.top >= 0 && result.right > result.left && result.bottom > result.top &&
			(UINT)result.right <= desc.Width && (UINT)result.bottom <= desc.Height &&
			result.left % blockSize == 0 && result.top % blockSize == 0 &&
			(result.right % blockSize == 0 || (UINT)result.right == desc.Width) &&
			(result.bottom % blockSize == 0 || (UINT)result.bottom == desc.Height);
	};
	RECT srcArea, destArea;
	if (!region(pSrcRect, descSrc, srcArea) || !region(pDestRect, descDest, destArea)) return D3DERR_INVALIDCALL;
	const size_t srcBlocks = (size_t(descSrc.Width) + blockSize - 1) / blockSize;
	const size_t destBlocks = (size_t(descDest.Width) + blockSize - 1) / blockSize;
	if (srcBlocks > 0x7fffffffU / blockBytes || destBlocks > 0x7fffffffU / blockBytes) return D3DERR_INVALIDCALL;
	const size_t srcRowBytes = srcBlocks * blockBytes, destRowBytes = destBlocks * blockBytes;
	const UINT srcRows = (descSrc.Height + blockSize - 1) / blockSize;
	const UINT destRows = (descDest.Height + blockSize - 1) / blockSize;
	descSrc.Width = srcArea.right - srcArea.left;
	descSrc.Height = srcArea.bottom - srcArea.top;
	descDest.Width = destArea.right - destArea.left;
	descDest.Height = destArea.bottom - destArea.top;
	const bool sameSize = descDest.Width == descSrc.Width && descDest.Height == descSrc.Height;
	// WarPowers @fix 05/09/2026 Rectangular mip chains retain a one-pixel axis.
	const bool halfSize = descDest.Width == (descSrc.Width > 1 ? descSrc.Width / 2 : 1) &&
		descDest.Height == (descSrc.Height > 1 ? descSrc.Height / 2 : 1);
	const bool supportedMipFormat = descSrc.Format == D3DFMT_A1R5G5B5 || descSrc.Format == D3DFMT_A4R4G4B4 ||
		descSrc.Format == D3DFMT_R5G6B5 || descSrc.Format == D3DFMT_A8R8G8B8 || descSrc.Format == D3DFMT_X8R8G8B8;
	if (!sameSize && (!supportedMipFormat || !halfSize))
	{
		return D3DERR_INVALIDCALL;
	}
	if (pSrcSurface == pDestSurface)
	{
		// An exact self-copy is already complete. Overlapping in-place conversion is unsupported.
		return sameSize && srcArea.left == destArea.left && srcArea.top == destArea.top ? D3D_OK : D3DERR_INVALIDCALL;
	}
	struct SurfaceLock
	{
		LPDIRECT3DSURFACE8 surface;
		~SurfaceLock() { if (surface) surface->UnlockRect(); }
		HRESULT release() { LPDIRECT3DSURFACE8 held = surface; surface = NULL; return held->UnlockRect(); }
	};
	D3DLOCKED_RECT srcRect = {}, destRect = {};
	hr = pSrcSurface->LockRect(&srcRect, NULL, D3DLOCK_READONLY);
	if (FAILED(hr)) return hr;
	SurfaceLock srcLock = {pSrcSurface};
	hr = pDestSurface->LockRect(&destRect, NULL, 0);
	if (FAILED(hr)) return hr;
	SurfaceLock destLock = {pDestSurface};
	if (!srcRect.pBits || !destRect.pBits || srcRect.Pitch <= 0 || destRect.Pitch <= 0 ||
		size_t(srcRect.Pitch) < srcRowBytes || size_t(destRect.Pitch) < destRowBytes ||
		srcRows > size_t(-1) / size_t(srcRect.Pitch) || destRows > size_t(-1) / size_t(destRect.Pitch) ||
		(!sameSize && (srcRect.Pitch % blockBytes || destRect.Pitch % blockBytes))) return D3DERR_INVALIDCALL;
	srcRect.pBits = (unsigned char*)srcRect.pBits + size_t(srcArea.top / blockSize) * srcRect.Pitch + size_t(srcArea.left / blockSize) * blockBytes;
	destRect.pBits = (unsigned char*)destRect.pBits + size_t(destArea.top / blockSize) * destRect.Pitch + size_t(destArea.left / blockSize) * blockBytes;
	auto finish = [&](HRESULT result) -> HRESULT {
		const HRESULT destResult = destLock.release(), srcResult = srcLock.release();
		return FAILED(result) ? result : FAILED(destResult) ? destResult : srcResult;
	};

	if (sameSize)
	{
		const size_t rowBytes = ((size_t(descSrc.Width) + blockSize - 1) / blockSize) * blockBytes;
		const UINT rows = (descSrc.Height + blockSize - 1) / blockSize;
		for (UINT y = 0; y < rows; ++y)
			memcpy((unsigned char*)destRect.pBits + size_t(y) * destRect.Pitch,
				(const unsigned char*)srcRect.pBits + size_t(y) * srcRect.Pitch, rowBytes);
		return finish(D3D_OK);
	}

#if !defined(__APPLE__) && !defined(__EMSCRIPTEN__)
	// GeneralsX @bugfix Antigravity 26/06/2026 Linux: GLI lacks support for A4R4G4B4/R5G6B5 formats.
	// We use manual box filter downsampling for these formats to fix black infantry rendering.
	if (halfSize)
	{
		if (descSrc.Format == D3DFMT_A4R4G4B4)
		{
			// Box filter for A4R4G4B4: average 2x2 blocks
			const uint16_t *src = (const uint16_t *)srcRect.pBits;
			uint16_t *dst = (uint16_t *)destRect.pBits;
			uint32_t srcPitch16 = srcRect.Pitch / 2;
			uint32_t dstPitch16 = destRect.Pitch / 2;

			for (uint32_t y = 0; y < descDest.Height; y++)
			{
				for (uint32_t x = 0; x < descDest.Width; x++)
				{
					uint32_t sx = x * 2;
					uint32_t sy = y * 2;
					uint32_t nx = sx + 1 < descSrc.Width ? sx + 1 : sx;
					uint32_t ny = sy + 1 < descSrc.Height ? sy + 1 : sy;
					uint16_t p00 = src[sy * srcPitch16 + sx];
					uint16_t p10 = src[sy * srcPitch16 + nx];
					uint16_t p01 = src[ny * srcPitch16 + sx];
					uint16_t p11 = src[ny * srcPitch16 + nx];

					// Extract and average each channel (A4 R4 G4 B4)
					uint32_t a = (((p00 >> 12) & 0x0F) + ((p10 >> 12) & 0x0F) +
					              ((p01 >> 12) & 0x0F) + ((p11 >> 12) & 0x0F) + 2) >> 2;
					uint32_t r = (((p00 >> 8) & 0x0F) + ((p10 >> 8) & 0x0F) +
					              ((p01 >> 8) & 0x0F) + ((p11 >> 8) & 0x0F) + 2) >> 2;
					uint32_t g = (((p00 >> 4) & 0x0F) + ((p10 >> 4) & 0x0F) +
					              ((p01 >> 4) & 0x0F) + ((p11 >> 4) & 0x0F) + 2) >> 2;
					uint32_t b = ((p00 & 0x0F) + (p10 & 0x0F) +
					              (p01 & 0x0F) + (p11 & 0x0F) + 2) >> 2;

					dst[y * dstPitch16 + x] = (uint16_t)((a << 12) | (r << 8) | (g << 4) | b);
				}
			}
			return finish(D3D_OK);
		}
		else if (descSrc.Format == D3DFMT_R5G6B5)
		{
			// Box filter for R5G6B5: average 2x2 blocks
			const uint16_t *src = (const uint16_t *)srcRect.pBits;
			uint16_t *dst = (uint16_t *)destRect.pBits;
			uint32_t srcPitch16 = srcRect.Pitch / 2;
			uint32_t dstPitch16 = destRect.Pitch / 2;

			for (uint32_t y = 0; y < descDest.Height; y++)
			{
				for (uint32_t x = 0; x < descDest.Width; x++)
				{
					uint32_t sx = x * 2;
					uint32_t sy = y * 2;
					uint32_t nx = sx + 1 < descSrc.Width ? sx + 1 : sx;
					uint32_t ny = sy + 1 < descSrc.Height ? sy + 1 : sy;
					uint16_t p00 = src[sy * srcPitch16 + sx];
					uint16_t p10 = src[sy * srcPitch16 + nx];
					uint16_t p01 = src[ny * srcPitch16 + sx];
					uint16_t p11 = src[ny * srcPitch16 + nx];

					// Extract and average each channel (R5 G6 B5)
					uint32_t r = (((p00 >> 11) & 0x1F) + ((p11 >> 11) & 0x1F) +
					              ((p01 >> 11) & 0x1F) + ((p10 >> 11) & 0x1F) + 2) >> 2;
					uint32_t g = (((p00 >> 5) & 0x3F) + ((p11 >> 5) & 0x3F) +
					              ((p01 >> 5) & 0x3F) + ((p10 >> 5) & 0x3F) + 2) >> 2;
					uint32_t b = ((p00 & 0x1F) + (p11 & 0x1F) +
					              (p01 & 0x1F) + (p10 & 0x1F) + 2) >> 2;

					dst[y * dstPitch16 + x] = (uint16_t)((r << 11) | (g << 5) | b);
				}
			}
			return finish(D3D_OK);
		}
	}
	// Pick a compatible format
	gli::format imageFormat = gli::format::FORMAT_RGBA8_UNORM_PACK8;

	assert(descSrc.Format == D3DFMT_A8R8G8B8 || descSrc.Format == D3DFMT_A1R5G5B5 || descSrc.Format == D3DFMT_X8R8G8B8);
	if (descSrc.Format == D3DFMT_A8R8G8B8 || descSrc.Format == D3DFMT_X8R8G8B8)
	{
		imageFormat = gli::format::FORMAT_RGBA8_UNORM_PACK8;	
	}
	else if (descSrc.Format == D3DFMT_A1R5G5B5)
	{
		imageFormat = gli::format::FORMAT_A1RGB5_UNORM_PACK16;
	}
	else
	{
		return D3DERR_INVALIDCALL;
	}

	// Create two levels of mips, 0 and 1
	gli::texture2d texSrc(imageFormat, gli::extent2d(descSrc.Width, descSrc.Height), 2);

	// Copy the data to level 0
	const size_t srcPackedPitch = size_t(descSrc.Width) * blockBytes;
	for (UINT y = 0; y < descSrc.Height; ++y)
		memcpy((unsigned char*)texSrc.data(0, 0, 0) + size_t(y) * srcPackedPitch,
			(const unsigned char*)srcRect.pBits + size_t(y) * srcRect.Pitch, srcPackedPitch);
	// Generate mip 1 from level 0
	gli::texture2d mipMap = gli::generate_mipmaps(texSrc, gli::filter::FILTER_LINEAR);

	const size_t destPackedPitch = size_t(descDest.Width) * blockBytes;
	if (mipMap.size(1) != destPackedPitch * descDest.Height)
	{
		// The generated dimension would not be the same as the destination
		// This does not happen in the game, yet let's not allow it
		return finish(D3DERR_INVALIDCALL);
	}

	// Copy mip level 1 to the destination
	for (UINT y = 0; y < descDest.Height; ++y)
		memcpy((unsigned char*)destRect.pBits + size_t(y) * destRect.Pitch,
			(const unsigned char*)mipMap.data(0, 0, 1) + size_t(y) * destPackedPitch, destPackedPitch);

	return finish(D3D_OK);
#else
	// GeneralsX @bugfix BenderAI 07/03/2026 macOS: GLI not available due to Apple Clang ambiguity.
	// Implement manual box filter downsampling for mipmap generation.
	// This is critical for terrain textures - without mipmaps, terrain renders black.
	if (halfSize)
	{
		if (descSrc.Format == D3DFMT_A1R5G5B5)
		{
			// Box filter for A1R5G5B5: average 2x2 blocks
			const uint16_t *src = (const uint16_t *)srcRect.pBits;
			uint16_t *dst = (uint16_t *)destRect.pBits;
			uint32_t srcPitch16 = srcRect.Pitch / 2;
			uint32_t dstPitch16 = destRect.Pitch / 2;

			for (uint32_t y = 0; y < descDest.Height; y++)
			{
				for (uint32_t x = 0; x < descDest.Width; x++)
				{
					uint32_t sx = x * 2;
					uint32_t sy = y * 2;
					uint32_t nx = sx + 1 < descSrc.Width ? sx + 1 : sx;
					uint32_t ny = sy + 1 < descSrc.Height ? sy + 1 : sy;
					uint16_t p00 = src[sy * srcPitch16 + sx];
					uint16_t p10 = src[sy * srcPitch16 + nx];
					uint16_t p01 = src[ny * srcPitch16 + sx];
					uint16_t p11 = src[ny * srcPitch16 + nx];

					// Extract and average each channel (A1 R5 G5 B5)
					uint32_t r = (((p00 >> 10) & 0x1F) + ((p10 >> 10) & 0x1F) +
					              ((p01 >> 10) & 0x1F) + ((p11 >> 10) & 0x1F) + 2) >> 2;
					uint32_t g = (((p00 >> 5) & 0x1F) + ((p10 >> 5) & 0x1F) +
					              ((p01 >> 5) & 0x1F) + ((p11 >> 5) & 0x1F) + 2) >> 2;
					uint32_t b = ((p00 & 0x1F) + (p10 & 0x1F) +
					              (p01 & 0x1F) + (p11 & 0x1F) + 2) >> 2;
					// Alpha: majority vote (set if 2+ pixels have alpha set)
					uint32_t a = ((p00 >> 15) + (p10 >> 15) + (p01 >> 15) + (p11 >> 15)) >= 2 ? 1 : 0;

					dst[y * dstPitch16 + x] = (uint16_t)((a << 15) | (r << 10) | (g << 5) | b);
				}
			}
		}
		else if (descSrc.Format == D3DFMT_A8R8G8B8 || descSrc.Format == D3DFMT_X8R8G8B8)
		{
			// Box filter for A8R8G8B8/X8R8G8B8: average 2x2 blocks
			const uint32_t *src = (const uint32_t *)srcRect.pBits;
			uint32_t *dst = (uint32_t *)destRect.pBits;
			uint32_t srcPitch32 = srcRect.Pitch / 4;
			uint32_t dstPitch32 = destRect.Pitch / 4;

			for (uint32_t y = 0; y < descDest.Height; y++)
			{
				for (uint32_t x = 0; x < descDest.Width; x++)
				{
					uint32_t sx = x * 2;
					uint32_t sy = y * 2;
					uint32_t nx = sx + 1 < descSrc.Width ? sx + 1 : sx;
					uint32_t ny = sy + 1 < descSrc.Height ? sy + 1 : sy;
					uint32_t p00 = src[sy * srcPitch32 + sx];
					uint32_t p10 = src[sy * srcPitch32 + nx];
					uint32_t p01 = src[ny * srcPitch32 + sx];
					uint32_t p11 = src[ny * srcPitch32 + nx];

					uint32_t a = (((p00 >> 24) & 0xFF) + ((p10 >> 24) & 0xFF) +
					              ((p01 >> 24) & 0xFF) + ((p11 >> 24) & 0xFF) + 2) >> 2;
					uint32_t r = (((p00 >> 16) & 0xFF) + ((p10 >> 16) & 0xFF) +
					              ((p01 >> 16) & 0xFF) + ((p11 >> 16) & 0xFF) + 2) >> 2;
					uint32_t g = (((p00 >> 8) & 0xFF) + ((p10 >> 8) & 0xFF) +
					              ((p01 >> 8) & 0xFF) + ((p11 >> 8) & 0xFF) + 2) >> 2;
					uint32_t b = ((p00 & 0xFF) + (p10 & 0xFF) +
					              (p01 & 0xFF) + (p11 & 0xFF) + 2) >> 2;

					dst[y * dstPitch32 + x] = (a << 24) | (r << 16) | (g << 8) | b;
				}
			}
		}
		else if (descSrc.Format == D3DFMT_A4R4G4B4)
		{
			// Box filter for A4R4G4B4: average 2x2 blocks
			const uint16_t *src = (const uint16_t *)srcRect.pBits;
			uint16_t *dst = (uint16_t *)destRect.pBits;
			uint32_t srcPitch16 = srcRect.Pitch / 2;
			uint32_t dstPitch16 = destRect.Pitch / 2;

			for (uint32_t y = 0; y < descDest.Height; y++)
			{
				for (uint32_t x = 0; x < descDest.Width; x++)
				{
					uint32_t sx = x * 2;
					uint32_t sy = y * 2;
					uint32_t nx = sx + 1 < descSrc.Width ? sx + 1 : sx;
					uint32_t ny = sy + 1 < descSrc.Height ? sy + 1 : sy;
					uint16_t p00 = src[sy * srcPitch16 + sx];
					uint16_t p10 = src[sy * srcPitch16 + nx];
					uint16_t p01 = src[ny * srcPitch16 + sx];
					uint16_t p11 = src[ny * srcPitch16 + nx];

					// Extract and average each channel (A4 R4 G4 B4)
					uint32_t a = (((p00 >> 12) & 0x0F) + ((p10 >> 12) & 0x0F) +
					              ((p01 >> 12) & 0x0F) + ((p11 >> 12) & 0x0F) + 2) >> 2;
					uint32_t r = (((p00 >> 8) & 0x0F) + ((p10 >> 8) & 0x0F) +
					              ((p01 >> 8) & 0x0F) + ((p11 >> 8) & 0x0F) + 2) >> 2;
					uint32_t g = (((p00 >> 4) & 0x0F) + ((p10 >> 4) & 0x0F) +
					              ((p01 >> 4) & 0x0F) + ((p11 >> 4) & 0x0F) + 2) >> 2;
					uint32_t b = ((p00 & 0x0F) + (p10 & 0x0F) +
					              (p01 & 0x0F) + (p11 & 0x0F) + 2) >> 2;

					dst[y * dstPitch16 + x] = (uint16_t)((a << 12) | (r << 8) | (g << 4) | b);
				}
			}
		}
		else if (descSrc.Format == D3DFMT_R5G6B5)
		{
			// Box filter for R5G6B5: average 2x2 blocks
			const uint16_t *src = (const uint16_t *)srcRect.pBits;
			uint16_t *dst = (uint16_t *)destRect.pBits;
			uint32_t srcPitch16 = srcRect.Pitch / 2;
			uint32_t dstPitch16 = destRect.Pitch / 2;

			for (uint32_t y = 0; y < descDest.Height; y++)
			{
				for (uint32_t x = 0; x < descDest.Width; x++)
				{
					uint32_t sx = x * 2;
					uint32_t sy = y * 2;
					uint32_t nx = sx + 1 < descSrc.Width ? sx + 1 : sx;
					uint32_t ny = sy + 1 < descSrc.Height ? sy + 1 : sy;
					uint16_t p00 = src[sy * srcPitch16 + sx];
					uint16_t p10 = src[sy * srcPitch16 + nx];
					uint16_t p01 = src[ny * srcPitch16 + sx];
					uint16_t p11 = src[ny * srcPitch16 + nx];

					// Extract and average each channel (R5 G6 B5)
					uint32_t r = (((p00 >> 11) & 0x1F) + ((p11 >> 11) & 0x1F) +
					              ((p01 >> 11) & 0x1F) + ((p10 >> 11) & 0x1F) + 2) >> 2;
					uint32_t g = (((p00 >> 5) & 0x3F) + ((p11 >> 5) & 0x3F) +
					              ((p01 >> 5) & 0x3F) + ((p10 >> 5) & 0x3F) + 2) >> 2;
					uint32_t b = ((p00 & 0x1F) + (p11 & 0x1F) +
					              (p01 & 0x1F) + (p10 & 0x1F) + 2) >> 2;

					dst[y * dstPitch16 + x] = (uint16_t)((r << 11) | (g << 5) | b);
				}
			}
		}
		else
		{
			return finish(D3DERR_INVALIDCALL);
		}

		return finish(D3D_OK);
	}

	// Non-power-of-two scaling not supported
#ifdef __EMSCRIPTEN__
	fprintf(stderr, "[LSFS] UNSUPPORTED scale %ux%u -> %ux%u fmt=%u\n",
	        descSrc.Width, descSrc.Height, descDest.Width, descDest.Height,
	        (unsigned)descSrc.Format);
#endif
	return finish(D3DERR_INVALIDCALL);
#endif
}

HRESULT WINAPI
D3DXGetErrorStringA(
	HRESULT hr,
	LPSTR pBuffer,
	UINT BufferLen)
{
	return D3DERR_INVALIDCALL;
}

// Taken and adopted from Wine 3.21
HRESULT WINAPI
D3DXFilterTexture(
	LPDIRECT3DBASETEXTURE8 pBaseTexture,
	CONST PALETTEENTRY *pPalette,
	UINT SrcLevel,
	DWORD Filter)
{
	HRESULT hr = D3DERR_INVALIDCALL;
	// WarPowers @fix 05/09/2026 Keep one owned reference while walking mip surfaces.
	if (!pBaseTexture)
	{
		return D3DERR_INVALIDCALL;
	}
	if (SrcLevel == D3DX_DEFAULT)
	{
		SrcLevel = 0;
	}
	if (SrcLevel >= pBaseTexture->GetLevelCount())
	{
		return D3DERR_INVALIDCALL;
	}

	D3DRESOURCETYPE type;
	switch (type = pBaseTexture->GetType())
	{
		case D3DRTYPE_TEXTURE:
		{
			IDirect3DTexture8 *tex = (IDirect3DTexture8 *)pBaseTexture;
			IDirect3DSurface8 *topsurf, *mipsurf;
			D3DSURFACE_DESC desc;
			hr = tex->GetLevelDesc(SrcLevel, &desc);
			if (FAILED(hr))
			{
				return hr;
			}
			if (Filter == D3DX_DEFAULT)
			{
				Filter = D3DX_FILTER_BOX;
			}

			UINT Level = SrcLevel + 1;
			hr = tex->GetSurfaceLevel(SrcLevel, &topsurf);
			if (FAILED(hr))
			{
				return hr;
			}

			while (Level < tex->GetLevelCount())
			{
				hr = tex->GetSurfaceLevel(Level, &mipsurf);
				if (FAILED(hr))
				{
					topsurf->Release();
					return hr;
				}
#ifdef __EMSCRIPTEN__
				if (desc.Width >= 512)
					if (wpTraceEnabled()) fprintf(stderr, "[FILTER_PASS] level=%d top=%p mip=%p\n", Level, (void*)topsurf, (void*)mipsurf);
#endif
				// Copy the data
				hr = D3DXLoadSurfaceFromSurface(mipsurf, NULL, NULL, topsurf, NULL, NULL, Filter, 0);

				// The old source is finished. The new mip remains owned as the next
				// source; releasing it here would borrow the texture's cache reference
				// and double-release the final mip when the loop ends.
				topsurf->Release();
				if (FAILED(hr))
				{
					mipsurf->Release();
					return hr;
				}
				topsurf = mipsurf;

				Level++;
			}

			topsurf->Release();
		}
		return D3D_OK;
	}

	return D3D_OK;
}

HRESULT WINAPI
D3DXCreateCubeTexture(
	LPDIRECT3DDEVICE8 pDevice,
	UINT Size,
	UINT MipLevels,
	DWORD Usage,
	D3DFORMAT Format,
	D3DPOOL Pool,
	LPDIRECT3DCUBETEXTURE8 *ppCubeTexture)
{
	// Also unused by the game
	return D3DERR_INVALIDCALL;
}

HRESULT WINAPI
D3DXCreateVolumeTexture(
	LPDIRECT3DDEVICE8 pDevice,
	UINT Width,
	UINT Height,
	UINT Depth,
	UINT MipLevels,
	DWORD Usage,
	D3DFORMAT Format,
	D3DPOOL Pool,
	LPDIRECT3DVOLUMETEXTURE8 *ppVolumeTexture)
{
	// Used to create volume textures by the texture loader
	// Actually not used in the game
	return D3DERR_INVALIDCALL;
}

HRESULT WINAPI
D3DXAssembleShader(
	LPCVOID pSrcData,
	UINT SrcDataLen,
	DWORD Flags,
	LPD3DXBUFFER *ppConstants,
	LPD3DXBUFFER *ppCompiledShader,
	LPD3DXBUFFER *ppCompilationErrors)
{
	// Called to create water shader amongst other things
	// Code seems to be handle assembly failing
	return D3DERR_INVALIDCALL;
}

HRESULT WINAPI
D3DXAssembleShaderFromFileA(
	LPCSTR pSrcFile,
	DWORD Flags,
	LPD3DXBUFFER *ppConstants,
	LPD3DXBUFFER *ppCompiledShader,
	LPD3DXBUFFER *ppCompilationErrors)
{
	// Not required by the game
	return D3DERR_INVALIDCALL;
}

// Taken from Wine at ca46949
static UINT Get_TexCoord_Size_From_FVF(DWORD FVF, int tex_num)
{
    return (((((FVF) >> (16 + (2 * (tex_num)))) + 1) & 0x03) + 1);
}

UINT WINAPI D3DXGetFVFVertexSize(DWORD FVF)
{
    DWORD size = 0;
    UINT i;
    UINT numTextures = (FVF & D3DFVF_TEXCOUNT_MASK) >> D3DFVF_TEXCOUNT_SHIFT;

    if (FVF & D3DFVF_NORMAL) size += sizeof(D3DVECTOR);
    if (FVF & D3DFVF_DIFFUSE) size += sizeof(DWORD);
    if (FVF & D3DFVF_SPECULAR) size += sizeof(DWORD);
    if (FVF & D3DFVF_PSIZE) size += sizeof(DWORD);

    switch (FVF & D3DFVF_POSITION_MASK)
    {
        case D3DFVF_XYZ:    size += sizeof(D3DVECTOR); break;
        case D3DFVF_XYZRHW: size += 4 * sizeof(FLOAT); break;
        case D3DFVF_XYZB1:  size += 4 * sizeof(FLOAT); break;
        case D3DFVF_XYZB2:  size += 5 * sizeof(FLOAT); break;
        case D3DFVF_XYZB3:  size += 6 * sizeof(FLOAT); break;
        case D3DFVF_XYZB4:  size += 7 * sizeof(FLOAT); break;
        case D3DFVF_XYZB5:  size += 8 * sizeof(FLOAT); break;
		// Not present in D3D8, thus uncommented
        //case D3DFVF_XYZW:   size += 4 * sizeof(FLOAT); break;
    }

    for (i = 0; i < numTextures; i++)
    {
        size += Get_TexCoord_Size_From_FVF(FVF, i) * sizeof(FLOAT);
    }

    return size;
}
