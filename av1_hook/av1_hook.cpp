#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <dshow.h>
#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>

// ============================================================================
// AdvHD DirectShow AV1 Registration-Free Decoder Hook
// Purpose: Enables AdvHD engine to play modern AV1 video files (MP4/MKV)
//          via in-process LAV Filters without modifying system registry.
// ============================================================================

static void LogMsg(const char* fmt, ...) {
    char buf[1024];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    OutputDebugStringA(buf);

    FILE* fp = fopen("av1_hook.log", "a");
    if (fp) {
        fputs(buf, fp);
        fclose(fp);
    }
}

// LAV Filters CLSIDs
static const GUID GUID_LAVSplitterSource = { 0xB98D13E7, 0x55DB, 0x4385, { 0xA3, 0x3D, 0x09, 0xFD, 0x1B, 0xA2, 0x63, 0x38 } };
static const GUID GUID_LAVVideo          = { 0xEE30215D, 0x164F, 0x4A92, { 0xA4, 0xEB, 0x9D, 0x4C, 0x13, 0x39, 0x0F, 0x9F } };
static const GUID GUID_LAVAudio          = { 0xE8E73B6B, 0x4CB3, 0x44A4, { 0xBE, 0x99, 0x4F, 0x7B, 0xCB, 0x96, 0xE4, 0x91 } };

// DirectShow / COM function signatures
typedef HRESULT (WINAPI *PFN_CoCreateInstance)(REFCLSID rclsid, LPUNKNOWN pUnkOuter, DWORD dwClsContext, REFIID riid, LPVOID *ppv);
typedef HRESULT (WINAPI *PFN_AddSourceFilter)(IGraphBuilder* pThis, LPCWSTR lpcwstrFileName, LPCWSTR lpcwstrFilterName, IBaseFilter** ppFilter);
typedef HRESULT (WINAPI *PFN_RenderFile)(IGraphBuilder* pThis, LPCWSTR lpcwstrFile, LPCWSTR lpcwstrPlayList);
typedef HRESULT (WINAPI *PFN_FindPin)(IBaseFilter* pThis, LPCWSTR Id, IPin** ppPin);
typedef HRESULT (STDAPICALLTYPE *PFN_DllGetClassObject)(REFCLSID rclsid, REFIID riid, LPVOID *ppv);

static PFN_CoCreateInstance g_pfnRealCoCreateInstance = NULL;
static PFN_AddSourceFilter   g_pfnRealAddSourceFilter   = NULL;
static PFN_RenderFile        g_pfnRealRenderFile        = NULL;
static PFN_FindPin           g_pfnRealFindPin           = NULL;
static HMODULE               g_hSelfModule              = NULL;

// Resolve LAV Filter path (supports both ./lav/ and ./)
static void GetLAVPath(const wchar_t* axName, wchar_t* outPath, DWORD maxLen) {
    wchar_t selfDir[MAX_PATH] = {0};
    GetModuleFileNameW(g_hSelfModule, selfDir, MAX_PATH);
    wchar_t* pSlash = wcsrchr(selfDir, L'\\');
    if (pSlash) *(pSlash + 1) = L'\0';

    // 1. Try <selfDir>\lav\<axName>
    lstrcpyW(outPath, selfDir);
    lstrcatW(outPath, L"lav\\");
    lstrcatW(outPath, axName);
    if (GetFileAttributesW(outPath) != INVALID_FILE_ATTRIBUTES) return;

    // 2. Try <selfDir>\<axName>
    lstrcpyW(outPath, selfDir);
    lstrcatW(outPath, axName);
    if (GetFileAttributesW(outPath) != INVALID_FILE_ATTRIBUTES) return;

    // 3. Fallback: <selfDir>\tools\lav_x86\<axName>
    lstrcpyW(outPath, selfDir);
    lstrcatW(outPath, L"tools\\lav_x86\\");
    lstrcatW(outPath, axName);
}

// In-process COM instantiation from .ax DLL
static IBaseFilter* LoadFilterFromAx(const wchar_t* axName, const GUID& clsid) {
    wchar_t axPath[MAX_PATH] = {0};
    GetLAVPath(axName, axPath, MAX_PATH);

    char szNarrowPath[MAX_PATH] = {0};
    WideCharToMultiByte(CP_ACP, 0, axPath, -1, szNarrowPath, MAX_PATH, NULL, NULL);

    // Ensure the directory containing the .ax is in the DLL search path
    wchar_t dirPath[MAX_PATH] = {0};
    lstrcpyW(dirPath, axPath);
    wchar_t* pSlash = wcsrchr(dirPath, L'\\');
    if (pSlash) {
        *pSlash = L'\0';
        SetDllDirectoryW(dirPath);
    }

    HMODULE hMod = LoadLibraryExW(axPath, NULL, LOAD_WITH_ALTERED_SEARCH_PATH);
    if (!hMod) {
        LogMsg("[AV1_HOOK] Failed to load %s (Error: %lu)\n", szNarrowPath, GetLastError());
        return NULL;
    }
    PFN_DllGetClassObject fnGetClass = (PFN_DllGetClassObject)GetProcAddress(hMod, "DllGetClassObject");
    if (!fnGetClass) {
        LogMsg("[AV1_HOOK] DllGetClassObject not found in %s\n", szNarrowPath);
        return NULL;
    }
    IClassFactory* pFactory = NULL;
    HRESULT hr = fnGetClass(clsid, IID_IClassFactory, (void**)&pFactory);
    if (FAILED(hr) || !pFactory) {
        LogMsg("[AV1_HOOK] DllGetClassObject failed for %s: 0x%08lX\n", szNarrowPath, hr);
        return NULL;
    }
    IBaseFilter* pFilter = NULL;
    hr = pFactory->CreateInstance(NULL, IID_IBaseFilter, (void**)&pFilter);
    pFactory->Release();
    if (FAILED(hr) || !pFilter) {
        LogMsg("[AV1_HOOK] CreateInstance failed for %s: 0x%08lX\n", szNarrowPath, hr);
        return NULL;
    }
    LogMsg("[AV1_HOOK] Successfully loaded and instantiated %s\n", szNarrowPath);
    return pFilter;
}

// Check magic header to identify if file is original ASF/WMV
static bool IsAsfFile(const wchar_t* path) {
    HANDLE hFile = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
    if (hFile == INVALID_HANDLE_VALUE) return false;
    uint8_t buf[16] = {0};
    DWORD read = 0;
    ReadFile(hFile, buf, 16, &read, NULL);
    CloseHandle(hFile);
    if (read < 16) return false;
    static const uint8_t asfGuid[16] = { 0x30, 0x26, 0xB2, 0x75, 0x8E, 0x66, 0xCF, 0x11, 0xA6, 0xD9, 0x00, 0xAA, 0x00, 0x62, 0xCE, 0x6C };
    return (memcmp(buf, asfGuid, 16) == 0);
}

// Intercept FindPin("Output") on LAV Splitter to route to Video Pin
static HRESULT WINAPI Hook_FindPin(IBaseFilter* pThis, LPCWSTR Id, IPin** ppPin) {
    if (Id && wcscmp(Id, L"Output") == 0) {
        IEnumPins* pEnum = NULL;
        if (SUCCEEDED(pThis->EnumPins(&pEnum))) {
            IPin* pPin = NULL;
            while (pEnum->Next(1, &pPin, NULL) == S_OK) {
                PIN_INFO pi;
                pPin->QueryPinInfo(&pi);
                if (pi.pFilter) pi.pFilter->Release();
                if (wcsstr(pi.achName, L"Video") != NULL) {
                    pEnum->Release();
                    LogMsg("[AV1_HOOK] Intercepted FindPin(\"Output\"), successfully routed to Video Pin %p\n", pPin);
                    *ppPin = pPin;
                    return S_OK;
                }
                pPin->Release();
            }
            pEnum->Release();
        }
    }
    if (g_pfnRealFindPin) return g_pfnRealFindPin(pThis, Id, ppPin);
    return E_FAIL;
}

// Intercept IGraphBuilder::AddSourceFilter
static HRESULT WINAPI Hook_AddSourceFilter(IGraphBuilder* pThis, LPCWSTR lpcwstrFileName, LPCWSTR lpcwstrFilterName, IBaseFilter** ppFilter) {
    char szNarrowFile[MAX_PATH] = {0};
    if (lpcwstrFileName) WideCharToMultiByte(CP_ACP, 0, lpcwstrFileName, -1, szNarrowFile, MAX_PATH, NULL, NULL);
    LogMsg("[AV1_HOOK] IGraphBuilder::AddSourceFilter called for: %s\n", szNarrowFile);

    // If file is original ASF/WMV, pass through to native Windows DirectShow
    if (IsAsfFile(lpcwstrFileName)) {
        LogMsg("[AV1_HOOK] File is original ASF/WMV, passing to native GraphBuilder.\n");
        return g_pfnRealAddSourceFilter(pThis, lpcwstrFileName, lpcwstrFilterName, ppFilter);
    }

    LogMsg("[AV1_HOOK] File is non-ASF (AV1/MP4)! Activating registration-free LAV Filters...\n");
    IBaseFilter* pSplitter = LoadFilterFromAx(L"LAVSplitter.ax", GUID_LAVSplitterSource);
    IBaseFilter* pVideoDec = LoadFilterFromAx(L"LAVVideo.ax", GUID_LAVVideo);
    IBaseFilter* pAudioDec = LoadFilterFromAx(L"LAVAudio.ax", GUID_LAVAudio);

    if (!pSplitter || !pVideoDec || !pAudioDec) {
        LogMsg("[AV1_HOOK] ERROR: Failed to instantiate LAV Filters! Fallback to native.\n");
        return g_pfnRealAddSourceFilter(pThis, lpcwstrFileName, lpcwstrFilterName, ppFilter);
    }

    IFileSourceFilter* pFileSource = NULL;
    HRESULT hr = pSplitter->QueryInterface(IID_IFileSourceFilter, (void**)&pFileSource);
    if (FAILED(hr)) return hr;
    hr = pFileSource->Load(lpcwstrFileName, NULL);
    pFileSource->Release();
    if (FAILED(hr)) {
        LogMsg("[AV1_HOOK] ERROR: LAVSplitter::Load failed (0x%08lX)\n", hr);
        return hr;
    }

    pThis->AddFilter(pSplitter, lpcwstrFilterName ? lpcwstrFilterName : L"Source");
    pThis->AddFilter(pVideoDec, L"LAV Video Decoder");
    pThis->AddFilter(pAudioDec, L"LAV Audio Decoder");

    // Automatically pre-render the Audio Pin so audio pipeline is fully connected
    // (AdvHD only calls Render() on the returned video pin, neglecting audio unless pre-connected)
    IEnumPins* pEnumPins = NULL;
    if (SUCCEEDED(pSplitter->EnumPins(&pEnumPins))) {
        IPin* pPin = NULL;
        while (pEnumPins->Next(1, &pPin, NULL) == S_OK) {
            PIN_INFO pi;
            pPin->QueryPinInfo(&pi);
            if (pi.pFilter) pi.pFilter->Release();
            if (wcsstr(pi.achName, L"Audio") != NULL) {
                HRESULT hrAudio = pThis->Render(pPin);
                LogMsg("[AV1_HOOK] Automatically connected Audio Pin (%ls) -> Result: 0x%08lX\n", pi.achName, hrAudio);
            }
            pPin->Release();
        }
        pEnumPins->Release();
    }

    // Hook FindPin on this splitter instance
    void** pSplitterVTable = *(void***)pSplitter;
    if (pSplitterVTable[11] != (void*)Hook_FindPin) {
        g_pfnRealFindPin = (PFN_FindPin)pSplitterVTable[11];
        DWORD oldProtect;
        VirtualProtect(&pSplitterVTable[11], sizeof(void*), PAGE_EXECUTE_READWRITE, &oldProtect);
        pSplitterVTable[11] = (void*)Hook_FindPin;
        VirtualProtect(&pSplitterVTable[11], sizeof(void*), oldProtect, &oldProtect);
    }

    *ppFilter = pSplitter;
    pSplitter->AddRef();
    LogMsg("[AV1_HOOK] AddSourceFilter SUCCESS! Splitter, Video, and Audio Decoders mounted.\n");
    return S_OK;
}

// Intercept IGraphBuilder::RenderFile
static HRESULT WINAPI Hook_RenderFile(IGraphBuilder* pThis, LPCWSTR lpcwstrFile, LPCWSTR lpcwstrPlayList) {
    LogMsg("[AV1_HOOK] IGraphBuilder::RenderFile called for: %ls\n", lpcwstrFile);
    if (IsAsfFile(lpcwstrFile)) {
        return g_pfnRealRenderFile(pThis, lpcwstrFile, lpcwstrPlayList);
    }

    IBaseFilter* pSource = NULL;
    HRESULT hr = Hook_AddSourceFilter(pThis, lpcwstrFile, L"Source", &pSource);
    if (FAILED(hr) || !pSource) {
        return g_pfnRealRenderFile(pThis, lpcwstrFile, lpcwstrPlayList);
    }

    // Render all output pins
    IEnumPins* pEnum = NULL;
    pSource->EnumPins(&pEnum);
    IPin* pPin = NULL;
    while (pEnum->Next(1, &pPin, NULL) == S_OK) {
        PIN_DIRECTION dir;
        pPin->QueryDirection(&dir);
        if (dir == PINDIR_OUTPUT) {
            pThis->Render(pPin);
        }
        pPin->Release();
    }
    pEnum->Release();
    pSource->Release();
    return S_OK;
}

static void HookGraphBuilder(IGraphBuilder* pGraph) {
    void** pVTable = *(void***)pGraph;
    if (pVTable[14] != (void*)Hook_AddSourceFilter) {
        g_pfnRealAddSourceFilter = (PFN_AddSourceFilter)pVTable[14];
        DWORD oldProtect;
        VirtualProtect(&pVTable[14], sizeof(void*), PAGE_EXECUTE_READWRITE, &oldProtect);
        pVTable[14] = (void*)Hook_AddSourceFilter;
        VirtualProtect(&pVTable[14], sizeof(void*), oldProtect, &oldProtect);
        LogMsg("[AV1_HOOK] IGraphBuilder::AddSourceFilter hooked successfully!\n");
    }
    if (pVTable[13] != (void*)Hook_RenderFile) {
        g_pfnRealRenderFile = (PFN_RenderFile)pVTable[13];
        DWORD oldProtect;
        VirtualProtect(&pVTable[13], sizeof(void*), PAGE_EXECUTE_READWRITE, &oldProtect);
        pVTable[13] = (void*)Hook_RenderFile;
        VirtualProtect(&pVTable[13], sizeof(void*), oldProtect, &oldProtect);
        LogMsg("[AV1_HOOK] IGraphBuilder::RenderFile hooked successfully!\n");
    }
}

static HRESULT WINAPI Hook_CoCreateInstance(REFCLSID rclsid, LPUNKNOWN pUnkOuter, DWORD dwClsContext, REFIID riid, LPVOID *ppv) {
    HRESULT hr = g_pfnRealCoCreateInstance(rclsid, pUnkOuter, dwClsContext, riid, ppv);
    if (SUCCEEDED(hr) && ppv && *ppv) {
        if (IsEqualGUID(rclsid, CLSID_FilterGraph)) {
            LogMsg("[AV1_HOOK] CoCreateInstance(CLSID_FilterGraph) -> Intercepted Graph %p\n", *ppv);
            HookGraphBuilder((IGraphBuilder*)*ppv);
        }
    }
    return hr;
}

static void InstallDShowAV1Hook() {
    HMODULE hOle32 = GetModuleHandleA("ole32.dll");
    if (!hOle32) hOle32 = LoadLibraryA("ole32.dll");
    if (!hOle32) {
        LogMsg("[AV1_HOOK] Failed to locate ole32.dll\n");
        return;
    }
    uint8_t* pTarget = (uint8_t*)GetProcAddress(hOle32, "CoCreateInstance");
    if (!pTarget) {
        LogMsg("[AV1_HOOK] Failed to locate ole32.dll!CoCreateInstance\n");
        return;
    }

    uint8_t* tramp = (uint8_t*)VirtualAlloc(NULL, 64, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
    if (!tramp) return;
    memcpy(tramp, pTarget, 5);
    tramp[5] = 0xE9;
    *(int32_t*)(tramp + 6) = (int32_t)(pTarget + 5) - (int32_t)(tramp + 10);
    g_pfnRealCoCreateInstance = (PFN_CoCreateInstance)tramp;

    DWORD oldProtect;
    VirtualProtect(pTarget, 5, PAGE_EXECUTE_READWRITE, &oldProtect);
    pTarget[0] = 0xE9;
    *(int32_t*)(pTarget + 1) = (int32_t)((uint8_t*)Hook_CoCreateInstance) - (int32_t)(pTarget + 5);
    VirtualProtect(pTarget, 5, oldProtect, &oldProtect);
    FlushInstructionCache(GetCurrentProcess(), pTarget, 5);
    LogMsg("[AV1_HOOK] Hook installed on ole32.dll!CoCreateInstance (%p -> %p)!\n", pTarget, Hook_CoCreateInstance);
}

// ============================================================================
// DllMain Entry
// ============================================================================

extern "C" {

BOOL WINAPI DllMain(HINSTANCE hinstDLL, DWORD fdwReason, LPVOID lpvReserved) {
    if (fdwReason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(hinstDLL);
        g_hSelfModule = hinstDLL;
        LogMsg("[AV1_HOOK] Standalone av1_hook.dll attached to PID %lu\n", GetCurrentProcessId());
        InstallDShowAV1Hook();
    }
    return TRUE;
}

}
