// AdvHD EasyModern launcher
//
// Hello, and welcome to the Aperture Science computer-aided enrichment launcher.
//
// Starts the game, then quietly slips jxl_hook.dll and av1_hook.dll into it. If either one isn't
// sitting next to this exe, no drama, we skip it and run the game stock.
//
// It has to know which exe to start, and it'll take the answer three ways, in order:
//   1. the command line - AdvHD_EasyModern.exe MyGame.exe | -t MyGame.exe | --target MyGame.exe
//   2. AdvHD_EasyModern.ini - [Launcher] Target=MyGame.exe, or a bare filename on line one
//   3. wildcard        - the first "AdvHD*.exe" next to this launcher
//
// NOTE on the config file name: it used to be launcher.ini. That name is a landmine on
// WillPlus titles, which ship their own UTF-16LE launcher.INI (read by the game's own
// launcher.exe to find GAMEEXE, MAINIMAGE, the manual, and so on). Windows is
// case-insensitive, so a mod config called launcher.ini does not sit next to the game's
// file - it IS the game's file, and a deployment that writes one silently destroys the
// game's launcher configuration. CONFIG_NAME therefore avoids that name entirely.
// LEGACY_CONFIG_NAME is still read for compatibility; the reader rejects the game's own
// launcher.INI on its own, since its first line is "[LAUNCHER]" and not a *.exe.

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

// ============================================================================
// Which executable to launch
//
// We glob TARGET_PATTERN next to this launcher and take the first hit,
// skipping this launcher itself. Differently named exe? Edit
// TARGET_PATTERN and rebuild.
// ============================================================================
#define TARGET_PATTERN "AdvHD*.exe"

// Optional config file, parsed by ReadConfigTarget().
// CONFIG_NAME must NOT be "launcher.ini" - see the note at the top of this file.
#define CONFIG_NAME        "AdvHD_EasyModern.ini"
#define LEGACY_CONFIG_NAME "launcher.ini"

// Base name of this launcher, for the usage text.
static const char* SelfName() {
    static char name[MAX_PATH] = {0};
    if (name[0] == 0) {
        char full[MAX_PATH] = {0};
        GetModuleFileNameA(NULL, full, MAX_PATH);
        char* base = full;
        for (char* p = full; *p; ++p) { if (*p == '\\' || *p == '/') base = p + 1; }
        lstrcpynA(name, base, MAX_PATH);
    }
    return name;
}

// First TARGET_PATTERN match beside us. Never returns ourselves.
static bool FindWildcardTarget(char* outPath, DWORD cch) {
    char pattern[MAX_PATH];
    FullPathNextToSelf(TARGET_PATTERN, pattern, MAX_PATH);

    char selfPath[MAX_PATH] = {0};
    if (GetModuleFileNameA(NULL, selfPath, MAX_PATH) == 0) selfPath[0] = 0;

    char best[MAX_PATH] = {0};
    WIN32_FIND_DATAA fd;
    HANDLE hFind = FindFirstFileA(pattern, &fd);
    if (hFind != INVALID_HANDLE_VALUE) {
        do {
            if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
            char candidate[MAX_PATH];
            FullPathNextToSelf(fd.cFileName, candidate, MAX_PATH);
            if (selfPath[0] && lstrcmpiA(candidate, selfPath) == 0) continue;
            if (best[0] == 0 || lstrcmpiA(candidate, best) < 0) lstrcpynA(best, candidate, MAX_PATH);
        } while (FindNextFileA(hFind, &fd));
        FindClose(hFind);
    }
    if (best[0] == 0) return false;
    lstrcpynA(outPath, best, cch);
    return true;
}

int main(int argc, char** argv) {
    const char* cliTarget = NULL;

    // Read the command line
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            printf("AdvHD EasyModern launcher\n\n");
            printf("Usage:\n");
            printf("  %s [target.exe]\n", SelfName());
            printf("  %s -t <target.exe>\n", SelfName());
            printf("  %s --target <target.exe>\n\n", SelfName());
            printf("Target selection, in order:\n");
            printf("  1. the command line, above\n");
            printf("  2. %s beside this exe:\n", CONFIG_NAME);
            printf("       [Launcher]\n");
            printf("       Target=YourGame.exe\n");
            printf("  3. the first '%s' sitting beside this exe\n\n", TARGET_PATTERN);
            printf("Your game exe is named something else? Either write a %s,\n", CONFIG_NAME);
            printf("or edit TARGET_PATTERN in launcher.cpp and rebuild. That is the\n");
            printf("supported way to add one.\n");
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

    // Second choice: the config file, for the double-click crowd.
    // Checked in order: CONFIG_NAME, then the legacy launcher.ini.
    if (exePath[0] == 0) {
        char iniPath[MAX_PATH];
        char configTarget[MAX_PATH] = {0};
        const char* usedConfig = NULL;

        FullPathNextToSelf(CONFIG_NAME, iniPath, MAX_PATH);
        if (ReadConfigTarget(iniPath, configTarget, MAX_PATH)) {
            usedConfig = CONFIG_NAME;
        } else {
            // Legacy name. ReadConfigTarget only accepts this when it actually looks like a
            // mod config, so the game's own launcher.INI (first line "[LAUNCHER]") is ignored.
            FullPathNextToSelf(LEGACY_CONFIG_NAME, iniPath, MAX_PATH);
            if (ReadConfigTarget(iniPath, configTarget, MAX_PATH)) {
                usedConfig = LEGACY_CONFIG_NAME;
            }
        }

        if (usedConfig != NULL) {
            FullPathNextToSelf(configTarget, exePath, MAX_PATH);
            if (!CheckFileExists(exePath)) {
                printf("[launcher] ERROR: Executable specified in %s not found: %s\n", usedConfig, exePath);
                return 2;
            }
            printf("[launcher] Target executable selected via %s: %s\n", usedConfig, configTarget);
        }
    }

    // Last resort: glob TARGET_PATTERN next to us
    if (exePath[0] == 0) {
        if (FindWildcardTarget(exePath, MAX_PATH)) {
            printf("[launcher] Auto-detected target: %s\n", exePath);
        }
    }

    if (exePath[0] == 0 || !CheckFileExists(exePath)) {
        printf("[launcher] ERROR: no '%s' found next to this launcher.\n", TARGET_PATTERN);
        printf("  Point it at your game:  %s <yourgame.exe>\n", SelfName());
        printf("  Or drop a %s beside it with:\n", CONFIG_NAME);
        printf("      [Launcher]\n");
        printf("      Target=yourgame.exe\n");
        printf("  To change the search pattern itself, edit TARGET_PATTERN in\n");
        printf("  launcher.cpp and rebuild.\n");
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
