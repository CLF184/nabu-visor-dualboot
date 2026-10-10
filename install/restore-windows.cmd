@echo off
setlocal EnableExtensions
chcp 65001 >nul
title nabu_visor_dualboot - remove ESP config

rem 路线 2：只删除 ESP 上的 \EFI\visor\ 配置与素材目录。
rem 注意：要彻底回滚还需要用 fastboot 刷回原来的 boot.img（见 README）。

net session >nul 2>&1
if errorlevel 1 (
  echo [!] Please run this script as Administrator.
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
  echo [!] Could not mount the ESP automatically.
  pause & exit /b 1
)
echo [*] ESP at %ESPL%

if exist "%ESPL%\EFI\visor" (
  echo [*] Removing %ESPL%\EFI\visor ...
  rmdir /s /q "%ESPL%\EFI\visor"
) else (
  echo [*] %ESPL%\EFI\visor does not exist, nothing to remove.
)

mountvol %ESPL% /d >nul 2>&1
echo.
echo [OK] ESP config removed.
echo      To go back to the original single-boot behaviour, also flash your
echo      previous boot.img:  fastboot flash boot ^<old image^>
pause
