Unicode True
Name "CaptureClean"
VIProductVersion "2.10.1.0"
VIAddVersionKey "ProductName" "CaptureClean"
VIAddVersionKey "FileDescription" "Capturador de pantalla limpio"
VIAddVersionKey "FileVersion" "2.10.1"
VIAddVersionKey "CompanyName" "CaptureClean Project"
VIAddVersionKey "ProductVersion" "2.10.1"
VIAddVersionKey "LegalCopyright" "Copyright 2026 CaptureClean contributors"
Icon "../assets/CaptureClean.ico"
UninstallIcon "../assets/CaptureClean.ico"
OutFile "../dist/CaptureClean-Setup-2.10.1.exe"
InstallDir "$LOCALAPPDATA\\CaptureClean"
RequestExecutionLevel user
Page directory
Page instfiles
UninstPage uninstConfirm
UninstPage instfiles
Section
  ExecWait 'taskkill /IM CaptureClean.exe /F'
  Sleep 400
  SetOutPath "$INSTDIR"
  Delete "$INSTDIR\\CaptureClean.exe"
  Delete "$INSTDIR\\CaptureClean.ini"
  SetOverwrite on
  File "../dist/CaptureClean.exe"
  WriteUninstaller "$INSTDIR\\Uninstall.exe"
  CreateShortcut "$SMPROGRAMS\\CaptureClean.lnk" "$INSTDIR\\CaptureClean.exe"
  CreateShortcut "$DESKTOP\\CaptureClean.lnk" "$INSTDIR\\CaptureClean.exe"
  WriteRegStr HKCU "Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\CaptureClean" "DisplayName" "CaptureClean"
  WriteRegStr HKCU "Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\CaptureClean" "DisplayVersion" "2.10.1"
  WriteRegStr HKCU "Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\CaptureClean" "Publisher" "CaptureClean Project"
  WriteRegStr HKCU "Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\CaptureClean" "DisplayIcon" "$INSTDIR\\CaptureClean.exe"
  WriteRegStr HKCU "Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\CaptureClean" "UninstallString" '"$INSTDIR\\Uninstall.exe"'
  WriteRegStr HKCU "Software\\Microsoft\\Windows\\CurrentVersion\\Run" "CaptureClean" '"$INSTDIR\\CaptureClean.exe"'
  Exec "$INSTDIR\\CaptureClean.exe"
SectionEnd
Section "Uninstall"
  ExecWait 'taskkill /IM CaptureClean.exe /F'
  Delete "$DESKTOP\\CaptureClean.lnk"
  Delete "$SMPROGRAMS\\CaptureClean.lnk"
  Delete "$INSTDIR\\CaptureClean.exe"
  Delete "$INSTDIR\\CaptureClean.ini"
  Delete "$INSTDIR\\Uninstall.exe"
  RMDir "$INSTDIR"
  DeleteRegKey HKCU "Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\CaptureClean"
  DeleteRegValue HKCU "Software\\Microsoft\\Windows\\CurrentVersion\\Run" "CaptureClean"
SectionEnd
