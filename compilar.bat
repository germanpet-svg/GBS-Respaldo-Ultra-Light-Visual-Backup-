@echo off
title Compilando GBS Respaldo Final
echo ========================================
echo   GBS Respaldo Final - Compilacion
echo ========================================
echo.

REM Buscar GCC en PATH
where gcc >nul 2>nul
if %errorlevel% neq 0 (
    echo ERROR: No se encuentra GCC en el PATH.
    echo Instala MinGW o añade GCC al PATH.
    pause
    exit /b 1
)

echo Compilando gbsbio_final.c ...
gcc -o gbsbio_final.exe gbsbio_final.c -lwinmm -luser32 -lshell32 -lgdi32 -ladvapi32 -O2 -mwindows

if %errorlevel% == 0 (
    echo.
    echo ========================================
    echo   ¡Compilacion exitosa!
    echo ========================================
    echo.
    echo USO NORMAL:
    echo   Arrastra archivos o carpetas sobre gbsbio_final.exe
    echo.
    echo COMANDOS ESPECIALES:
    echo   gbsbio_final.exe --restaurar "archivo.txt" "2026-05-28_14-30-00"
    echo   gbsbio_final.exe --diff "archivo.txt" "fecha1" "fecha2"
    echo   gbsbio_final.exe --exportar-git "nombre_proyecto"
    echo.
    echo CARACTERISTICAS:
    echo   - Click izquierdo: Ver estadisticas
    echo   - Doble click: Abrir carpeta de respaldos
    echo   - Click derecho: Menu contextual (Pausar/Reanudar/Salir)
    echo   - Modo nocturno configurable
    echo   - Exclusion automatica de archivos temporales
    echo.
) else (
    echo.
    echo ERROR: Fallo en la compilacion.
    echo Revisa que el archivo gbsbio_final.c exista.
)

pause