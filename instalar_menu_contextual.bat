@echo off
REM instalar_menu_contextual.bat
REM Ejecutar como administrador UNA VEZ

set "EXE_PATH=%~dp0respaldador.exe"
if not exist "%EXE_PATH%" (
    set "EXE_PATH=%~dp0gbsbio.exe"
)

REG ADD "HKCR\*\shell\EnviarAGBS" /ve /t REG_SZ /d "Enviar a GBS Respaldo" /f
REG ADD "HKCR\*\shell\EnviarAGBS\command" /ve /t REG_SZ /d "\"%EXE_PATH%\" \"%%1\"" /f

REG ADD "HKCR\Folder\shell\EnviarAGBS" /ve /t REG_SZ /d "Enviar a GBS Respaldo" /f
REG ADD "HKCR\Folder\shell\EnviarAGBS\command" /ve /t REG_SZ /d "\"%EXE_PATH%\" \"%%1\"" /f

echo Menu contextual instalado!
pause
