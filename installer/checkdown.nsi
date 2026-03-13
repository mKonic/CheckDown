; CheckDown NSIS Installer Script
; Builds a Windows installer for CheckDown Download Manager

!include "MUI2.nsh"
!include "FileFunc.nsh"

; ---------------------------------------------------------------------------
; General
; ---------------------------------------------------------------------------
Name "CheckDown"
OutFile "CheckDown-Setup.exe"
InstallDir "$PROGRAMFILES64\CheckDown"
InstallDirRegKey HKLM "Software\CheckDown" "InstallDir"
RequestExecutionLevel admin
SetCompressor /SOLID lzma

; Version info
VIProductVersion "1.0.1.0"
VIAddVersionKey "ProductName" "CheckDown"
VIAddVersionKey "ProductVersion" "1.0.1"
VIAddVersionKey "FileDescription" "CheckDown Download Manager Installer"
VIAddVersionKey "FileVersion" "1.0.1.0"
VIAddVersionKey "LegalCopyright" "Copyright (c) 2026"

; ---------------------------------------------------------------------------
; Variables
; ---------------------------------------------------------------------------
Var StartMenuFolder

; ---------------------------------------------------------------------------
; MUI Settings
; ---------------------------------------------------------------------------
!define MUI_ABORTWARNING
!define MUI_ICON "..\resources\checkdown.ico"
!define MUI_UNICON "..\resources\checkdown.ico"

; Welcome page
!insertmacro MUI_PAGE_WELCOME

; License page
!insertmacro MUI_PAGE_LICENSE "..\installer\license.txt"

; Directory page
!insertmacro MUI_PAGE_DIRECTORY

; Start menu folder page
!define MUI_STARTMENUPAGE_REGISTRY_ROOT "HKLM"
!define MUI_STARTMENUPAGE_REGISTRY_KEY "Software\CheckDown"
!define MUI_STARTMENUPAGE_REGISTRY_VALUENAME "StartMenuFolder"
!insertmacro MUI_PAGE_STARTMENU Application $StartMenuFolder

; Components page
!insertmacro MUI_PAGE_COMPONENTS

; Install files page
!insertmacro MUI_PAGE_INSTFILES

; Finish page
!define MUI_FINISHPAGE_RUN "$INSTDIR\CheckDown.exe"
!define MUI_FINISHPAGE_RUN_TEXT "Launch CheckDown"
!insertmacro MUI_PAGE_FINISH

; Uninstaller pages
!insertmacro MUI_UNPAGE_CONFIRM
!insertmacro MUI_UNPAGE_INSTFILES

; Language
!insertmacro MUI_LANGUAGE "English"

; ---------------------------------------------------------------------------
; Sections
; ---------------------------------------------------------------------------

Section "CheckDown (required)" SecCore
    SectionIn RO  ; Read-only (always installed)

    SetOutPath "$INSTDIR"

    ; Main executable
    File "..\bin\Release\CheckDown.exe"

    ; Qt DLLs
    File "..\bin\Release\Qt6Core.dll"
    File "..\bin\Release\Qt6Gui.dll"
    File "..\bin\Release\Qt6Widgets.dll"
    File "..\bin\Release\Qt6Network.dll"

    ; Qt platform plugin
    SetOutPath "$INSTDIR\platforms"
    File "..\bin\Release\platforms\qwindows.dll"

    ; Qt styles plugin (if exists)
    SetOutPath "$INSTDIR\styles"
    File /nonfatal "..\deps\qt\6.8.3\msvc2022_64\plugins\styles\qmodernwindowsstyle.dll"

    ; VC Runtime (user may already have these, but include just in case)
    SetOutPath "$INSTDIR"
    File /nonfatal "vcredist\vcruntime140.dll"
    File /nonfatal "vcredist\vcruntime140_1.dll"
    File /nonfatal "vcredist\msvcp140.dll"

    ; yt-dlp binary
    SetOutPath "$INSTDIR\vendor\yt-dlp"
    File "..\vendor\yt-dlp\yt-dlp.exe"

    ; Chrome Extension (bundle in install dir for easy loading)
    SetOutPath "$INSTDIR\extension"
    File /r "..\extension\*.*"

    ; Write install dir to registry
    WriteRegStr HKLM "Software\CheckDown" "InstallDir" "$INSTDIR"

    ; Create uninstaller
    WriteUninstaller "$INSTDIR\uninstall.exe"

    ; Add/Remove Programs entry
    WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\CheckDown" \
        "DisplayName" "CheckDown - Download Manager"
    WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\CheckDown" \
        "UninstallString" '"$INSTDIR\uninstall.exe"'
    WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\CheckDown" \
        "InstallLocation" "$INSTDIR"
    WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\CheckDown" \
        "Publisher" "CheckDown"
    WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\CheckDown" \
        "DisplayVersion" "1.0.1"
    WriteRegDWORD HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\CheckDown" \
        "NoModify" 1
    WriteRegDWORD HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\CheckDown" \
        "NoRepair" 1

    ; Calculate installed size
    ${GetSize} "$INSTDIR" "/S=0K" $0 $1 $2
    IntFmt $0 "0x%08X" $0
    WriteRegDWORD HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\CheckDown" \
        "EstimatedSize" $0

    ; Start Menu shortcuts
    !insertmacro MUI_STARTMENU_WRITE_BEGIN Application
        CreateDirectory "$SMPROGRAMS\$StartMenuFolder"
        CreateShortCut "$SMPROGRAMS\$StartMenuFolder\CheckDown.lnk" "$INSTDIR\CheckDown.exe"
        CreateShortCut "$SMPROGRAMS\$StartMenuFolder\Uninstall.lnk" "$INSTDIR\uninstall.exe"
    !insertmacro MUI_STARTMENU_WRITE_END
SectionEnd

Section "Desktop Shortcut" SecDesktop
    CreateShortCut "$DESKTOP\CheckDown.lnk" "$INSTDIR\CheckDown.exe"
SectionEnd

Section "Start with Windows" SecStartup
    WriteRegStr HKCU "Software\Microsoft\Windows\CurrentVersion\Run" \
        "CheckDown" '"$INSTDIR\CheckDown.exe" --minimized'
SectionEnd

; ---------------------------------------------------------------------------
; Section descriptions
; ---------------------------------------------------------------------------
!insertmacro MUI_FUNCTION_DESCRIPTION_BEGIN
    !insertmacro MUI_DESCRIPTION_TEXT ${SecCore} \
        "Core application files (required)."
    !insertmacro MUI_DESCRIPTION_TEXT ${SecDesktop} \
        "Create a shortcut on the Desktop."
    !insertmacro MUI_DESCRIPTION_TEXT ${SecStartup} \
        "Automatically start CheckDown when Windows starts (minimized to tray)."
!insertmacro MUI_FUNCTION_DESCRIPTION_END

; ---------------------------------------------------------------------------
; Uninstaller
; ---------------------------------------------------------------------------
Section "Uninstall"
    ; Kill running instance
    nsExec::ExecToLog 'taskkill /F /IM CheckDown.exe'

    ; Remove files
    Delete "$INSTDIR\CheckDown.exe"
    Delete "$INSTDIR\Qt6Core.dll"
    Delete "$INSTDIR\Qt6Gui.dll"
    Delete "$INSTDIR\Qt6Widgets.dll"
    Delete "$INSTDIR\Qt6Network.dll"
    Delete "$INSTDIR\vcruntime140.dll"
    Delete "$INSTDIR\vcruntime140_1.dll"
    Delete "$INSTDIR\msvcp140.dll"
    Delete "$INSTDIR\platforms\qwindows.dll"
    RMDir "$INSTDIR\platforms"
    Delete "$INSTDIR\styles\qmodernwindowsstyle.dll"
    RMDir "$INSTDIR\styles"

    ; Remove yt-dlp
    Delete "$INSTDIR\vendor\yt-dlp\yt-dlp.exe"
    RMDir "$INSTDIR\vendor\yt-dlp"
    RMDir "$INSTDIR\vendor"

    ; Remove extension
    RMDir /r "$INSTDIR\extension"

    ; Remove uninstaller
    Delete "$INSTDIR\uninstall.exe"
    RMDir "$INSTDIR"

    ; Remove Start Menu shortcuts
    !insertmacro MUI_STARTMENU_GETFOLDER Application $StartMenuFolder
    Delete "$SMPROGRAMS\$StartMenuFolder\CheckDown.lnk"
    Delete "$SMPROGRAMS\$StartMenuFolder\Uninstall.lnk"
    RMDir "$SMPROGRAMS\$StartMenuFolder"

    ; Remove Desktop shortcut
    Delete "$DESKTOP\CheckDown.lnk"

    ; Remove registry entries
    DeleteRegKey HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\CheckDown"
    DeleteRegKey HKLM "Software\CheckDown"
    DeleteRegValue HKCU "Software\Microsoft\Windows\CurrentVersion\Run" "CheckDown"
SectionEnd
