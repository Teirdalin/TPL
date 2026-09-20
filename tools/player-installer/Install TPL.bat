@echo off
setlocal
set "GAME_DIR=%~dp0"
if "%GAME_DIR:~-1%"=="\" set "GAME_DIR=%GAME_DIR:~0,-1%"
set "INSTALLER_DIR=%~dp0TPL Installer Files"

if /I "%~1"=="/S" goto reinstall
if /I "%~1"=="/UNINSTALL" goto uninstall

findstr /R /I /C:"^[ ]*Plugin[ ]*=[ ]*TPL[ ]*$" "%GAME_DIR%\Plugins_x64.cfg" >nul 2>nul
if errorlevel 1 goto reinstall

echo Teirdalin's Plugin Loader is already installed.
echo.
echo [R] Reinstall TPL
echo [U] Uninstall TPL
echo [C] Cancel
choice /C RUC /N /M "Choose an option: "
if errorlevel 3 exit /b 0
if errorlevel 2 goto uninstall

:reinstall
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%INSTALLER_DIR%\install.ps1" -GameDir "%GAME_DIR%" -PayloadDir "%INSTALLER_DIR%\payload"
if errorlevel 1 goto failed
echo.
echo TPL installation complete. You can now start Kenshi.
if /I not "%~1"=="/S" pause
exit /b 0

:uninstall
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%INSTALLER_DIR%\uninstall.ps1" -GameDir "%GAME_DIR%"
if errorlevel 1 goto failed
echo.
echo TPL uninstalled. Plugins, settings, saves, and recovery backups were retained.
if /I not "%~1"=="/UNINSTALL" pause
exit /b 0

:failed
echo.
echo TPL setup failed. Close Kenshi and confirm this ZIP was extracted directly into the Kenshi folder.
if /I not "%~1"=="/S" if /I not "%~1"=="/UNINSTALL" pause
exit /b 1
