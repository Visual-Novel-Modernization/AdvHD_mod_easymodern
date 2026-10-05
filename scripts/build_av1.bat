@echo off
cd /d "%~dp0.."
where g++ >nul 2>nul
if %ERRORLEVEL% NEQ 0 (
    echo [ERROR] 32-bit MinGW g++ not found in PATH.
    exit /b 1
)
g++ -m32 -O2 -shared -static -static-libgcc -static-libstdc++ av1_hook\av1_hook.cpp -o av1_hook\av1_hook.dll -lole32 -loleaut32 -lstrmiids
if %ERRORLEVEL% EQU 0 (
    echo [OK] av1_hook.dll build succeeded.
) else (
    echo [ERROR] av1_hook.dll build failed.
)
