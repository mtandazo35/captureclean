; makensis -DVERSION=x.y.z installer/CaptureClean.nsi (lo hace build.sh)
; En NSIS la barra invertida es literal: una sola, nunca doble.
!ifndef VERSION
  !error "Falta -DVERSION"
!endif
!define UNINST_KEY "Software\Microsoft\Windows\CurrentVersion\Uninstall\CaptureClean"
!define RUN_KEY "Software\Microsoft\Windows\CurrentVersion\Run"

Unicode True
Name "CaptureClean"
VIProductVersion "${VERSION}.0"
VIAddVersionKey "ProductName" "CaptureClean"
VIAddVersionKey "FileDescription" "Capturador de pantalla limpio"
VIAddVersionKey "FileVersion" "${VERSION}"
VIAddVersionKey "CompanyName" "CaptureClean Project"
VIAddVersionKey "ProductVersion" "${VERSION}"
VIAddVersionKey "LegalCopyright" "Copyright 2026 CaptureClean contributors"
Icon "../assets/CaptureClean.ico"
UninstallIcon "../assets/CaptureClean.ico"
OutFile "../dist/CaptureClean-Setup-${VERSION}.exe"
InstallDir "$LOCALAPPDATA\CaptureClean"
RequestExecutionLevel user
Page directory
Page instfiles
UninstPage uninstConfirm
UninstPage instfiles

Section
  ; Primero pide cerrar (la bandeja se limpia sola); /F solo si no responde.
  nsExec::Exec 'taskkill /IM CaptureClean.exe'
  Pop $0
  Sleep 800
  nsExec::Exec 'taskkill /IM CaptureClean.exe /F'
  Pop $0
  Sleep 300
  SetOutPath "$INSTDIR"
  Delete "$INSTDIR\CaptureClean.ini"
  SetOverwrite on
  File "../dist/CaptureClean.exe"
  WriteUninstaller "$INSTDIR\Uninstall.exe"
  CreateShortcut "$SMPROGRAMS\CaptureClean.lnk" "$INSTDIR\CaptureClean.exe"
  CreateShortcut "$DESKTOP\CaptureClean.lnk" "$INSTDIR\CaptureClean.exe"
  WriteRegStr HKCU "${UNINST_KEY}" "DisplayName" "CaptureClean"
  WriteRegStr HKCU "${UNINST_KEY}" "DisplayVersion" "${VERSION}"
  WriteRegStr HKCU "${UNINST_KEY}" "Publisher" "CaptureClean Project"
  WriteRegStr HKCU "${UNINST_KEY}" "DisplayIcon" "$INSTDIR\CaptureClean.exe"
  WriteRegStr HKCU "${UNINST_KEY}" "UninstallString" '"$INSTDIR\Uninstall.exe"'
  ; El inicio con Windows lo gestiona la aplicacion, para no pisar lo que eligio el usuario.
  Exec '"$INSTDIR\CaptureClean.exe" --installed'
SectionEnd

Section "Uninstall"
  nsExec::Exec 'taskkill /IM CaptureClean.exe'
  Pop $0
  Sleep 800
  nsExec::Exec 'taskkill /IM CaptureClean.exe /F'
  Pop $0
  Sleep 300
  Delete "$DESKTOP\CaptureClean.lnk"
  Delete "$SMPROGRAMS\CaptureClean.lnk"
  Delete "$INSTDIR\CaptureClean.exe"
  Delete "$INSTDIR\CaptureClean.ini"
  Delete "$INSTDIR\Uninstall.exe"
  RMDir "$INSTDIR"
  DeleteRegKey HKCU "${UNINST_KEY}"
  DeleteRegValue HKCU "${RUN_KEY}" "CaptureClean"
SectionEnd
