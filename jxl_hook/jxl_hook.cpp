#include <windows.h>
#include <d3d9.h>
#include <jxl/decode.h>
#include <jxl/types.h>
#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>

typedef struct _D3DXIMAGE_INFO {
    UINT Width;
    UINT Height;
    UINT Depth;
    UINT MipLevels;
    D3DFORMAT Format;
    D3DRESOURCETYPE ResourceType;
    DWORD ImageFileFormat;
} D3DXIMAGE_INFO;

typedef HRESULT (WINAPI *PFN_D3DXCreateTextureFromFileInMemoryEx)(
    LPDIRECT3DDEVICE9 pDevice,
    LPCVOID pSrcData,
    UINT SrcDataSize,
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
    LPDIRECT3DTEXTURE9 *ppTexture
);

static HMODULE g_hD3DX = NULL;
static PFN_D3DXCreateTextureFromFileInMemoryEx g_pfnTrampolineEx = NULL;

static void LogMsg(const char* fmt, ...) {
    char buf[1024];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    OutputDebugStringA(buf);

    FILE* fp = fopen("jxl_hook.log", "a");
    if (fp) {
        fputs(buf, fp);
        fclose(fp);
    }
}

// libjxl entry points, grabbed at runtime. No import library, no linker arguments, no worries.
static HMODULE g_hLibJXL = NULL;
typedef JxlDecoder* (*pfn_JxlDecoderCreate)(const JxlMemoryManager*);
typedef void (*pfn_JxlDecoderDestroy)(JxlDecoder*);
typedef JxlDecoderStatus (*pfn_JxlDecoderSubscribeEvents)(JxlDecoder*, int);
typedef JxlDecoderStatus (*pfn_JxlDecoderSetInput)(JxlDecoder*, const uint8_t*, size_t);
typedef JxlDecoderStatus (*pfn_JxlDecoderProcessInput)(JxlDecoder*);
typedef JxlDecoderStatus (*pfn_JxlDecoderGetBasicInfo)(const JxlDecoder*, JxlBasicInfo*);
typedef JxlDecoderStatus (*pfn_JxlDecoderImageOutBufferSize)(const JxlDecoder*, const JxlPixelFormat*, size_t*);
typedef JxlDecoderStatus (*pfn_JxlDecoderSetImageOutBuffer)(JxlDecoder*, const JxlPixelFormat*, void*, size_t);

static pfn_JxlDecoderCreate            dyn_JxlDecoderCreate = NULL;
static pfn_JxlDecoderDestroy           dyn_JxlDecoderDestroy = NULL;
static pfn_JxlDecoderSubscribeEvents   dyn_JxlDecoderSubscribeEvents = NULL;
static pfn_JxlDecoderSetInput          dyn_JxlDecoderSetInput = NULL;
static pfn_JxlDecoderProcessInput      dyn_JxlDecoderProcessInput = NULL;
static pfn_JxlDecoderGetBasicInfo      dyn_JxlDecoderGetBasicInfo = NULL;
static pfn_JxlDecoderImageOutBufferSize dyn_JxlDecoderImageOutBufferSize = NULL;
static pfn_JxlDecoderSetImageOutBuffer dyn_JxlDecoderSetImageOutBuffer = NULL;

static bool InitLibJXL() {
    if (g_hLibJXL) return true;
    g_hLibJXL = LoadLibraryA("libjxl.dll");
    if (!g_hLibJXL) {
        LogMsg("[JXL_HOOK] Failed to load libjxl.dll\n");
        return false;
    }
    dyn_JxlDecoderCreate            = (pfn_JxlDecoderCreate)GetProcAddress(g_hLibJXL, "JxlDecoderCreate");
    dyn_JxlDecoderDestroy           = (pfn_JxlDecoderDestroy)GetProcAddress(g_hLibJXL, "JxlDecoderDestroy");
    dyn_JxlDecoderSubscribeEvents   = (pfn_JxlDecoderSubscribeEvents)GetProcAddress(g_hLibJXL, "JxlDecoderSubscribeEvents");
    dyn_JxlDecoderSetInput          = (pfn_JxlDecoderSetInput)GetProcAddress(g_hLibJXL, "JxlDecoderSetInput");
    dyn_JxlDecoderProcessInput      = (pfn_JxlDecoderProcessInput)GetProcAddress(g_hLibJXL, "JxlDecoderProcessInput");
    dyn_JxlDecoderGetBasicInfo      = (pfn_JxlDecoderGetBasicInfo)GetProcAddress(g_hLibJXL, "JxlDecoderGetBasicInfo");
    dyn_JxlDecoderImageOutBufferSize = (pfn_JxlDecoderImageOutBufferSize)GetProcAddress(g_hLibJXL, "JxlDecoderImageOutBufferSize");
    dyn_JxlDecoderSetImageOutBuffer = (pfn_JxlDecoderSetImageOutBuffer)GetProcAddress(g_hLibJXL, "JxlDecoderSetImageOutBuffer");
    return (dyn_JxlDecoderCreate && dyn_JxlDecoderDestroy);
}

static bool IsJXL(const uint8_t* data, size_t size) {
    if (!data || size < 2) return false;
    if (data[0] == 0xFF && data[1] == 0x0A) return true;
    if (size >= 12 && data[0] == 0x00 && data[1] == 0x00 && data[2] == 0x00 && data[3] == 0x0C &&
        data[4] == 'J' && data[5] == 'X' && data[6] == 'L' && data[7] == ' ') return true;
    return false;
}

static HRESULT DecodeJXLToTexture(
    LPDIRECT3DDEVICE9 pDevice,
    LPCVOID pSrcData,
    UINT SrcDataSize,
    UINT ReqWidth,
    UINT ReqHeight,
    DWORD ReqUsage,
    D3DFORMAT ReqFormat,
    D3DPOOL ReqPool,
    D3DXIMAGE_INFO* pSrcInfo,
    LPDIRECT3DTEXTURE9* ppTexture
) {
    if (!InitLibJXL()) return E_FAIL;

    JxlDecoder* dec = dyn_JxlDecoderCreate(NULL);
    if (!dec) return E_FAIL;

    if (JXL_DEC_SUCCESS != dyn_JxlDecoderSubscribeEvents(dec, JXL_DEC_BASIC_INFO | JXL_DEC_FULL_IMAGE)) {
        dyn_JxlDecoderDestroy(dec);
        return E_FAIL;
    }

    if (JXL_DEC_SUCCESS != dyn_JxlDecoderSetInput(dec, (const uint8_t*)pSrcData, SrcDataSize)) {
        dyn_JxlDecoderDestroy(dec);
        return E_FAIL;
    }

    uint32_t xsize = 0, ysize = 0;
    uint8_t* pixels = NULL;
    size_t buffer_size = 0;
    JxlPixelFormat format = {4, JXL_TYPE_UINT8, JXL_NATIVE_ENDIAN, 0}; // RGBA8 (we flip it to BGRA below, eh)

    bool success = false;
    for (;;) {
        JxlDecoderStatus status = dyn_JxlDecoderProcessInput(dec);
        if (status == JXL_DEC_BASIC_INFO) {
            JxlBasicInfo info;
            if (JXL_DEC_SUCCESS != dyn_JxlDecoderGetBasicInfo(dec, &info)) break;
            xsize = info.xsize;
            ysize = info.ysize;
        } else if (status == JXL_DEC_NEED_IMAGE_OUT_BUFFER) {
            if (JXL_DEC_SUCCESS != dyn_JxlDecoderImageOutBufferSize(dec, &format, &buffer_size)) break;
            pixels = (uint8_t*)malloc(buffer_size);
            if (!pixels) break;
            if (JXL_DEC_SUCCESS != dyn_JxlDecoderSetImageOutBuffer(dec, &format, pixels, buffer_size)) break;
        } else if (status == JXL_DEC_FULL_IMAGE) {
            success = true;
        } else if (status == JXL_DEC_SUCCESS) {
            break;
        } else if (status == JXL_DEC_ERROR) {
            break;
        }
    }
    dyn_JxlDecoderDestroy(dec);

    if (!success || !pixels || xsize == 0 || ysize == 0) {
        if (pixels) free(pixels);
        return E_FAIL;
    }

    // D3D9 says A8R8G8B8, which little-endian means BGRA in memory. Swap red and blue, carry on.
    for (size_t i = 0; i < (size_t)xsize * ysize; i++) {
        uint8_t r = pixels[i * 4 + 0];
        uint8_t b = pixels[i * 4 + 2];
        pixels[i * 4 + 0] = b;
        pixels[i * 4 + 2] = r;
    }

    // Drivers get picky about which pool/usage pair they'll accept, so try the lot and take the
    // first one that doesn't complain.
    HRESULT hr = E_FAIL;
    D3DPOOL pools[] = { ReqPool, D3DPOOL_MANAGED, D3DPOOL_DEFAULT, D3DPOOL_SYSTEMMEM };
    DWORD usages[] = { ReqUsage, 0, D3DUSAGE_DYNAMIC };

    LPDIRECT3DTEXTURE9 pTex = NULL;

    for (int p = 0; p < 4 && FAILED(hr); p++) {
        for (int u = 0; u < 3 && FAILED(hr); u++) {
            D3DPOOL pool = pools[p];
            DWORD usage = usages[u];
            if (pool == D3DPOOL_MANAGED && usage != 0) continue;
            if (pool == D3DPOOL_DEFAULT && usage == 0) usage = D3DUSAGE_DYNAMIC;
            hr = pDevice->CreateTexture(xsize, ysize, 1, usage, D3DFMT_A8R8G8B8, pool, &pTex, NULL);
        }
    }

    if (FAILED(hr) || !pTex) {
        LogMsg("[JXL_HOOK] All CreateTexture attempts failed! Last hr: 0x%08X (ReqPool=%d, ReqUsage=%lu)\n", hr, ReqPool, ReqUsage);
        free(pixels);
        return hr;
    }

    D3DLOCKED_RECT rect;
    hr = pTex->LockRect(0, &rect, NULL, 0);
    if (SUCCEEDED(hr)) {
        for (size_t y = 0; y < ysize; y++) {
            memcpy((uint8_t*)rect.pBits + y * rect.Pitch, pixels + y * xsize * 4, xsize * 4);
        }
        pTex->UnlockRect(0);
    } else {
        LogMsg("[JXL_HOOK] LockRect failed: 0x%08X\n", hr);
    }

    *ppTexture = pTex;

    if (pSrcInfo) {
        pSrcInfo->Width = xsize;
        pSrcInfo->Height = ysize;
        pSrcInfo->Depth = 1;
        pSrcInfo->MipLevels = 1;
        pSrcInfo->Format = D3DFMT_A8R8G8B8;
        pSrcInfo->ResourceType = D3DRTYPE_TEXTURE;
        pSrcInfo->ImageFileFormat = 3; // D3DXIFF_PNG. The engine just wants a number here.
    }

    free(pixels);
    LogMsg("[JXL_HOOK] SUCCESS! Decoded JXL: %ux%u (%u bytes) -> Texture: %p\n", xsize, ysize, SrcDataSize, pTex);
    return S_OK;
}

static HRESULT WINAPI Hook_D3DXCreateTextureFromFileInMemoryEx(
    LPDIRECT3DDEVICE9 pDevice,
    LPCVOID pSrcData,
    UINT SrcDataSize,
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
    LPDIRECT3DTEXTURE9 *ppTexture
) {
    if (IsJXL((const uint8_t*)pSrcData, SrcDataSize)) {
        return DecodeJXLToTexture(pDevice, pSrcData, SrcDataSize, Width, Height, Usage, Format, Pool, pSrcInfo, ppTexture);
    }

    if (g_pfnTrampolineEx) {
        return g_pfnTrampolineEx(pDevice, pSrcData, SrcDataSize, Width, Height, MipLevels, Usage, Format, Pool, Filter, MipFilter, ColorKey, pSrcInfo, pPalette, ppTexture);
    }
    return E_FAIL;
}

static void InstallD3DXHook() {
    g_hD3DX = LoadLibraryA("d3dx9_43.dll");
    if (!g_hD3DX) {
        LogMsg("[JXL_HOOK] Failed to load d3dx9_43.dll\n");
        return;
    }

    uint8_t* pTarget = (uint8_t*)GetProcAddress(g_hD3DX, "D3DXCreateTextureFromFileInMemoryEx");
    if (!pTarget) {
        LogMsg("[JXL_HOOK] Failed to get D3DXCreateTextureFromFileInMemoryEx\n");
        return;
    }

    uint8_t* tramp = (uint8_t*)VirtualAlloc(NULL, 64, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
    if (!tramp) return;

    memcpy(tramp, pTarget, 5);
    tramp[5] = 0xE9;
    *(int32_t*)(tramp + 6) = (int32_t)(pTarget + 5) - (int32_t)(tramp + 10);
    g_pfnTrampolineEx = (PFN_D3DXCreateTextureFromFileInMemoryEx)tramp;

    DWORD oldProtect;
    VirtualProtect(pTarget, 5, PAGE_EXECUTE_READWRITE, &oldProtect);
    pTarget[0] = 0xE9;
    *(int32_t*)(pTarget + 1) = (int32_t)((uint8_t*)Hook_D3DXCreateTextureFromFileInMemoryEx) - (int32_t)(pTarget + 5);
    VirtualProtect(pTarget, 5, oldProtect, &oldProtect);

    FlushInstructionCache(GetCurrentProcess(), pTarget, 5);
    LogMsg("[JXL_HOOK] Hook installed on D3DXCreateTextureFromFileInMemoryEx (%p -> %p)!\n", pTarget, Hook_D3DXCreateTextureFromFileInMemoryEx);
}

extern "C" {

BOOL WINAPI DllMain(HINSTANCE hinstDLL, DWORD fdwReason, LPVOID lpvReserved) {
    if (fdwReason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(hinstDLL);
        LogMsg("[JXL_HOOK] Standalone jxl_hook.dll attached to PID %lu\n", GetCurrentProcessId());
        InstallD3DXHook();
    }
    return TRUE;
}

}
