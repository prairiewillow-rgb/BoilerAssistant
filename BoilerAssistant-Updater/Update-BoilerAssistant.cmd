@echo off
setlocal
set "ROOT=%~dp0"
set "CLI=%ROOT%tools\arduino-cli.exe"

if not exist "%CLI%" (
  echo Arduino CLI is missing from tools.
  echo This updater package is incomplete.
  pause
  exit /b 1
)

powershell -NoProfile -ExecutionPolicy Bypass -File "%ROOT%Update-BoilerAssistant.ps1"
set "RESULT=%ERRORLEVEL%"
if not "%RESULT%"=="0" pause
exit /b %RESULT%
