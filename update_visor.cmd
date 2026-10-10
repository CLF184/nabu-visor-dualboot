@echo off
setlocal
chcp 65001 >nul
cd /d "%~dp0"
py -3 -X utf8 "%~dp0update_visor.py" %*
set "UPDATE_RESULT=%ERRORLEVEL%"
echo.
if not "%UPDATE_RESULT%"=="0" echo Update failed. See .visor-update\last-error.log
pause
exit /b %UPDATE_RESULT%
