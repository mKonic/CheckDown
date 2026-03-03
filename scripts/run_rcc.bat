@echo off
setlocal
REM Usage: run_rcc.bat <rcc_exe> <resources_dir> <generated_dir>
set "RCC_EXE=%~1"
set "RES_DIR=%~2"
set "GEN_DIR=%~3"

if not exist "%GEN_DIR%" mkdir "%GEN_DIR%"

set "QRC_FILE=%RES_DIR%\resources.qrc"
set "OUT_FILE=%GEN_DIR%\qrc_resources.cpp"

if not exist "%QRC_FILE%" (
    echo RCC: No resources.qrc found, skipping
    exit /b 0
)

"%RCC_EXE%" "%QRC_FILE%" -o "%OUT_FILE%"
if %errorlevel% == 0 (
    echo RCC: resources.qrc compiled
) else (
    echo RCC FAILED: resources.qrc
)

endlocal
