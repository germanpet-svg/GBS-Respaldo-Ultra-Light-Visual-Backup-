@echo off
title Instalador GBS Respaldo - Menu Contextual

echo ========================================
echo   GBS Respaldo - Instalador
echo ========================================
echo.

:: Verificar si se ejecuta como administrador; si no, relanzar elevando permisos
net session >nul 2>&1
if %errorlevel% neq 0 (
    echo Solicitando permisos de administrador...
    powershell -NoProfile -ExecutionPolicy Bypass -Command "Start-Process -FilePath '%~f0' -Verb RunAs"
    exit /b 0
)

set "SCRIPT_MENU=%~dp0instalar_menu_contextual.bat"

if not exist "%SCRIPT_MENU%" (
    echo ERROR: No se encuentra instalar_menu_contextual.bat en la carpeta actual.
    pause
    exit /b 1
)

call "%SCRIPT_MENU%"
if %errorlevel% neq 0 (
    echo.
    echo ERROR: No se pudo completar la instalacion del menu contextual.
    pause
    exit /b 1
)

echo.
echo ========================================
echo   Instalacion completada!
echo ========================================
echo.
echo Ahora puedes hacer click derecho sobre
echo cualquier archivo o carpeta y seleccionar
echo "Enviar a GBS Respaldo"
echo.
pause