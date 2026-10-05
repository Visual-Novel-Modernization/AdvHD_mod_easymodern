@echo off
cd /d "%~dp0.."
where g++ >nul 2>nul
if %ERRORLEVEL% NEQ 0 (
    echo [ERROR] 32-bit MinGW g++ not found in PATH.
    exit /b 1
)
g++ -m32 -O2 -static -static-libgcc -static-libstdc++ launcher\launcher.cpp -o launcher\game_launcher.exe
if %ERRORLEVEL% EQU 0 (
    echo [OK] game_launcher.exe build succeeded.
) else (
    echo [ERROR] game_launcher.exe build failed.
)
