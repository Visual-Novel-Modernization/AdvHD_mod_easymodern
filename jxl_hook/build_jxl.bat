@echo off
cd /d "%~dp0"
where g++ >nul 2>nul
if %ERRORLEVEL% NEQ 0 (
    echo [ERROR] 32-bit MinGW g++ not found in PATH.
    exit /b 1
)
g++ -m32 -O2 -shared -static -static-libgcc -static-libstdc++ jxl_hook.cpp -o jxl_hook.dll
if %ERRORLEVEL% EQU 0 (
    echo [OK] jxl_hook.dll build succeeded.
) else (
    echo [ERROR] jxl_hook.dll build failed.
)
