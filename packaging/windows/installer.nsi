; ──────────────────────────────────────────────────
; Vibe Language Installer (NSIS)
; Produces vibe-setup.exe
; Installs to %LOCALAPPDATA%\Vibe (no admin needed)
; Adds Vibe to user PATH and provides an uninstaller
; ──────────────────────────────────────────────────

!include "MUI2.nsh"
!include "EnvVarUpdate.nsh"

; ── Metadata ──
!define PRODUCT_NAME "Vibe"
!define PRODUCT_VERSION "1.0.0"
!define PRODUCT_PUBLISHER "Vibe Language Team"
!define PRODUCT_WEB_SITE "https://vibelang.dev"

Name "${PRODUCT_NAME} ${PRODUCT_VERSION}"
OutFile "vibe-setup.exe"
InstallDir "$LOCALAPPDATA\${PRODUCT_NAME}"
RequestExecutionLevel user

; ── UI ──
!define MUI_ICON "..\..\vibe-vscode\images\vibe-icon.ico"
!define MUI_ABORTWARNING

!insertmacro MUI_PAGE_WELCOME
!insertmacro MUI_PAGE_DIRECTORY
!insertmacro MUI_PAGE_INSTFILES
!insertmacro MUI_PAGE_FINISH

!insertmacro MUI_UNPAGE_CONFIRM
!insertmacro MUI_UNPAGE_INSTFILES

!insertmacro MUI_LANGUAGE "English"

; ── Install Section ──
Section "Vibe Language" SecMain
  SetOutPath "$INSTDIR"

  ; Core binary
  File "..\..\build\vibe.exe"

  ; Documentation
  File "..\..\README.md"

  ; Example programs
  SetOutPath "$INSTDIR\examples"
  File "..\..\examples\hello.vibe"
  File "..\..\examples\fib.vibe"
  File "..\..\examples\functions.vibe"
  File "..\..\examples\match_demo.vibe"
  File "..\..\examples\showcase.vibe"
  File "..\..\examples\snake.vibe"

  ; Back to root
  SetOutPath "$INSTDIR"

  ; Add to user PATH
  ${EnvVarUpdate} $0 "PATH" "A" "HKCU" "$INSTDIR"

  ; Broadcast environment change
  SendMessage ${HWND_BROADCAST} ${WM_SETTINGCHANGE} 0 "STR:Environment" /TIMEOUT=5000

  ; Write uninstaller
  WriteUninstaller "$INSTDIR\uninstall.exe"

  ; Registry for Add/Remove Programs
  WriteRegStr HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\${PRODUCT_NAME}" \
              "DisplayName" "${PRODUCT_NAME} ${PRODUCT_VERSION}"
  WriteRegStr HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\${PRODUCT_NAME}" \
              "UninstallString" "$\"$INSTDIR\uninstall.exe$\""
  WriteRegStr HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\${PRODUCT_NAME}" \
              "DisplayVersion" "${PRODUCT_VERSION}"
  WriteRegStr HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\${PRODUCT_NAME}" \
              "Publisher" "${PRODUCT_PUBLISHER}"
  WriteRegStr HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\${PRODUCT_NAME}" \
              "URLInfoAbout" "${PRODUCT_WEB_SITE}"
  WriteRegDWORD HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\${PRODUCT_NAME}" \
              "NoModify" 1
  WriteRegDWORD HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\${PRODUCT_NAME}" \
              "NoRepair" 1
SectionEnd

; ── Uninstall Section ──
Section "Uninstall"
  ; Remove from PATH
  ${un.EnvVarUpdate} $0 "PATH" "R" "HKCU" "$INSTDIR"

  ; Remove files
  Delete "$INSTDIR\vibe.exe"
  Delete "$INSTDIR\README.md"
  Delete "$INSTDIR\uninstall.exe"

  ; Remove examples
  Delete "$INSTDIR\examples\hello.vibe"
  Delete "$INSTDIR\examples\fib.vibe"
  Delete "$INSTDIR\examples\functions.vibe"
  Delete "$INSTDIR\examples\match_demo.vibe"
  Delete "$INSTDIR\examples\showcase.vibe"
  Delete "$INSTDIR\examples\snake.vibe"
  RMDir "$INSTDIR\examples"

  ; Remove install directory
  RMDir "$INSTDIR"

  ; Remove registry entries
  DeleteRegKey HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\${PRODUCT_NAME}"

  ; Broadcast environment change
  SendMessage ${HWND_BROADCAST} ${WM_SETTINGCHANGE} 0 "STR:Environment" /TIMEOUT=5000
SectionEnd

