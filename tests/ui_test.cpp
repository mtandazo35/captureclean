// Prueba de extremo a extremo: maneja CaptureClean.exe con raton y teclado reales y comprueba
// el resultado en el portapapeles y en disco. Se ejecuta en un Windows desechable (CI), porque
// cambia preferencias y portapapeles.
// Uso: ui_test.exe <ruta a CaptureClean.exe> <carpeta para capturas>
#include "../src/logic.h"
#include "../src/ui_model.h"
#include <gdiplus.h>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

using namespace Gdiplus;
using std::wstring;

static int failures = 0;
static wstring gOut;
#define CHECK(cond) do { if (!(cond)) { printf("FALLA linea %d: %s\n", __LINE__, #cond); fflush(stdout); failures++; } else { printf("ok: %s\n", #cond); fflush(stdout); } } while (0)

static const wchar_t* kKey = L"Software\\CaptureClean";
static HANDLE gProcess;

static void SetDword(const wchar_t* name, DWORD v) { RegSetKeyValueW(HKEY_CURRENT_USER, kKey, name, REG_DWORD, &v, sizeof(v)); }
static void SetText(const wchar_t* name, const wstring& v) { RegSetKeyValueW(HKEY_CURRENT_USER, kKey, name, REG_SZ, v.c_str(), (DWORD)((v.size() + 1) * sizeof(wchar_t))); }

static bool WaitFor(bool (*cond)(), int ms) {
  for (int t = 0; t < ms; t += 25) {
    if (cond()) return true;
    Sleep(25);
  }
  return cond();
}
static bool HostUp() { return FindWindowW(L"CaptureCleanHost", nullptr) != nullptr; }
static bool OverlayUp() { return FindWindowW(L"CaptureCleanOverlay", nullptr) != nullptr; }
static bool OverlayGone() { return !OverlayUp(); }

static HANDLE Launch(const wstring& exe, const wstring& args) {
  wstring cmd = L"\"" + exe + L"\" " + args;
  STARTUPINFOW si{};
  si.cb = sizeof(si);
  PROCESS_INFORMATION pi{};
  if (!CreateProcessW(exe.c_str(), cmd.data(), nullptr, nullptr, FALSE, 0, nullptr, nullptr, &si, &pi)) return nullptr;
  CloseHandle(pi.hThread);
  return pi.hProcess;
}

static void MoveTo(int x, int y) { SetCursorPos(x, y); mouse_event(MOUSEEVENTF_MOVE, 0, 0, 0, 0); Sleep(40); }
static void Down() { mouse_event(MOUSEEVENTF_LEFTDOWN, 0, 0, 0, 0); Sleep(40); }
static void Up() { mouse_event(MOUSEEVENTF_LEFTUP, 0, 0, 0, 0); Sleep(40); }
static void Click(int x, int y) { MoveTo(x, y); Down(); Up(); Sleep(250); }  // pausa: sin doble clic accidental
static void DragFromTo(int x1, int y1, int x2, int y2) {
  MoveTo(x1, y1); Down();
  MoveTo((x1 + x2) / 2, (y1 + y2) / 2);
  MoveTo(x2, y2); Up();
  Sleep(250);
}
static void Key(WORD vk) { keybd_event((BYTE)vk, 0, 0, 0); Sleep(15); keybd_event((BYTE)vk, 0, KEYEVENTF_KEYUP, 0); Sleep(60); }
static void Chord(WORD mod, WORD vk) { keybd_event((BYTE)mod, 0, 0, 0); Sleep(15); Key(vk); keybd_event((BYTE)mod, 0, KEYEVENTF_KEYUP, 0); Sleep(120); }
static void TypeText(const wchar_t* text) {
  for (const wchar_t* c = text; *c; c++) {
    INPUT in[2]{};
    in[0].type = in[1].type = INPUT_KEYBOARD;
    in[0].ki.wScan = in[1].ki.wScan = *c;
    in[0].ki.dwFlags = KEYEVENTF_UNICODE;
    in[1].ki.dwFlags = KEYEVENTF_UNICODE | KEYEVENTF_KEYUP;
    SendInput(2, in, sizeof(INPUT));
    Sleep(30);
  }
  Sleep(100);
}

static bool EncoderFor(const wchar_t* mime, CLSID* out) {
  UINT n = 0, size = 0;
  GetImageEncodersSize(&n, &size);
  std::vector<BYTE> buffer(size);
  auto enc = (ImageCodecInfo*)buffer.data();
  GetImageEncoders(n, size, enc);
  for (UINT i = 0; i < n; i++) if (!wcscmp(enc[i].MimeType, mime)) { *out = enc[i].Clsid; return true; }
  return false;
}
static void SaveBitmap(Bitmap& b, const wstring& name) {
  CLSID png{};
  if (EncoderFor(L"image/png", &png)) b.Save((gOut + L"\\" + name).c_str(), &png, nullptr);
}
static void Screenshot(const wstring& name) {
  Sleep(150);
  const int w = GetSystemMetrics(SM_CXVIRTUALSCREEN), h = GetSystemMetrics(SM_CYVIRTUALSCREEN);
  HDC screen = GetDC(nullptr), mem = CreateCompatibleDC(screen);
  HBITMAP bmp = CreateCompatibleBitmap(screen, w, h);
  HGDIOBJ old = SelectObject(mem, bmp);
  BitBlt(mem, 0, 0, w, h, screen, GetSystemMetrics(SM_XVIRTUALSCREEN), GetSystemMetrics(SM_YVIRTUALSCREEN), SRCCOPY);
  SelectObject(mem, old);
  { Bitmap b(bmp, nullptr); SaveBitmap(b, name); }
  DeleteObject(bmp);
  DeleteDC(mem);
  ReleaseDC(nullptr, screen);
}

// Imagen del portapapeles como pixeles BGR de arriba abajo.
struct ClipImage {
  int w = 0, h = 0, stride = 0;
  std::vector<BYTE> px;
  void Get(int x, int y, int* r, int* g, int* b) const {
    const BYTE* p = px.data() + y * stride + x * 3;
    *b = p[0]; *g = p[1]; *r = p[2];
  }
  bool Near(int x, int y, int r, int g, int b, int tol) const {
    int cr, cg, cb;
    Get(x, y, &cr, &cg, &cb);
    return abs(cr - r) <= tol && abs(cg - g) <= tol && abs(cb - b) <= tol;
  }
};
static ClipImage ReadClipboard() {
  ClipImage out;
  for (int i = 0; i < 20 && !OpenClipboard(nullptr); i++) Sleep(50);
  HANDLE data = GetClipboardData(CF_DIB);
  if (data) {
    auto raw = (BYTE*)GlobalLock(data);
    auto bi = (BITMAPINFOHEADER*)raw;
    if (bi->biBitCount == 24 && bi->biHeight > 0) {
      out.w = bi->biWidth;
      out.h = bi->biHeight;
      out.stride = ((out.w * 24 + 31) / 32) * 4;
      out.px.resize(out.stride * out.h);
      for (int y = 0; y < out.h; y++)
        memcpy(out.px.data() + y * out.stride, raw + bi->biSize + (out.h - 1 - y) * out.stride, out.stride);
    }
    GlobalUnlock(data);
  }
  CloseClipboard();
  return out;
}
static void ClearClipboard() {
  if (OpenClipboard(nullptr)) { EmptyClipboard(); CloseClipboard(); }
}

static POINT ToolCenter(const RECT& sel, const RECT& monitor, Action a) {
  const auto bars = logic::PlaceToolbars(sel, monitor, kButtonSize, kToolbarGap, kToolCount, kBarCount);
  for (int i = 0; i < kToolCount; i++)
    if (kToolActions[i] == a) return {bars.tools.left + kButtonSize / 2, bars.tools.top + i * kButtonSize + kButtonSize / 2};
  for (int i = 0; i < kBarCount; i++)
    if (kBarActions[i] == a) return {bars.actions.left + i * kButtonSize + kButtonSize / 2, bars.actions.top + kButtonSize / 2};
  return {};
}

static std::vector<wstring> ListFiles(const wstring& folder) {
  std::vector<wstring> files;
  WIN32_FIND_DATAW f;
  HANDLE h = FindFirstFileW((folder + L"\\*").c_str(), &f);
  if (h == INVALID_HANDLE_VALUE) return files;
  do { if (!(f.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) files.push_back(folder + L"\\" + f.cFileName); } while (FindNextFileW(h, &f));
  FindClose(h);
  return files;
}
static std::vector<BYTE> ReadHead(const wstring& path, DWORD n) {
  std::vector<BYTE> out(n);
  HANDLE f = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
  DWORD got = 0;
  if (f != INVALID_HANDLE_VALUE) { ReadFile(f, out.data(), n, &got, nullptr); CloseHandle(f); }
  out.resize(got);
  return out;
}

static void QuitApp() {
  if (HWND h = FindWindowW(L"CaptureCleanHost", nullptr)) PostMessageW(h, WM_CLOSE, 0, 0);
  if (gProcess) {
    CHECK(WaitForSingleObject(gProcess, 5000) == WAIT_OBJECT_0);
    CloseHandle(gProcess);
    gProcess = nullptr;
  }
}

int wmain(int argc, wchar_t** argv) {
  if (argc < 3) { puts("uso: ui_test <exe> <carpeta>"); return 2; }
  SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
  const wstring exe = argv[1];
  gOut = argv[2];
  CreateDirectoryW(gOut.c_str(), nullptr);
  ULONG_PTR token;
  GdiplusStartupInput input;
  GdiplusStartup(&token, &input, nullptr);

  const int sw = GetSystemMetrics(SM_CXSCREEN), sh = GetSystemMetrics(SM_CYSCREEN);
  const RECT monitor{0, 0, sw, sh};
  printf("pantalla %dx%d\n", sw, sh);

  // Estado conocido: rojo puro, sin lupa, sin confirmaciones, trazo de 7 px.
  SetDword(L"Color", RGB(255, 0, 0));
  SetDword(L"ConfirmOnEscape", 0);
  SetDword(L"ShowMagnifier", 0);
  SetDword(L"AutoSave", 0);
  SetDword(L"Thickness", 7);
  SetDword(L"LastTool", 1);

  gProcess = Launch(exe, L"--background");
  CHECK(gProcess != nullptr);
  CHECK(WaitFor(HostUp, 5000));
  Sleep(500);

  // Con la app abierta, volver a lanzarla abre una captura (en 2.10.1 no hacia nada).
  HANDLE second = Launch(exe, L"");
  CHECK(WaitFor(OverlayUp, 3000));
  if (second) CloseHandle(second);
  Sleep(300);

  // Esc cierra una captura sin selección.
  Key(VK_ESCAPE);
  CHECK(WaitFor(OverlayGone, 2000));

  // Atajo de teclado.
  ClearClipboard();
  keybd_event(VK_CONTROL, 0, 0, 0); keybd_event(VK_SHIFT, 0, 0, 0); Key('S');
  keybd_event(VK_SHIFT, 0, KEYEVENTF_KEYUP, 0); keybd_event(VK_CONTROL, 0, KEYEVENTF_KEYUP, 0);
  CHECK(WaitFor(OverlayUp, 3000));
  Sleep(300);

  // Seleccionar 100,100 -> 500,400.
  DragFromTo(100, 100, 500, 400);
  CHECK(OverlayUp());
  Screenshot(L"1-seleccion.png");

  // Redimensionar con el tirador inferior derecho: queda 420x320.
  DragFromTo(500, 400, 520, 420);
  const RECT sel{100, 100, 520, 420};

  // Rectangulo rojo en coordenadas conocidas.
  POINT b = ToolCenter(sel, monitor, Action::Rectangle);
  Click(b.x, b.y);
  DragFromTo(150, 150, 300, 300);

  // Pixelar una zona.
  b = ToolCenter(sel, monitor, Action::Pixelate);
  Click(b.x, b.y);
  DragFromTo(340, 130, 500, 200);

  // Numero.
  b = ToolCenter(sel, monitor, Action::Number);
  Click(b.x, b.y);
  Click(450, 300);
  Click(490, 300);

  // Texto: una linea, con caracteres no ASCII.
  b = ToolCenter(sel, monitor, Action::Text);
  Click(b.x, b.y);
  Click(130, 340);
  TypeText(L"Hola ñandú");
  Screenshot(L"2-escribiendo.png");
  Key(VK_RETURN);

  // Linea con una forma encima de un texto: el orden de dibujo debe respetarse.
  b = ToolCenter(sel, monitor, Action::Line);
  Click(b.x, b.y);
  DragFromTo(120, 355, 330, 355);

  // Deshacer quita la linea y Rehacer la devuelve.
  Chord(VK_CONTROL, 'Z');
  Screenshot(L"3-tras-deshacer.png");
  Chord(VK_CONTROL, 'Y');
  Screenshot(L"4-anotada.png");

  Chord(VK_CONTROL, 'C');
  CHECK(WaitFor(OverlayGone, 3000));
  Sleep(200);

  ClipImage img = ReadClipboard();
  printf("portapapeles %dx%d\n", img.w, img.h);
  CHECK(img.w == 420 && img.h == 320);
  if (img.w == 420 && img.h == 320) {
    { // Volcado para revisar a ojo.
      Bitmap b2(img.w, img.h, img.stride, PixelFormat24bppRGB, img.px.data());
      SaveBitmap(b2, L"5-portapapeles.png");
    }
    CHECK(img.Near(50, 125, 255, 0, 0, 40));      // lado izquierdo del rectangulo (x=150)
    CHECK(img.Near(125, 50, 255, 0, 0, 40));      // lado superior (y=150)
    CHECK(!img.Near(125, 125, 255, 0, 0, 40));    // interior hueco
    CHECK(img.Near(339, 200, 255, 0, 0, 40));     // circulo del numero en 350,200; el centro lleva la cifra
    // La linea (rehecha) pasa sobre el texto.
    CHECK(img.Near(100, 255, 255, 0, 0, 40));
    // Pixelado: dentro de un bloque todos los pixeles son identicos.
    int r1, g1, b1, r2, g2, b2v;
    img.Get(244, 34, &r1, &g1, &b1);
    img.Get(246, 35, &r2, &g2, &b2v);
    CHECK(r1 == r2 && g1 == g2 && b1 == b2v);
  }

  // Ctrl+A y Ctrl+C: toda la pantalla, pixel exacto.
  ClearClipboard();
  keybd_event(VK_CONTROL, 0, 0, 0); keybd_event(VK_SHIFT, 0, 0, 0); Key('S');
  keybd_event(VK_SHIFT, 0, KEYEVENTF_KEYUP, 0); keybd_event(VK_CONTROL, 0, KEYEVENTF_KEYUP, 0);
  CHECK(WaitFor(OverlayUp, 3000));
  Sleep(300);
  Chord(VK_CONTROL, 'A');
  Sleep(200);
  Chord(VK_CONTROL, 'C');
  CHECK(WaitFor(OverlayGone, 3000));
  img = ReadClipboard();
  printf("completa %dx%d\n", img.w, img.h);
  CHECK(img.w == GetSystemMetrics(SM_CXVIRTUALSCREEN) && img.h == GetSystemMetrics(SM_CYVIRTUALSCREEN));

  // Un clic suelto sobre el escritorio selecciona el monitor entero o una ventana.
  keybd_event(VK_CONTROL, 0, 0, 0); keybd_event(VK_SHIFT, 0, 0, 0); Key('S');
  keybd_event(VK_SHIFT, 0, KEYEVENTF_KEYUP, 0); keybd_event(VK_CONTROL, 0, KEYEVENTF_KEYUP, 0);
  CHECK(WaitFor(OverlayUp, 3000));
  Sleep(300);
  MoveTo(sw / 2, sh / 2);
  Sleep(150);
  Screenshot(L"6-ventana-bajo-cursor.png");
  Down(); Up();
  Sleep(300);
  CHECK(OverlayUp());
  Screenshot(L"7-clic-selecciona.png");
  Key(VK_ESCAPE);
  CHECK(WaitFor(OverlayGone, 2000));

  // Explorer reiniciado: el aviso de barra de tareas no debe tumbar el programa.
  SendMessageTimeoutW(HWND_BROADCAST, RegisterWindowMessageW(L"TaskbarCreated"), 0, 0, SMTO_ABORTIFHUNG, 1000, nullptr);
  Sleep(300);
  CHECK(HostUp());
  QuitApp();

  // Guardado automatico, PNG y JPEG, sin dialogo y sin pisar archivos.
  wchar_t tempBase[MAX_PATH];
  GetTempPathW(MAX_PATH, tempBase);
  const wstring autoFolder = wstring(tempBase) + L"cc-autosave-" + std::to_wstring(GetTickCount());
  SetText(L"AutoSaveFolder", autoFolder);
  SetDword(L"AutoSave", 1);
  for (int format = 0; format < 2; format++) {
    SetDword(L"AutoSaveFormat", format);
    gProcess = Launch(exe, L"--background");
    CHECK(WaitFor(HostUp, 5000));
    Sleep(400);
    keybd_event(VK_CONTROL, 0, 0, 0); keybd_event(VK_SHIFT, 0, 0, 0); Key('S');
    keybd_event(VK_SHIFT, 0, KEYEVENTF_KEYUP, 0); keybd_event(VK_CONTROL, 0, KEYEVENTF_KEYUP, 0);
    CHECK(WaitFor(OverlayUp, 3000));
    Sleep(300);
    DragFromTo(200, 200, 400, 300);
    Chord(VK_CONTROL, 'S');
    CHECK(WaitFor(OverlayGone, 3000));
    Sleep(200);
    QuitApp();
  }
  const auto files = ListFiles(autoFolder);
  printf("archivos guardados: %d\n", (int)files.size());
  CHECK(files.size() == 2);
  bool sawPng = false, sawJpeg = false;
  for (const auto& f : files) {
    const auto head = ReadHead(f, 4);
    if (head.size() == 4 && head[0] == 0x89 && head[1] == 'P' && logic::HasExtension(f, L"png")) sawPng = true;
    if (head.size() == 4 && head[0] == 0xFF && head[1] == 0xD8 && logic::HasExtension(f, L"jpg")) sawJpeg = true;
    CHECK(!logic::HasExtension(f, L"tmp"));
  }
  CHECK(sawPng);
  CHECK(sawJpeg);

  GdiplusShutdown(token);
  printf(failures ? "%d FALLOS\n" : "OK (%d fallos)\n", failures);
  return failures ? 1 : 0;
}
