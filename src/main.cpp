#include "app.h"
#include "settings.h"
#include "version.h"
#include <windowsx.h>
#include <shellapi.h>
#include <gdiplus.h>
#include <string>
#include <cwchar>

HINSTANCE gInst;
HWND gHost;

static constexpr UINT WM_TRAY = WM_APP + 10;
// Lo envia otra instancia al lanzarla con el programa ya abierto.
static constexpr UINT WM_APP_CAPTURE = WM_APP + 11;
static constexpr UINT TIMER_CAPTURE = 77, TIMER_TRAY_RETRY = 78;
static constexpr int kTrayRetries = 30;

enum { HOTKEY_AREA = 1, HOTKEY_AREA_ALT, HOTKEY_WINDOW, HOTKEY_REPEAT };
enum { MENU_AREA = 1, MENU_WINDOW, MENU_FULL, MENU_REPEAT, MENU_PREFERENCES, MENU_EXIT };

static HICON gIcon;
static UINT gTaskbarCreated;
static CaptureMode gPendingMode = CaptureMode::Area;
static HWND gLastForeground;
static RECT gLastSelection;
static bool gHasLastSelection;
static int gTrayRetries;
static bool gHotkeyBusy;

void RememberSelection(const RECT& screenRect) {
  gLastSelection = screenRect;
  gHasLastSelection = true;
}

static bool AddTray(HWND h) {
  NOTIFYICONDATAW n{};
  n.cbSize = sizeof(n);
  n.hWnd = h;
  n.uID = 1;
  n.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP;
  n.uCallbackMessage = WM_TRAY;
  n.hIcon = gIcon;
  wcsncpy(n.szTip, (std::wstring(L"CaptureClean ") + CC_VERSION_W + L" — Impr Pant para capturar").c_str(), 127);
  return Shell_NotifyIconW(NIM_ADD, &n) != FALSE;
}

void ShowTip(const wchar_t* text) {
  NOTIFYICONDATAW n{};
  n.cbSize = sizeof(n);
  n.hWnd = gHost;
  n.uID = 1;
  n.uFlags = NIF_INFO;
  wcscpy(n.szInfoTitle, L"CaptureClean");
  wcsncpy(n.szInfo, text, 255);
  n.dwInfoFlags = NIIF_INFO | NIIF_NOSOUND;
  Shell_NotifyIconW(NIM_MODIFY, &n);
}

// Sin icono en la bandeja el globo se pierde: el aviso espera a que el icono exista.
static const wchar_t* gHotkeyWarning;
static void FlushHotkeyWarning() {
  if (gHotkeyWarning) {
    ShowTip(gHotkeyWarning);
    gHotkeyWarning = nullptr;
  }
}

static void RequestCapture(HWND h, CaptureMode mode) {
  if (FindWindowW(L"CaptureCleanOverlay", nullptr)) return;
  gPendingMode = mode;
  // Espera a que se cierre el menu de la bandeja y se suelte Impr Pant.
  KillTimer(h, TIMER_CAPTURE);
  SetTimer(h, TIMER_CAPTURE, 60, nullptr);
}

// Ventanas del propio shell: "ventana activa" nunca debe referirse a ellas.
static bool IsShellWindow(HWND w) {
  wchar_t cls[64]{};
  GetClassNameW(w, cls, 64);
  for (const wchar_t* name : {L"Shell_TrayWnd", L"Shell_SecondaryTrayWnd", L"NotifyIconOverflowWindow",
                              L"TopLevelWindowForOverflowXamlIsland", L"XamlExplorerHostIslandWindow",
                              L"Windows.UI.Core.CoreWindow", L"Progman", L"WorkerW"})
    if (wcscmp(cls, name) == 0) return true;
  return false;
}

static void CALLBACK OnForegroundChanged(HWINEVENTHOOK, DWORD, HWND w, LONG, LONG, DWORD, DWORD) {
  if (w && IsWindow(w) && !IsShellWindow(w)) gLastForeground = w;
}

static void ShowTrayMenu(HWND h) {
  POINT p;
  GetCursorPos(&p);
  HMENU q = CreatePopupMenu();
  AppendMenuW(q, MF_STRING | MF_GRAYED, 0, (std::wstring(L"CaptureClean ") + CC_VERSION_W).c_str());
  AppendMenuW(q, MF_SEPARATOR, 0, nullptr);
  AppendMenuW(q, MF_STRING, MENU_AREA, L"Capturar área\tImpr Pant");
  AppendMenuW(q, MF_STRING, MENU_WINDOW, L"Capturar ventana activa\tAlt+Impr Pant");
  AppendMenuW(q, MF_STRING, MENU_FULL, L"Capturar escritorio completo");
  AppendMenuW(q, MF_STRING | (gHasLastSelection ? 0 : MF_GRAYED), MENU_REPEAT, L"Repetir última zona\tMayús+Impr Pant");
  AppendMenuW(q, MF_SEPARATOR, 0, nullptr);
  AppendMenuW(q, MF_STRING, MENU_PREFERENCES, L"Preferencias…");
  AppendMenuW(q, MF_SEPARATOR, 0, nullptr);
  AppendMenuW(q, MF_STRING, MENU_EXIT, L"Salir");
  SetForegroundWindow(h);
  const int c = TrackPopupMenu(q, TPM_RETURNCMD | TPM_RIGHTBUTTON, p.x, p.y, 0, h, nullptr);
  PostMessageW(h, WM_NULL, 0, 0);
  DestroyMenu(q);
  switch (c) {
    case MENU_AREA: RequestCapture(h, CaptureMode::Area); break;
    case MENU_WINDOW: RequestCapture(h, CaptureMode::ActiveWindow); break;
    case MENU_FULL: RequestCapture(h, CaptureMode::FullDesktop); break;
    case MENU_REPEAT: RequestCapture(h, CaptureMode::RepeatLast); break;
    case MENU_PREFERENCES: ShowPreferences(); break;
    case MENU_EXIT: DestroyWindow(h); break;
  }
}

static LRESULT CALLBACK HostProc(HWND h, UINT m, WPARAM w, LPARAM l) {
  if (gTaskbarCreated && m == gTaskbarCreated) {
    if (AddTray(h)) FlushHotkeyWarning();
    return 0;
  }
  switch (m) {
    case WM_CREATE: {
      gHost = h;
      const bool tray = AddTray(h);
      if (!tray) SetTimer(h, TIMER_TRAY_RETRY, 2000, nullptr);  // al iniciar sesion el shell puede no estar listo
      const bool primary = RegisterHotKey(h, HOTKEY_AREA, MOD_NOREPEAT, VK_SNAPSHOT);
      const bool alt = RegisterHotKey(h, HOTKEY_AREA_ALT, MOD_CONTROL | MOD_SHIFT | MOD_NOREPEAT, 'S');
      RegisterHotKey(h, HOTKEY_WINDOW, MOD_ALT | MOD_NOREPEAT, VK_SNAPSHOT);
      RegisterHotKey(h, HOTKEY_REPEAT, MOD_SHIFT | MOD_NOREPEAT, VK_SNAPSHOT);
      gHotkeyBusy = !primary;
      if (!primary) gHotkeyWarning = alt ? L"Otro programa está usando Impr Pant. Usa Ctrl+Mayús+S o cierra ese programa."
                                         : L"Otro programa está usando Impr Pant y Ctrl+Mayús+S. Captura desde el menú de la bandeja o cierra ese programa.";
      if (tray) FlushHotkeyWarning();
      return 0;
    }
    case WM_HOTKEY:
      RequestCapture(h, w == HOTKEY_WINDOW ? CaptureMode::ActiveWindow : w == HOTKEY_REPEAT ? CaptureMode::RepeatLast : CaptureMode::Area);
      return 0;
    case WM_APP_CAPTURE:
      RequestCapture(h, CaptureMode::Area);
      return 0;
    case WM_TIMER:
      if (w == TIMER_CAPTURE) {
        KillTimer(h, TIMER_CAPTURE);
        OpenOverlay(gPendingMode, gLastForeground, gHasLastSelection ? &gLastSelection : nullptr);
      } else if (w == TIMER_TRAY_RETRY) {
        const bool ok = AddTray(h);
        if (ok) FlushHotkeyWarning();
        if (ok || ++gTrayRetries >= kTrayRetries) KillTimer(h, TIMER_TRAY_RETRY);
      }
      return 0;
    case WM_TRAY:
      if (l == WM_LBUTTONDBLCLK) RequestCapture(h, CaptureMode::Area);
      else if (l == WM_RBUTTONUP) ShowTrayMenu(h);
      return 0;
    case WM_CLOSE:
      DestroyWindow(h);
      return 0;
    case WM_ENDSESSION:
      return 0;
    case WM_DESTROY: {
      if (HWND overlay = FindWindowW(L"CaptureCleanOverlay", nullptr)) DestroyWindow(overlay);
      for (int id : {HOTKEY_AREA, HOTKEY_AREA_ALT, HOTKEY_WINDOW, HOTKEY_REPEAT}) UnregisterHotKey(h, id);
      NOTIFYICONDATAW n{};
      n.cbSize = sizeof(n);
      n.hWnd = h;
      n.uID = 1;
      Shell_NotifyIconW(NIM_DELETE, &n);
      PostQuitMessage(0);
      return 0;
    }
  }
  return DefWindowProcW(h, m, w, l);
}

int WINAPI wWinMain(HINSTANCE hi, HINSTANCE, LPWSTR cmd, int) {
  gInst = hi;
  SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
  const bool background = wcsstr(cmd, L"--background") != nullptr;
  const bool installed = wcsstr(cmd, L"--installed") != nullptr;

  HANDLE mutex = CreateMutexW(nullptr, TRUE, L"Local\\CaptureCleanV2.SingleInstance");
  if (GetLastError() == ERROR_ALREADY_EXISTS) {
    // Ya abierto: lanzar el acceso directo pide una captura. El host es una ventana oculta
    // normal (no HWND_MESSAGE) justamente para que FindWindow lo encuentre.
    if (HWND h = FindWindowW(L"CaptureCleanHost", nullptr)) {
      DWORD pid = 0;
      GetWindowThreadProcessId(h, &pid);
      AllowSetForegroundWindow(pid);
      if (!background && !installed) PostMessageW(h, WM_APP_CAPTURE, 0, 0);
    }
    return 0;
  }

  CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
  ULONG_PTR gdiplusToken = 0;
  Gdiplus::GdiplusStartupInput gdiplusInput;
  Gdiplus::GdiplusStartup(&gdiplusToken, &gdiplusInput, nullptr);
  LoadSettings();
  SyncAutoStart();

  gIcon = (HICON)LoadImageW(hi, MAKEINTRESOURCEW(101), IMAGE_ICON, GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON), LR_DEFAULTCOLOR);
  if (!gIcon) gIcon = LoadIcon(nullptr, IDI_APPLICATION);
  gTaskbarCreated = RegisterWindowMessageW(L"TaskbarCreated");

  WNDCLASSW c{};
  c.hInstance = hi;
  c.hIcon = gIcon;
  c.hCursor = LoadCursor(nullptr, IDC_ARROW);
  c.lpfnWndProc = HostProc;
  c.lpszClassName = L"CaptureCleanHost";
  RegisterClassW(&c);
  c.lpfnWndProc = OverlayProc;
  c.lpszClassName = L"CaptureCleanOverlay";
  c.style = CS_DBLCLKS;
  RegisterClassW(&c);

  // Oculta pero de nivel superior: una ventana HWND_MESSAGE no recibe TaskbarCreated,
  // no la ve FindWindow y no puede pasar a primer plano para cerrar bien el menu.
  CreateWindowExW(WS_EX_TOOLWINDOW, L"CaptureCleanHost", L"CaptureClean", WS_POPUP, 0, 0, 0, 0, nullptr, nullptr, hi, nullptr);
  HWINEVENTHOOK hook = SetWinEventHook(EVENT_SYSTEM_FOREGROUND, EVENT_SYSTEM_FOREGROUND, nullptr, OnForegroundChanged, 0, 0, WINEVENT_OUTOFCONTEXT | WINEVENT_SKIPOWNPROCESS);
  // La ventana activa al arrancar no genera evento: sin esto, Alt+Impr Pant no tendria ventana.
  if (HWND fg = GetForegroundWindow()) OnForegroundChanged(nullptr, 0, fg, 0, 0, 0, 0);
  if (!gHotkeyBusy) {
    if (installed) ShowTip(L"CaptureClean está activo y listo para capturar");
    else if (!background) ShowTip(L"CaptureClean está activo. Pulsa Impr Pant para capturar.");
  }

  MSG msg;
  while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
    if (PreferencesDialogMessage(&msg)) continue;
    TranslateMessage(&msg);
    DispatchMessageW(&msg);
  }

  if (hook) UnhookWinEvent(hook);
  Gdiplus::GdiplusShutdown(gdiplusToken);
  CoUninitialize();
  ReleaseMutex(mutex);
  CloseHandle(mutex);
  return 0;
}
