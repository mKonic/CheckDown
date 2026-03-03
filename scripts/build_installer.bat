@echo off
setlocal
REM Build CheckDown installer
REM Prerequisites: NSIS must be installed and makensis.exe in PATH
REM                Release build must be completed first

echo === CheckDown Installer Build ===
echo.

REM Check Release build exists
set "BIN_DIR=%~dp0..\bin\Release"
if not exist "%BIN_DIR%\CheckDown.exe" (
    echo ERROR: Release build not found at %BIN_DIR%\CheckDown.exe
    echo Please build the Release configuration first.
    exit /b 1
)

REM Check NSIS
where makensis >nul 2>&1
if %errorlevel% neq 0 (
    echo ERROR: makensis.exe not found in PATH.
    echo Please install NSIS from https://nsis.sourceforge.io/
    exit /b 1
)

REM Create vcredist directory for VC runtime DLLs (optional)
set "VCREDIST_DIR=%~dp0..\installer\vcredist"
if not exist "%VCREDIST_DIR%" mkdir "%VCREDIST_DIR%"

REM Try to copy VC runtime from VS installation
set "VCRT_SRC=C:\Program Files\Microsoft Visual Studio\18\Community\VC\Redist\MSVC"
if exist "%VCRT_SRC%" (
    for /D %%d in ("%VCRT_SRC%\*") do (
        if exist "%%d\x64\Microsoft.VC*.CRT\vcruntime140.dll" (
            copy /Y "%%d\x64\Microsoft.VC*.CRT\vcruntime140.dll" "%VCREDIST_DIR%\" >nul 2>&1
            copy /Y "%%d\x64\Microsoft.VC*.CRT\vcruntime140_1.dll" "%VCREDIST_DIR%\" >nul 2>&1
            copy /Y "%%d\x64\Microsoft.VC*.CRT\msvcp140.dll" "%VCREDIST_DIR%\" >nul 2>&1
            echo Copied VC Runtime DLLs
        )
    )
) else (
    echo WARNING: Could not find VC Runtime DLLs. Installer will skip them.
    echo          Users may need VC Redistributable installed separately.
)

echo.
echo Building installer...
cd /d "%~dp0..\installer"
makensis checkdown.nsi
if %errorlevel% neq 0 (
    echo.
    echo ERROR: NSIS compilation failed.
    exit /b 1
)

echo.
echo === Installer built successfully! ===
echo Output: %~dp0..\installer\CheckDown-Setup.exe
echo.

endlocal
