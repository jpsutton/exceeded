; Exceeded installer for Windows (NSIS 3, Modern UI 2).
;
; makensis /DVERSION=<x.y.z> /DSRCDIR=<folder with exceeded.exe, the DLLs, soundfont\ and licenses\> /DOUTFILE=<installer.exe> exceeded.nsi
;
; Installs per user by default (no administrator prompt); machine-wide when chosen and allowed.
; Players, scores and settings live in %LOCALAPPDATA%\OutNumbered and are not touched by the installer or the uninstaller.

Unicode true
SetCompressor /SOLID lzma

!ifndef VERSION
  !error "VERSION is not defined"
!endif
!ifndef SRCDIR
  !error "SRCDIR is not defined"
!endif
!ifndef OUTFILE
  !define OUTFILE "exceeded-${VERSION}-windows-x64-setup.exe"
!endif

!define APPNAME "Exceeded"
!define PUBLISHER "Jared Sutton"
!define WEBSITE "https://github.com/jpsutton/exceeded"
!define UNINSTKEY "Software\Microsoft\Windows\CurrentVersion\Uninstall\Exceeded"
!define APPKEY "Software\Exceeded"
!define ICON "..\..\assets\exceeded.ico"

Name "${APPNAME} ${VERSION}"
OutFile "${OUTFILE}"
BrandingText "${APPNAME} ${VERSION}"

; Per-user or machine-wide installs (MultiUser.nsh ships with NSIS).
!define MULTIUSER_EXECUTIONLEVEL Highest
!define MULTIUSER_MUI
!define MULTIUSER_INSTALLMODE_COMMANDLINE
!define MULTIUSER_INSTALLMODE_DEFAULT_CURRENTUSER
!define MULTIUSER_INSTALLMODE_INSTDIR "${APPNAME}"
!define MULTIUSER_INSTALLMODE_INSTDIR_REGISTRY_KEY "${APPKEY}"
!define MULTIUSER_INSTALLMODE_INSTDIR_REGISTRY_VALUENAME "InstallDir"
!define MULTIUSER_USE_PROGRAMFILES64
!include MultiUser.nsh
!include MUI2.nsh
!include LogicLib.nsh

VIProductVersion "${VERSION}.0"
VIAddVersionKey "ProductName" "${APPNAME}"
VIAddVersionKey "ProductVersion" "${VERSION}"
VIAddVersionKey "FileVersion" "${VERSION}"
VIAddVersionKey "FileDescription" "${APPNAME} installer"
VIAddVersionKey "CompanyName" "${PUBLISHER}"
VIAddVersionKey "LegalCopyright" "GPL-3.0-only"

!define MUI_ICON "${ICON}"
!define MUI_UNICON "${ICON}"
!define MUI_ABORTWARNING
!define MUI_COMPONENTSPAGE_SMALLDESC
!define MUI_FINISHPAGE_RUN "$INSTDIR\exceeded.exe"
!define MUI_FINISHPAGE_RUN_TEXT "Start ${APPNAME}"
!define MUI_FINISHPAGE_SHOWREADME "$INSTDIR\README.md"
!define MUI_FINISHPAGE_SHOWREADME_TEXT "Show the README"
!define MUI_FINISHPAGE_SHOWREADME_NOTCHECKED

!insertmacro MUI_PAGE_WELCOME
!insertmacro MUI_PAGE_LICENSE "..\..\LICENSE"
!insertmacro MULTIUSER_PAGE_INSTALLMODE
!insertmacro MUI_PAGE_COMPONENTS
!insertmacro MUI_PAGE_DIRECTORY
!insertmacro MUI_PAGE_INSTFILES
!insertmacro MUI_PAGE_FINISH

!define MUI_UNCONFIRMPAGE_TEXT_TOP "Exceeded will be removed from your computer. Your players, scores and settings (in %LOCALAPPDATA%\OutNumbered) and the game files copied in by Exceeded's own setup are kept."
!insertmacro MUI_UNPAGE_CONFIRM
!insertmacro MUI_UNPAGE_INSTFILES

!insertmacro MUI_LANGUAGE "English"

Section "Exceeded (required)" SecMain
  SectionIn RO
  SetOutPath "$INSTDIR"
  File "${SRCDIR}\exceeded.exe"
  File "${SRCDIR}\README.md"
  File "${SRCDIR}\LICENSE"
  File "${SRCDIR}\*.dll"
  SetOutPath "$INSTDIR\soundfont"
  File /r "${SRCDIR}\soundfont\*.*"
  SetOutPath "$INSTDIR\licenses"
  File /r "${SRCDIR}\licenses\*.*"
  SetOutPath "$INSTDIR"
  WriteUninstaller "$INSTDIR\uninstall.exe"

  WriteRegStr SHCTX "${APPKEY}" "InstallDir" "$INSTDIR"
  WriteRegStr SHCTX "${UNINSTKEY}" "DisplayName" "${APPNAME}"
  WriteRegStr SHCTX "${UNINSTKEY}" "DisplayVersion" "${VERSION}"
  WriteRegStr SHCTX "${UNINSTKEY}" "Publisher" "${PUBLISHER}"
  WriteRegStr SHCTX "${UNINSTKEY}" "URLInfoAbout" "${WEBSITE}"
  WriteRegStr SHCTX "${UNINSTKEY}" "DisplayIcon" "$INSTDIR\exceeded.exe"
  WriteRegStr SHCTX "${UNINSTKEY}" "InstallLocation" "$INSTDIR"
  WriteRegStr SHCTX "${UNINSTKEY}" "UninstallString" '"$INSTDIR\uninstall.exe" /$MultiUser.InstallMode'
  WriteRegStr SHCTX "${UNINSTKEY}" "QuietUninstallString" '"$INSTDIR\uninstall.exe" /$MultiUser.InstallMode /S'
  WriteRegDWORD SHCTX "${UNINSTKEY}" "NoModify" 1
  WriteRegDWORD SHCTX "${UNINSTKEY}" "NoRepair" 1
  SectionGetSize ${SecMain} $0
  WriteRegDWORD SHCTX "${UNINSTKEY}" "EstimatedSize" $0
SectionEnd

Section "Start menu shortcut" SecStartMenu
  CreateShortCut "$SMPROGRAMS\${APPNAME}.lnk" "$INSTDIR\exceeded.exe"
SectionEnd

Section /o "Desktop shortcut" SecDesktop
  CreateShortCut "$DESKTOP\${APPNAME}.lnk" "$INSTDIR\exceeded.exe"
SectionEnd

!insertmacro MUI_FUNCTION_DESCRIPTION_BEGIN
  !insertmacro MUI_DESCRIPTION_TEXT ${SecMain} "The program, its libraries and the SoundFont."
  !insertmacro MUI_DESCRIPTION_TEXT ${SecStartMenu} "A shortcut in the Start menu."
  !insertmacro MUI_DESCRIPTION_TEXT ${SecDesktop} "A shortcut on the desktop."
!insertmacro MUI_FUNCTION_DESCRIPTION_END

Function .onInit
  !insertmacro MULTIUSER_INIT
FunctionEnd

Function un.onInit
  !insertmacro MULTIUSER_UNINIT
FunctionEnd

Section "Uninstall"
  Delete "$INSTDIR\exceeded.exe"
  Delete "$INSTDIR\README.md"
  Delete "$INSTDIR\LICENSE"
  Delete "$INSTDIR\*.dll"
  RMDir /r "$INSTDIR\soundfont"
  RMDir /r "$INSTDIR\licenses"
  Delete "$INSTDIR\uninstall.exe"
  RMDir "$INSTDIR"
  Delete "$SMPROGRAMS\${APPNAME}.lnk"
  Delete "$DESKTOP\${APPNAME}.lnk"
  DeleteRegKey SHCTX "${APPKEY}"
  DeleteRegKey SHCTX "${UNINSTKEY}"
SectionEnd
