; makensis -INPUTCHARSET UTF8 -DVERSION=x.y.z installer/CaptureClean.nsi (lo hace build.sh)
; En NSIS la barra invertida es literal: una sola, nunca doble.
;
; Parametros: /S silencioso, /D=carpeta (siempre el ultimo, sin comillas), /ABRIR abre la
; aplicacion al terminar una instalacion silenciosa.
!ifndef VERSION
  !error "Falta -DVERSION"
!endif
!define UNINST_KEY "Software\Microsoft\Windows\CurrentVersion\Uninstall\CaptureClean"
!define RUN_KEY "Software\Microsoft\Windows\CurrentVersion\Run"
!define APP_KEY "Software\CaptureClean"
!define DEFAULT_DIR "$LOCALAPPDATA\CaptureClean"
; Windows 10 1703: la aplicacion importa SetProcessDpiAwarenessContext, que no existe antes.
!define MIN_BUILD 15063

Unicode True
!include "MUI2.nsh"
!include "LogicLib.nsh"
!include "Sections.nsh"
!include "WinVer.nsh"
!include "x64.nsh"
!include "FileFunc.nsh"
!include "WordFunc.nsh"
!include "WinMessages.nsh"

Name "CaptureClean ${VERSION}"
BrandingText "CaptureClean ${VERSION}"
VIProductVersion "${VERSION}.0"
VIAddVersionKey /LANG=3082 "ProductName" "CaptureClean"
VIAddVersionKey /LANG=3082 "FileDescription" "Instalador de CaptureClean"
VIAddVersionKey /LANG=3082 "FileVersion" "${VERSION}"
VIAddVersionKey /LANG=3082 "CompanyName" "CaptureClean Project"
VIAddVersionKey /LANG=3082 "ProductVersion" "${VERSION}"
VIAddVersionKey /LANG=3082 "LegalCopyright" "Copyright 2026 CaptureClean contributors"
OutFile "../dist/CaptureClean-Setup-${VERSION}.exe"
SetCompressor /SOLID lzma
InstallDir "${DEFAULT_DIR}"
InstallDirRegKey HKCU "${UNINST_KEY}" "InstallLocation"
RequestExecutionLevel user
ManifestDPIAware true

Var OldVersion
Var WelcomeText

!define MUI_ICON "../assets/CaptureClean.ico"
!define MUI_UNICON "../assets/CaptureClean.ico"
!define MUI_ABORTWARNING
!define MUI_COMPONENTSPAGE_NODESC

!define MUI_WELCOMEPAGE_TEXT "$WelcomeText"
!insertmacro MUI_PAGE_WELCOME
!insertmacro MUI_PAGE_COMPONENTS
; Al actualizar no se pregunta la carpeta: se usa la de la instalacion existente.
!define MUI_PAGE_CUSTOMFUNCTION_PRE SkipDirectoryOnUpgrade
!insertmacro MUI_PAGE_DIRECTORY
!insertmacro MUI_PAGE_INSTFILES
!define MUI_FINISHPAGE_RUN "$INSTDIR\CaptureClean.exe"
!define MUI_FINISHPAGE_RUN_PARAMETERS "--installed"
!define MUI_FINISHPAGE_RUN_TEXT "Abrir CaptureClean"
!insertmacro MUI_PAGE_FINISH

!insertmacro MUI_UNPAGE_CONFIRM
!insertmacro MUI_UNPAGE_INSTFILES

!insertmacro MUI_LANGUAGE "Spanish"

; Pide cerrar la aplicacion y espera a que el ejecutable quede libre. taskkill solo entra si
; sigue en uso: las versiones anteriores a 3.3 tienen una ventana que FindWindow no encuentra.
!macro CLOSE_APP_FUNCTION un
Function ${un}CloseApp
  FindWindow $0 "CaptureCleanHost"
  ${If} $0 <> 0
    SendMessage $0 ${WM_CLOSE} 0 0 /TIMEOUT=2000
  ${EndIf}
  StrCpy $2 0
  ${Do}
    ${IfNot} ${FileExists} "$INSTDIR\CaptureClean.exe"
      Return
    ${EndIf}
    ClearErrors
    FileOpen $1 "$INSTDIR\CaptureClean.exe" a
    ${IfNot} ${Errors}
      FileClose $1
      Return
    ${EndIf}
    IntOp $2 $2 + 1
    ${If} $2 == 4
      nsExec::Exec 'taskkill /IM CaptureClean.exe'
      Pop $0
    ${ElseIf} $2 == 12
      nsExec::Exec 'taskkill /IM CaptureClean.exe /F'
      Pop $0
    ${ElseIf} $2 >= 24
      MessageBox MB_RETRYCANCEL|MB_ICONEXCLAMATION "CaptureClean sigue abierto y no se puede reemplazar.$\r$\nCiérralo desde la bandeja del sistema y pulsa Reintentar." /SD IDCANCEL IDRETRY retry
      SetErrorLevel 2
      Abort "CaptureClean sigue abierto."
      retry:
      StrCpy $2 0
    ${EndIf}
    Sleep 250
  ${Loop}
FunctionEnd
!macroend
!insertmacro CLOSE_APP_FUNCTION ""
!insertmacro CLOSE_APP_FUNCTION "un."

Section "CaptureClean (requerido)" SEC_MAIN
  SectionIn RO
  Call CloseApp
  SetOutPath "$INSTDIR"
  Delete "$INSTDIR\CaptureClean.ini"
  SetOverwrite on
  File "..\dist\CaptureClean.exe"
  WriteUninstaller "$INSTDIR\Uninstall.exe"
  CreateShortcut "$SMPROGRAMS\CaptureClean.lnk" "$INSTDIR\CaptureClean.exe"
SectionEnd

Section "Acceso directo en el escritorio" SEC_DESKTOP
SectionEnd

Section "Iniciar con Windows" SEC_AUTOSTART
SectionEnd

; Tambien aplica lo que se desmarco al actualizar.
Section "-Aplicar"
  ${If} ${SectionIsSelected} ${SEC_DESKTOP}
    CreateShortcut "$DESKTOP\CaptureClean.lnk" "$INSTDIR\CaptureClean.exe"
  ${Else}
    Delete "$DESKTOP\CaptureClean.lnk"
  ${EndIf}

  WriteRegDWORD HKCU "${APP_KEY}" "AutoStartConfigured" 1
  ${If} ${SectionIsSelected} ${SEC_AUTOSTART}
    WriteRegDWORD HKCU "${APP_KEY}" "AutoStartEnabled" 1
    WriteRegStr HKCU "${RUN_KEY}" "CaptureClean" '"$INSTDIR\CaptureClean.exe" --background'
  ${Else}
    WriteRegDWORD HKCU "${APP_KEY}" "AutoStartEnabled" 0
    DeleteRegValue HKCU "${RUN_KEY}" "CaptureClean"
  ${EndIf}

  WriteRegStr HKCU "${UNINST_KEY}" "DisplayName" "CaptureClean"
  WriteRegStr HKCU "${UNINST_KEY}" "DisplayVersion" "${VERSION}"
  WriteRegStr HKCU "${UNINST_KEY}" "Publisher" "CaptureClean Project"
  WriteRegStr HKCU "${UNINST_KEY}" "DisplayIcon" "$INSTDIR\CaptureClean.exe"
  WriteRegStr HKCU "${UNINST_KEY}" "InstallLocation" "$INSTDIR"
  WriteRegStr HKCU "${UNINST_KEY}" "UninstallString" '"$INSTDIR\Uninstall.exe"'
  WriteRegStr HKCU "${UNINST_KEY}" "QuietUninstallString" '"$INSTDIR\Uninstall.exe" /S'
  WriteRegDWORD HKCU "${UNINST_KEY}" "NoModify" 1
  WriteRegDWORD HKCU "${UNINST_KEY}" "NoRepair" 1
  ${GetSize} "$INSTDIR" "/S=0K" $0 $1 $2
  IntFmt $0 "0x%08X" $0
  WriteRegDWORD HKCU "${UNINST_KEY}" "EstimatedSize" $0

  ${If} ${Silent}
    ${GetParameters} $0
    ClearErrors
    ${GetOptions} $0 "/ABRIR" $1
    ${IfNot} ${Errors}
      Exec '"$INSTDIR\CaptureClean.exe" --installed'
    ${EndIf}
  ${EndIf}
SectionEnd

Function .onInit
  ${IfNot} ${RunningX64}
    MessageBox MB_OK|MB_ICONSTOP "CaptureClean necesita Windows de 64 bits." /SD IDOK
    SetErrorLevel 3
    Quit
  ${EndIf}
  ${IfNot} ${AtLeastBuild} ${MIN_BUILD}
    MessageBox MB_OK|MB_ICONSTOP "CaptureClean necesita Windows 10 versión 1703 o posterior." /SD IDOK
    SetErrorLevel 3
    Quit
  ${EndIf}

  ; Las versiones anteriores no guardaban InstallLocation: la carpeta sale de DisplayIcon, que
  ; ademas podia llevar barras dobles. /D solo se aplica si no hay otra instalacion.
  ReadRegStr $2 HKCU "${UNINST_KEY}" "InstallLocation"
  ${If} $2 == ""
    ReadRegStr $0 HKCU "${UNINST_KEY}" "DisplayIcon"
    ${If} $0 != ""
      ${GetParent} $0 $0
      GetFullPathName $0 $0
      StrCpy $1 $0 1 -1
      ${If} $1 == "\"
        StrCpy $0 $0 -1
      ${EndIf}
      StrCpy $2 $0
      ${If} $INSTDIR == "${DEFAULT_DIR}"
      ${AndIf} ${FileExists} "$0\CaptureClean.exe"
        StrCpy $INSTDIR $0
      ${EndIf}
    ${EndIf}
  ${EndIf}
  ${If} $2 != ""
  ${AndIf} $2 != $INSTDIR
  ${AndIf} ${FileExists} "$2\CaptureClean.exe"
    MessageBox MB_OK|MB_ICONSTOP "Ya hay una instalación en $2. Desinstálala antes de elegir otra carpeta." /SD IDOK
    SetErrorLevel 4
    Quit
  ${EndIf}

  ReadRegStr $OldVersion HKCU "${UNINST_KEY}" "DisplayVersion"
  ${IfNot} ${FileExists} "$INSTDIR\CaptureClean.exe"
    StrCpy $OldVersion ""
  ${EndIf}

  ; La casilla muestra lo guardado, tambien al reinstalar tras conservar las preferencias.
  ClearErrors
  ReadRegDWORD $0 HKCU "${APP_KEY}" "AutoStartEnabled"
  ${IfNot} ${Errors}
  ${AndIf} $0 == 0
    !insertmacro UnselectSection ${SEC_AUTOSTART}
  ${EndIf}

  ${If} $OldVersion == ""
    StrCpy $WelcomeText "Este asistente instalará CaptureClean ${VERSION}, un capturador de pantalla local: no sube nada a internet ni necesita permisos de administrador."
    Return
  ${EndIf}

  StrCpy $WelcomeText "Este asistente actualizará CaptureClean de la versión $OldVersion a la ${VERSION}.$\r$\n$\r$\nSe conservan tus preferencias y tus capturas."
  ${VersionCompare} "$OldVersion" "${VERSION}" $0
  ${If} $0 == 1
    MessageBox MB_YESNO|MB_ICONEXCLAMATION "Ya tienes CaptureClean $OldVersion, que es más reciente que la ${VERSION}.$\r$\n¿Quieres instalar de todos modos la versión anterior?" /SD IDNO IDYES downgrade
    Quit
    downgrade:
    StrCpy $WelcomeText "Este asistente sustituirá CaptureClean $OldVersion por la versión anterior ${VERSION}."
  ${ElseIf} $0 == 0
    StrCpy $WelcomeText "CaptureClean ${VERSION} ya está instalado. Este asistente volverá a instalarlo, conservando tus preferencias y tus capturas."
  ${EndIf}

  ; Al actualizar, la casilla muestra lo que el usuario ya tenia.
  ${IfNot} ${FileExists} "$DESKTOP\CaptureClean.lnk"
    !insertmacro UnselectSection ${SEC_DESKTOP}
  ${EndIf}
FunctionEnd

Function SkipDirectoryOnUpgrade
  ${If} $OldVersion != ""
    Abort
  ${EndIf}
FunctionEnd

Section "Uninstall"
  Call un.CloseApp
  Delete "$DESKTOP\CaptureClean.lnk"
  Delete "$SMPROGRAMS\CaptureClean.lnk"
  Delete "$INSTDIR\CaptureClean.exe"
  Delete "$INSTDIR\CaptureClean.ini"
  Delete "$INSTDIR\Uninstall.exe"
  RMDir "$INSTDIR"
  DeleteRegKey HKCU "${UNINST_KEY}"
  DeleteRegValue HKCU "${RUN_KEY}" "CaptureClean"
  ; Las capturas guardadas nunca se tocan. En silencio se conservan tambien las preferencias.
  MessageBox MB_YESNO|MB_ICONQUESTION "¿Quieres borrar también tus preferencias de CaptureClean?$\r$\nTus capturas guardadas no se borrarán." /SD IDNO IDNO keep
  DeleteRegKey HKCU "${APP_KEY}"
  keep:
SectionEnd
