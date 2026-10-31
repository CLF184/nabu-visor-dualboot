@echo off
setlocal EnableExtensions
chcp 65001 >nul
title nabu_visor_dualboot - install ESP config

rem ESP 应用方案：先安装配套资源，再用菜单替换 EFI/BOOT 回退入口。
rem 仅在平板 Windows 上运行，保留微软引导文件与原回退入口备份。

net session >nul 2>&1
if errorlevel 1 (
  echo [!] Please run this script as Administrator.
  pause & exit /b 1
)

set "PKG=%~dp0..\dist"
if not exist "%PKG%\esp\EFI\visor\boot.conf" (
  echo [!] dist\esp\EFI\visor\boot.conf not found. Build with --fd first.
  pause & exit /b 1
)
if not exist "%PKG%\esp\EFI\BOOT\bootaa64.efi" (
  echo [!] Standalone Visor application missing. Build this branch first.
  pause & exit /b 1
)

echo [*] Mounting EFI System Partition...
set "ESPL="
for %%L in (S T U V W) do (
  if not defined ESPL if not exist "%%L:\" (
    mountvol %%L: /s >nul 2>&1 && (
      if exist "%%L:\EFI" set "ESPL=%%L:"
    )
  )
)
if not defined ESPL (
  echo [!] Could not mount the ESP automatically. Mount it manually and copy files.
  pause & exit /b 1
)
echo [*] ESP at %ESPL%
if not exist "%ESPL%\EFI\Microsoft\Boot\bootmgfw.efi" (
  echo [!] Microsoft bootmgfw.efi missing. Restore it before installing Visor.
  mountvol %ESPL% /d >nul 2>&1
  pause & exit /b 1
)
if not exist "%ESPL%\EFI\BOOT" mkdir "%ESPL%\EFI\BOOT"
if exist "%ESPL%\EFI\BOOT\bootaa64.efi" if not exist "%ESPL%\EFI\BOOT\bootaa64.efi.before-visor" (
  copy /b /y "%ESPL%\EFI\BOOT\bootaa64.efi" "%ESPL%\EFI\BOOT\bootaa64.efi.before-visor" >nul
  if errorlevel 1 (
    echo [!] Failed to back up the previous fallback EFI. Stopping.
    mountvol %ESPL% /d >nul 2>&1
    pause & exit /b 1
  )
)

echo [*] Copying config and assets to %ESPL%\EFI\visor ...
robocopy "%PKG%\esp\EFI\visor" "%ESPL%\EFI\visor" /e /nfl /ndl /njh /njs >nul
if errorlevel 8 (
  echo [!] Resource copy failed. Fallback EFI was not replaced.
  mountvol %ESPL% /d >nul 2>&1
  pause & exit /b 1
)
copy /b /y "%PKG%\esp\EFI\BOOT\bootaa64.efi" "%ESPL%\EFI\BOOT\bootaa64.efi" >nul
if errorlevel 1 (
  if exist "%ESPL%\EFI\BOOT\bootaa64.efi.before-visor" copy /b /y "%ESPL%\EFI\BOOT\bootaa64.efi.before-visor" "%ESPL%\EFI\BOOT\bootaa64.efi" >nul
  echo [!] Menu copy failed. Check the fallback EFI before rebooting.
  mountvol %ESPL% /d >nul 2>&1
  pause & exit /b 1
)

mountvol %ESPL% /d >nul 2>&1
echo.
echo [OK] ESP menu and resources installed. Use the matching NoSB boot image.
echo      Edit EFI\visor\boot.conf any time. Microsoft boot files are preserved.
echo      Firmware must boot EFI\BOOT\bootaa64.efi.
pause
