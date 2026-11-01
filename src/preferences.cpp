#include "app.h"
#include "settings.h"
#include "version.h"
#include "logic.h"
#include <shlobj.h>
#include <shellscalingapi.h>
#include <string>

enum {
  ID_AUTOSTART = 100, ID_MAGNIFIER, ID_CONFIRM, ID_THICKNESS, ID_HIGHLIGHTER, ID_TEXTSIZE, ID_BOLD,
  ID_AUTOSAVE, ID_FOLDER, ID_BROWSE, ID_FORMAT
};

static HWND gPreferences;
static HFONT gFont;
static int gDpi = 96;
static int S(int v) { return MulDiv(v, gDpi, 96); }

static HWND AddControl(HWND parent, const wchar_t* cls, const wchar_t* text, DWORD style, int x, int y, int w, int h, int id) {
  HWND c = CreateWindowExW(0, cls, text, WS_CHILD | WS_VISIBLE | style, S(x), S(y), S(w), S(h), parent, (HMENU)(INT_PTR)id, gInst, nullptr);
  SendMessageW(c, WM_SETFONT, (WPARAM)gFont, TRUE);
  return c;
}
static void AddCheck(HWND parent, const wchar_t* text, int y, int id, bool checked) {
  AddControl(parent, L"BUTTON", text, WS_TABSTOP | BS_AUTOCHECKBOX, 16, y, 340, 22, id);
  CheckDlgButton(parent, id, checked ? BST_CHECKED : BST_UNCHECKED);
}
static void AddCombo(HWND parent, const wchar_t* label, int y, int id, const wchar_t* const (&items)[3], int selected) {
  AddControl(parent, L"STATIC", label, 0, 16, y + 3, 150, 20, 0);
  HWND c = AddControl(parent, L"COMBOBOX", L"", WS_TABSTOP | CBS_DROPDOWNLIST, 170, y, 186, 200, id);
  for (auto item : items) SendMessageW(c, CB_ADDSTRING, 0, (LPARAM)item);
  SendMessageW(c, CB_SETCURSEL, selected, 0);
}
template <size_t N>
static int IndexOf(int value, const int (&values)[N]) {
  for (size_t i = 0; i < N; i++) if (values[i] == value) return (int)i;
  return 0;
}
static int ComboIndex(HWND h, int id) {
  const LRESULT i = SendDlgItemMessageW(h, id, CB_GETCURSEL, 0, 0);
  return i == CB_ERR ? 0 : (int)i;
}
static void UpdateAutoSaveControls(HWND h) {
  const BOOL on = IsDlgButtonChecked(h, ID_AUTOSAVE) == BST_CHECKED;
  for (int id : {ID_FOLDER, ID_BROWSE, ID_FORMAT}) EnableWindow(GetDlgItem(h, id), on);
}

static void BrowseFolder(HWND h) {
  BROWSEINFOW info{};
  info.hwndOwner = h;
  info.lpszTitle = L"Selecciona la carpeta para guardar las capturas";
  info.ulFlags = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE;
  PIDLIST_ABSOLUTE item = SHBrowseForFolderW(&info);
  if (!item) return;
  wchar_t path[MAX_PATH];
  if (SHGetPathFromIDListW(item, path)) SetDlgItemTextW(h, ID_FOLDER, path);
  CoTaskMemFree(item);
}

static void Apply(HWND h) {
  std::wstring folder(32768, L'\0');
  folder.resize(GetDlgItemTextW(h, ID_FOLDER, folder.data(), 32768));
  const bool autoSave = IsDlgButtonChecked(h, ID_AUTOSAVE) == BST_CHECKED;
  if (autoSave && folder.empty()) {
    MessageBoxW(h, L"Selecciona una carpeta para el guardado automático.", L"CaptureClean", MB_OK | MB_ICONWARNING);
    return;
  }
  Settings& s = gSettings;
  s.showMagnifier = IsDlgButtonChecked(h, ID_MAGNIFIER) == BST_CHECKED;
  s.confirmOnEscape = IsDlgButtonChecked(h, ID_CONFIRM) == BST_CHECKED;
  s.thickness = kThicknessValues[ComboIndex(h, ID_THICKNESS)];
  s.highlighterWidth = kHighlighterValues[ComboIndex(h, ID_HIGHLIGHTER)];
  s.textSize = kTextSizeValues[ComboIndex(h, ID_TEXTSIZE)];
  s.textBold = IsDlgButtonChecked(h, ID_BOLD) == BST_CHECKED;
  s.autoSave = autoSave;
  s.autoSaveFolder = folder;
  s.autoSaveFormat = ComboIndex(h, ID_FORMAT);
  SaveSettings();
  if (SetAutoStart(IsDlgButtonChecked(h, ID_AUTOSTART) == BST_CHECKED)) ShowTip(L"Preferencias guardadas");
  else MessageBoxW(h, L"Las preferencias se guardaron, pero no se pudo cambiar el inicio con Windows.", L"CaptureClean", MB_OK | MB_ICONWARNING);
  DestroyWindow(h);
}

static LRESULT CALLBACK PreferencesProc(HWND h, UINT m, WPARAM w, LPARAM l) {
  switch (m) {
    case WM_CREATE: {
      const Settings& s = gSettings;
      AddControl(h, L"STATIC", L"Preferencias locales", 0, 16, 12, 340, 20, 0);
      AddCheck(h, L"Iniciar CaptureClean con Windows", 38, ID_AUTOSTART, AutoStartEnabled());
      AddCheck(h, L"Mostrar lupa durante la selección", 62, ID_MAGNIFIER, s.showMagnifier);
      AddCheck(h, L"Preguntar antes de cerrar con Esc", 86, ID_CONFIRM, s.confirmOnEscape);
      const wchar_t* const thick[3] = {L"Fino (2 px)", L"Medio (4 px)", L"Grueso (7 px)"};
      AddCombo(h, L"Grosor predeterminado:", 118, ID_THICKNESS, thick, IndexOf(s.thickness, kThicknessValues));
      const wchar_t* const high[3] = {L"Fino (12 px)", L"Medio (20 px)", L"Ancho (32 px)"};
      AddCombo(h, L"Tamaño del resaltador:", 150, ID_HIGHLIGHTER, high, IndexOf(s.highlighterWidth, kHighlighterValues));
      const wchar_t* const text[3] = {L"Pequeño (18 px)", L"Mediano (24 px)", L"Grande (32 px)"};
      AddCombo(h, L"Tamaño del texto:", 182, ID_TEXTSIZE, text, IndexOf(s.textSize, kTextSizeValues));
      AddCheck(h, L"Texto en negrita", 214, ID_BOLD, s.textBold);
      AddCheck(h, L"Guardar automáticamente al copiar o guardar", 246, ID_AUTOSAVE, s.autoSave);
      AddControl(h, L"STATIC", L"Carpeta:", 0, 16, 277, 60, 20, 0);
      AddControl(h, L"EDIT", s.autoSaveFolder.c_str(), WS_BORDER | WS_TABSTOP | ES_AUTOHSCROLL, 80, 274, 190, 24, ID_FOLDER);
      AddControl(h, L"BUTTON", L"Examinar…", WS_TABSTOP, 276, 273, 80, 26, ID_BROWSE);
      AddControl(h, L"STATIC", L"Formato:", 0, 16, 311, 60, 20, 0);
      HWND format = AddControl(h, L"COMBOBOX", L"", WS_TABSTOP | CBS_DROPDOWNLIST, 80, 308, 190, 100, ID_FORMAT);
      SendMessageW(format, CB_ADDSTRING, 0, (LPARAM)L"PNG (máxima calidad)");
      SendMessageW(format, CB_ADDSTRING, 0, (LPARAM)L"JPEG (archivo menor)");
      SendMessageW(format, CB_SETCURSEL, s.autoSaveFormat, 0);
      AddControl(h, L"BUTTON", L"Guardar", WS_TABSTOP | BS_DEFPUSHBUTTON, 186, 350, 82, 28, IDOK);
      AddControl(h, L"BUTTON", L"Cancelar", WS_TABSTOP, 274, 350, 82, 28, IDCANCEL);
      UpdateAutoSaveControls(h);
      return 0;
    }
    case WM_COMMAND:
      switch (LOWORD(w)) {
        case ID_AUTOSAVE: UpdateAutoSaveControls(h); return 0;
        case ID_BROWSE: BrowseFolder(h); return 0;
        case IDOK: Apply(h); return 0;
        case IDCANCEL: DestroyWindow(h); return 0;
      }
      break;
    case WM_DESTROY:
      gPreferences = nullptr;
      DeleteObject(gFont);
      gFont = nullptr;
      return 0;
  }
  return DefWindowProcW(h, m, w, l);
}

void ShowPreferences() {
  if (gPreferences) {
    SetForegroundWindow(gPreferences);
    return;
  }
  static bool registered = false;
  if (!registered) {
    WNDCLASSW c{};
    c.hInstance = gInst;
    c.hCursor = LoadCursor(nullptr, IDC_ARROW);
    c.hIcon = LoadIconW(gInst, MAKEINTRESOURCEW(101));
    c.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
    c.lpfnWndProc = PreferencesProc;
    c.lpszClassName = L"CaptureCleanPreferences";
    RegisterClassW(&c);
    registered = true;
  }
  POINT cursor;
  GetCursorPos(&cursor);
  HMONITOR monitor = MonitorFromPoint(cursor, MONITOR_DEFAULTTONEAREST);
  UINT dx = 96, dy = 96;
  if (SUCCEEDED(GetDpiForMonitor(monitor, MDT_EFFECTIVE_DPI, &dx, &dy))) gDpi = (int)dx;
  gFont = CreateFontW(-S(13), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");

  MONITORINFO mi{};
  mi.cbSize = sizeof(mi);
  GetMonitorInfoW(monitor, &mi);
  const DWORD style = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU;
  RECT frame{0, 0, S(372), S(394)};
  AdjustWindowRect(&frame, style, FALSE);
  const int w = logic::Width(frame), h = logic::Height(frame);
  const int x = mi.rcWork.left + (logic::Width(mi.rcWork) - w) / 2, y = mi.rcWork.top + (logic::Height(mi.rcWork) - h) / 2;
  const std::wstring title = std::wstring(L"Preferencias — CaptureClean ") + CC_VERSION_W;
  gPreferences = CreateWindowExW(0, L"CaptureCleanPreferences", title.c_str(), style, x, y, w, h, nullptr, nullptr, gInst, nullptr);
  ShowWindow(gPreferences, SW_SHOW);
  SetForegroundWindow(gPreferences);
}

bool PreferencesDialogMessage(MSG* msg) {
  return gPreferences && IsDialogMessageW(gPreferences, msg);
}
