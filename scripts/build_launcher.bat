@echo off
cd /d "%~dp0.."
where g++ >nul 2>nul
if %ERRORLEVEL% NEQ 0 (
    echo [ERROR] 32-bit MinGW g++ not found in PATH.
    exit /b 1
)
g++ -m32 -O2 -static -static-libgcc -static-libstdc++ launcher\launcher.cpp -o launcher\advhd_mod_launcher.exe
if %ERRORLEVEL% EQU 0 (
    echo [OK] advhd_mod_launcher.exe build succeeded.
) else (
    echo [ERROR] advhd_mod_launcher.exe build failed.
)
