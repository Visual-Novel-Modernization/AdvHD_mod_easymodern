@echo off
where g++ >nul 2>nul
if %ERRORLEVEL% NEQ 0 (
    echo [ERROR] 32-bit MinGW g++ not found in PATH.
    exit /b 1
)
echo Building JXL Hook...
cd "%~dp0..\jxl_hook"
call "%~dp0build_jxl.bat"
cd "%~dp0"

echo Building AV1 Hook...
cd "%~dp0..\av1_hook"
call "%~dp0build_av1.bat"
cd "%~dp0"

echo Building Universal Launcher...
cd "%~dp0..\launcher"
call "%~dp0build_launcher.bat"
cd "%~dp0"

echo All builds complete!
