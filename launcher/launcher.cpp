// AdvHD EasyModern launcher
//
// Hello, and welcome to the Aperture Science computer-aided enrichment launcher.
//
// Starts the game, then quietly slips jxl_hook.dll and av1_hook.dll into it. If either one isn't
// sitting next to this exe, no drama, we skip it and run the game stock.
//
// It has to know which exe to start, and it'll take the answer three ways, in order:
//   1. the command line - AdvHD_EasyModern.exe MyGame.exe | -t MyGame.exe | --target MyGame.exe
//   2. launcher.ini     - [Launcher] Target=MyGame.exe, or a bare filename on line one
//   3. guesswork        - AdvHD_CN.exe, then AdvHD_CHS.exe, then AdvHD_crack.exe, then AdvHD.exe

#include <windows.h>
#include <stdio.h>
#include <stdbool.h>
#include <string.h>

static void FullPathNextToSelf(const char* leaf, char* out, DWORD cch) {
    char self[MAX_PATH];
    DWORD n = GetModuleFileNameA(NULL, self, MAX_PATH);
    if (n == 0 || n >= MAX_PATH) { lstrcpynA(out, leaf, cch); return; }
    char* slash = self;
    for (char* p = self; *p; ++p) { if (*p == '\\' || *p == '/') slash = p; }
    *(slash + 1) = 0;
    lstrcpynA(out, self, cch);
    int used = lstrlenA(out);
    if (used < (int)cch - 1) {
        lstrcpynA(out + used, leaf, cch - used);
    }
}

static bool CheckFileExists(const char* path) {
    DWORD dwAttrib = GetFileAttributesA(path);
    return (dwAttrib != INVALID_FILE_ATTRIBUTES && !(dwAttrib & FILE_ATTRIBUTE_DIRECTORY));
}

static bool ReadConfigTarget(const char* iniPath, char* outTarget, DWORD cch) {
    if (!CheckFileExists(iniPath)) return false;

    // Proper INI style: [Launcher] Target=Game.exe
    char buf[MAX_PATH] = {0};
    GetPrivateProfileStringA("Launcher", "Target", "", buf, MAX_PATH, iniPath);
    if (buf[0] != 0) {
        lstrcpynA(outTarget, buf, cch);
        return true;
    }

    // Or somebody just pasted a filename on its own line. We'll take that too.
    FILE* fp = fopen(iniPath, "r");
    if (fp) {
        if (fgets(buf, sizeof(buf), fp)) {
            char* p = buf;
            while (*p && (*p == ' ' || *p == '\t')) p++;
            char* end = p + strlen(p) - 1;
            while (end >= p && (*end == '\r' || *end == '\n' || *end == ' ' || *end == '\t')) *end-- = 0;
            if (strlen(p) > 0 && strstr(p, ".exe") != NULL) {
                lstrcpynA(outTarget, p, cch);
                fclose(fp);
                return true;
            }
        }
        fclose(fp);
    }
    return false;
}

static bool InjectDll(HANDLE hProcess, const char* dllPath, const char* hookName) {
    printf("[launcher] Injecting %s from: %s\n", hookName, dllPath);

    size_t len = lstrlenA(dllPath) + 1;
    void* pBuf = VirtualAllocEx(hProcess, NULL, len, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (!pBuf) {
        printf("[launcher] VirtualAllocEx failed for %s (Error: %lu)\n", hookName, GetLastError());
        return false;
    }
    if (!WriteProcessMemory(hProcess, pBuf, dllPath, len, NULL)) {
        printf("[launcher] WriteProcessMemory failed for %s (Error: %lu)\n", hookName, GetLastError());
        VirtualFreeEx(hProcess, pBuf, 0, MEM_RELEASE);
        return false;
    }

    HMODULE hK32 = GetModuleHandleA("kernel32.dll");
    void* pLoadLib = (void*)GetProcAddress(hK32, "LoadLibraryA");
    HANDLE hThread = CreateRemoteThread(hProcess, NULL, 0, (LPTHREAD_START_ROUTINE)pLoadLib, pBuf, 0, NULL);
    if (!hThread) {
        printf("[launcher] CreateRemoteThread failed for %s (Error: %lu)\n", hookName, GetLastError());
        VirtualFreeEx(hProcess, pBuf, 0, MEM_RELEASE);
        return false;
    }

    WaitForSingleObject(hThread, 5000);
    DWORD hRemoteMod = 0;
    GetExitCodeThread(hThread, &hRemoteMod);
    CloseHandle(hThread);
    VirtualFreeEx(hProcess, pBuf, 0, MEM_RELEASE);

    if (hRemoteMod == 0) {
        printf("[launcher] Remote LoadLibraryA returned NULL for %s\n", hookName);
        return false;
    }

    printf("[launcher] %s successfully initialized in remote process (HMODULE: 0x%08lX)\n", hookName, hRemoteMod);
    return true;
}

int main(int argc, char** argv) {
    const char* defaultTargets[] = {
        "AdvHD_CN.exe",
        "AdvHD_CHS.exe",
        "AdvHD_crack.exe",
        "AdvHD.exe"
    };

    const char* cliTarget = NULL;

    // Read the command line
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            printf("AdvHD Modular Mod Launcher\n\n");
            printf("Usage:\n");
            printf("  advhd_mod_launcher.exe [target.exe]\n");
            printf("  advhd_mod_launcher.exe -t <target.exe>\n");
            printf("  advhd_mod_launcher.exe --target <target.exe>\n\n");
            printf("Configuration File Override:\n");
            printf("  Place a 'launcher.ini' in the launcher directory:\n");
            printf("    [Launcher]\n");
            printf("    Target=YourCustomGame.exe\n\n");
            printf("Default auto-detected executables (in order):\n");
            printf("  AdvHD_CN.exe -> AdvHD_CHS.exe -> AdvHD_crack.exe -> AdvHD.exe\n");
            return 0;
        } else if ((strcmp(argv[i], "-t") == 0 || strcmp(argv[i], "--target") == 0) && i + 1 < argc) {
            cliTarget = argv[++i];
        } else if (argv[i][0] != '-') {
            cliTarget = argv[i];
        }
    }

    char exePath[MAX_PATH] = {0};
    char workDir[MAX_PATH] = {0};

    // First choice: whatever they typed
    if (cliTarget != NULL) {
        FullPathNextToSelf(cliTarget, exePath, MAX_PATH);
        if (!CheckFileExists(exePath)) {
            printf("[launcher] ERROR: Specified target executable not found: %s\n", exePath);
            return 2;
        }
        printf("[launcher] Target executable selected via CLI argument: %s\n", cliTarget);
    }

    // Second choice: launcher.ini, for the double-click crowd
    if (exePath[0] == 0) {
        char iniPath[MAX_PATH];
        char configTarget[MAX_PATH] = {0};
        FullPathNextToSelf("launcher.ini", iniPath, MAX_PATH);
        if (ReadConfigTarget(iniPath, configTarget, MAX_PATH)) {
            FullPathNextToSelf(configTarget, exePath, MAX_PATH);
            if (!CheckFileExists(exePath)) {
                printf("[launcher] ERROR: Executable specified in launcher.ini not found: %s\n", exePath);
                return 2;
            }
            printf("[launcher] Target executable selected via launcher.ini: %s\n", configTarget);
        }
    }

    // Last resort: sniff around for a standard AdvHD exe
    if (exePath[0] == 0) {
        for (size_t i = 0; i < sizeof(defaultTargets) / sizeof(defaultTargets[0]); i++) {
            char candidate[MAX_PATH];
            FullPathNextToSelf(defaultTargets[i], candidate, MAX_PATH);
            if (CheckFileExists(candidate)) {
                lstrcpyA(exePath, candidate);
                printf("[launcher] Auto-detected target executable: %s\n", defaultTargets[i]);
                break;
            }
        }
    }

    if (exePath[0] == 0 || !CheckFileExists(exePath)) {
        printf("[launcher] ERROR: No valid AdvHD executable found!\n");
        printf("Please specify a target via CLI argument, launcher.ini, or place a standard AdvHD exe next to the launcher.\n");
        return 2;
    }

    lstrcpynA(workDir, exePath, MAX_PATH);
    char* slash = workDir;
    for (char* p = workDir; *p; ++p) { if (*p == '\\' || *p == '/') slash = p; }
    *slash = 0;

    printf("====================================================\n");
    printf("   AdvHD Modular Mod Launcher (JXL / AV1 Engine)\n");
    printf("====================================================\n");
    printf("[launcher] Target Path : %s\n", exePath);
    printf("[launcher] Work Dir    : %s\n", workDir);

    // See which hooks we actually have before starting anything
    char jxlDllPath[MAX_PATH];
    char av1DllPath[MAX_PATH];
    FullPathNextToSelf("jxl_hook.dll", jxlDllPath, MAX_PATH);
    FullPathNextToSelf("av1_hook.dll", av1DllPath, MAX_PATH);

    bool hasJxlHook = CheckFileExists(jxlDllPath);
    bool hasAv1Hook = CheckFileExists(av1DllPath);

    if (hasJxlHook) {
        printf("[launcher] Detected optional plugin: jxl_hook.dll\n");
    } else {
        printf("[launcher] jxl_hook.dll not present; gracefully bypassing JXL texture hook.\n");
    }

    if (hasAv1Hook) {
        printf("[launcher] Detected optional plugin: av1_hook.dll\n");
    } else {
        printf("[launcher] av1_hook.dll not present; gracefully bypassing AV1 video hook.\n");
    }

    STARTUPINFOA si = { sizeof(si) };
    PROCESS_INFORMATION pi = { 0 };

    if (!CreateProcessA(exePath, NULL, NULL, NULL, FALSE, 0, NULL, workDir, &si, &pi)) {
        printf("[launcher] CreateProcess failed (Error: %lu)\n", GetLastError());
        return 1;
    }
    printf("[launcher] Engine process spawned (PID: %lu)\n", pi.dwProcessId);

    // Give the engine a couple of seconds to unpack itself before we go poking at it
    if (hasJxlHook || hasAv1Hook) {
        Sleep(2000);
    }

    int injectedCount = 0;
    if (hasJxlHook) {
        if (InjectDll(pi.hProcess, jxlDllPath, "JXL Hook")) injectedCount++;
    }
    if (hasAv1Hook) {
        if (InjectDll(pi.hProcess, av1DllPath, "AV1 Hook")) injectedCount++;
    }

    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);

    printf("[launcher] Startup completed (%d active hook plugin(s)). Engine running.\n", injectedCount);
    return 0;
}
