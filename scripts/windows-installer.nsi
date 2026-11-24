; SPDX-License-Identifier: GPL-2.0-or-later
Unicode true
RequestExecutionLevel user

!include "MUI2.nsh"

!define APP "Abuse Modern"
!define UNINSTALL_KEY "Software\Microsoft\Windows\CurrentVersion\Uninstall\AbuseModern"

Name "${APP}"
OutFile "${SETUP_FILE}"
InstallDir "$LOCALAPPDATA\Programs\Abuse Modern"
SetCompressor /SOLID lzma
VIProductVersion "${PRODUCT_VERSION}.0"
VIAddVersionKey "ProductName" "${APP}"
VIAddVersionKey "FileDescription" "${APP} Setup"
VIAddVersionKey "FileVersion" "${PRODUCT_VERSION}"
VIAddVersionKey "LegalCopyright" "Abuse Modern contributors"

!define MUI_FINISHPAGE_RUN "$INSTDIR\abuse-launcher.exe"
!define MUI_FINISHPAGE_RUN_TEXT "Launch Abuse Modern"
!insertmacro MUI_PAGE_WELCOME
!insertmacro MUI_PAGE_INSTFILES
!insertmacro MUI_PAGE_FINISH
!insertmacro MUI_UNPAGE_CONFIRM
!insertmacro MUI_UNPAGE_INSTFILES
!insertmacro MUI_LANGUAGE "English"

Section "Install"
    SetOutPath "$INSTDIR"
    File /r "${PACKAGE_DIR}\*"
    WriteUninstaller "$INSTDIR\Uninstall.exe"
    CreateShortcut "$SMPROGRAMS\Abuse Modern.lnk" "$INSTDIR\abuse-launcher.exe"

    WriteRegStr HKCU "${UNINSTALL_KEY}" "DisplayName" "${APP}"
    WriteRegStr HKCU "${UNINSTALL_KEY}" "DisplayVersion" "${PRODUCT_VERSION}"
    WriteRegStr HKCU "${UNINSTALL_KEY}" "InstallLocation" "$INSTDIR"
    WriteRegStr HKCU "${UNINSTALL_KEY}" "UninstallString" '"$INSTDIR\Uninstall.exe"'
    WriteRegDWORD HKCU "${UNINSTALL_KEY}" "NoModify" 1
    WriteRegDWORD HKCU "${UNINSTALL_KEY}" "NoRepair" 1
SectionEnd

Section "Uninstall"
    Delete "$SMPROGRAMS\Abuse Modern.lnk"
    DeleteRegKey HKCU "${UNINSTALL_KEY}"

    Delete "$INSTDIR\abuse-launcher.exe"
    Delete "$INSTDIR\START_HERE.txt"
    Delete "$INSTDIR\LICENSE"
    Delete "$INSTDIR\bin\abuse.exe"
    Delete "$INSTDIR\bin\abuse-tool.exe"
    Delete "$INSTDIR\licenses\SDL3.txt"
    Delete "$INSTDIR\licenses\SDL3_mixer.txt"
    Delete "$INSTDIR\licenses\TiMidity.txt"
    Delete "$INSTDIR\licenses\Python.txt"
    Delete "$INSTDIR\licenses\pyinstaller.txt"
    Delete "$INSTDIR\licenses\certifi.txt"
    Delete "$INSTDIR\share\abuse-modern\LICENSE"
    Delete "$INSTDIR\share\abuse-modern\menu-font-license.txt"
    Delete "$INSTDIR\Uninstall.exe"
    RMDir "$INSTDIR\bin"
    RMDir "$INSTDIR\licenses"
    RMDir "$INSTDIR\share\abuse-modern"
    RMDir "$INSTDIR\share"
    RMDir "$INSTDIR"
SectionEnd
