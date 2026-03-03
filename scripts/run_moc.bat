@echo off
setlocal enabledelayedexpansion
REM Usage: run_moc.bat <moc_exe> <src_dir> <generated_dir>
set "MOC_EXE=%~1"
set "SRC_DIR=%~2"
set "GEN_DIR=%~3"

if not exist "%GEN_DIR%" mkdir "%GEN_DIR%"

for /R "%SRC_DIR%" %%f in (*.h) do (
    findstr /M /C:"Q_OBJECT" "%%f" >nul 2>&1
    if !errorlevel! == 0 (
        set "MOC_OUT=%GEN_DIR%\moc_%%~nf.cpp"
        "%MOC_EXE%" "%%f" -o "!MOC_OUT!"
        if !errorlevel! == 0 (
            echo MOC: %%~nf.h
        ) else (
            echo MOC FAILED: %%~nf.h
        )
    )
)
endlocal
