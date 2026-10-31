@echo off
setlocal EnableExtensions
chcp 65001 >nul
title nabu_visor_dualboot - remove ESP config

rem 先恢复原回退 EFI，再删除 Visor 资源；另需恢复原 boot 镜像。

net session >nul 2>&1
if errorlevel 1 (
  echo [!] Please run this script as Administrator.
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
  echo [!] Could not mount the ESP automatically.
  pause & exit /b 1
)
echo [*] ESP at %ESPL%
if not exist "%ESPL%\EFI\BOOT\bootaa64.efi.before-visor" (
  echo [!] Original fallback EFI backup missing. Restore it manually first.
  mountvol %ESPL% /d >nul 2>&1
  pause & exit /b 1
)
copy /b /y "%ESPL%\EFI\BOOT\bootaa64.efi.before-visor" "%ESPL%\EFI\BOOT\bootaa64.efi" >nul
if errorlevel 1 (
  echo [!] Failed to restore the fallback EFI. Keeping Visor resources.
  mountvol %ESPL% /d >nul 2>&1
  pause & exit /b 1
)

if exist "%ESPL%\EFI\visor" (
  echo [*] Removing %ESPL%\EFI\visor ...
  rmdir /s /q "%ESPL%\EFI\visor"
) else (
  echo [*] %ESPL%\EFI\visor does not exist, nothing to remove.
)

mountvol %ESPL% /d >nul 2>&1
echo.
echo [OK] Original fallback EFI restored; Visor resources removed.
echo      To go back to the original single-boot behaviour, also flash your
echo      previous boot.img:  fastboot flash boot ^<old image^>
pause
