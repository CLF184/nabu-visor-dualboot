@echo off
setlocal EnableExtensions
chcp 65001 >nul
title nabu_visor_dualboot - install ESP config

rem 路线 2：只把配置与主题资源复制到 ESP 的 \EFI\visor\。
rem 不改动 Windows 启动器，不替换任何引导文件；系统选择由 boot.img 内的驱动完成。

net session >nul 2>&1
if errorlevel 1 (
  echo [!] Please run this script as Administrator.
  pause & exit /b 1
)

set "PKG=%~dp0.."
if not exist "%PKG%\esp\EFI\visor\boot.conf" (
  echo [!] esp\EFI\visor\boot.conf not found. Run "python build.py" first.
  pause & exit /b 1
)

echo [*] Mounting EFI System Partition...
set "ESPL="
for %%L in (S T U V W) do (
  if not defined ESPL (
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

echo [*] Copying config and assets to %ESPL%\EFI\visor ...
robocopy "%PKG%\esp\EFI\visor" "%ESPL%\EFI\visor" /e /nfl /ndl /njh /njs >nul

mountvol %ESPL% /d >nul 2>&1
echo.
echo [OK] Config installed. Next cold boot shows the Visor menu (from boot.img).
echo      Edit %ESPL%\EFI\visor\boot.conf any time - Visor reads it every boot.
echo      This script never touches Windows boot files.
pause
