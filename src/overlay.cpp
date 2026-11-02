#include "app.h"
#include "imageio.h"
#include "logic.h"
#include "settings.h"
#include "ui_model.h"
#include <windowsx.h>
#include <commdlg.h>
#include <dwmapi.h>
#include <shellscalingapi.h>
#include <gdiplus.h>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

using namespace Gdiplus;
using logic::Grip;
using logic::Height;
using logic::Inside;
using logic::Normal;
using logic::Width;
using std::wstring;

// Todo lo dibujado es un objeto: se pinta en orden, se mueve, y Deshacer solo copia esta
// lista, nunca la imagen.
enum class Kind { Stroke, Highlight, Line, Arrow, Rectangle, Ellipse, Text, Number, Pixelate };

struct Annotation {
  Kind kind = Kind::Stroke;
  std::vector<POINT> pts;  // trazos: todos; formas: 2 esquinas; texto: esquina; numero: centro
  COLORREF color = 0;
  int width = 3;   // grosor; numero: diametro; texto: alto de letra; pixelar: tamano de bloque
  int fontStyle = 0;  // combinacion de FontStyleBold, FontStyleItalic y FontStyleUnderline
  int number = 0;
  wstring text;
  SIZE textSize{};
  std::shared_ptr<Bitmap> cache;  // pixelado ya calculado para cacheRect
  RECT cacheRect{};
};

enum class Phase { Selecting, Editing };
enum class Drag { None, Create, ResizeSelection, Draw, MoveObject };

struct UndoEntry {
  std::vector<Annotation> items;
  int nextNumber;
};

struct Overlay {
  int W = 0, H = 0;
  POINT origin{};  // esquina del escritorio virtual en coordenadas de pantalla
  HDC baseDC = nullptr, backDC = nullptr;
  HBITMAP baseBmp = nullptr, backBmp = nullptr;
  HGDIOBJ baseOld = nullptr, backOld = nullptr;
  uint32_t* bits = nullptr;
  std::unique_ptr<Bitmap> base;  // envuelve los pixeles de baseBmp, sin copiarlos

  Phase phase = Phase::Selecting;
  Drag drag = Drag::None;
  RECT sel{}, gripOrigin{};
  Grip grip = Grip::None;
  POINT start{}, current{}, last{};
  std::vector<RECT> targets;  // ventanas y monitores en orden Z, para seleccionar con un clic
  int hoverTarget = -1;

  Action tool = Action::Pencil, hover = Action::None;
  COLORREF color = 0;
  std::vector<Annotation> items;
  std::vector<UndoEntry> undo, redo;
  Annotation draft;
  int selected = -1;
  bool movedOnce = false, dirty = false;
  int nextNumber = 1, numberStart = 1;

  bool editing = false, caretOn = true;
  logic::TextEdit edit;
  POINT editAt{};
  int editIndex = -1;  // -1: texto nuevo; si no, el objeto que se esta reeditando
  int editStyle = 0;

  logic::BarLayout bars{};
  RECT formatBar{};
  double scale = 1;
  int btn = kButtonSize;
};

static constexpr UINT TIMER_CARET = 91;
static constexpr int kMaxUndo = 50;
static constexpr int kMinSelection = 12;
static constexpr int kClickSlop = 4;
static constexpr int kMagnifierSource = 15, kMagnifierZoom = 8;

static int Sc(const Overlay* s, double v) { return (int)std::lround(v * s->scale); }
static bool CtrlDown() { return (GetKeyState(VK_CONTROL) & 0x8000) && !(GetKeyState(VK_MENU) & 0x8000); }  // AltGr = Ctrl+Alt
static bool ShiftDown() { return (GetKeyState(VK_SHIFT) & 0x8000) != 0; }
static Color ToColor(COLORREF c, BYTE alpha = 255) { return Color(alpha, GetRValue(c), GetGValue(c), GetBValue(c)); }
static Gdiplus::Rect ToRect(const RECT& r) { return Gdiplus::Rect(r.left, r.top, Width(r), Height(r)); }
static void Repaint(HWND h) { InvalidateRect(h, nullptr, FALSE); }

static std::vector<wstring> SplitLines(const wstring& text) {
  std::vector<wstring> lines(1);
  for (wchar_t c : text) {
    if (c == L'\n') lines.emplace_back();
    else lines.back() += c;
  }
  return lines;
}

// --- Texto ---------------------------------------------------------------

static void TextFormat(StringFormat& f) {
  f.SetFormatFlags(f.GetFormatFlags() | StringFormatFlagsMeasureTrailingSpaces | StringFormatFlagsNoWrap | StringFormatFlagsNoClip);
}
static REAL LineWidth(Graphics& g, const wstring& line, Font& font) {
  if (line.empty()) return 0;
  StringFormat fmt(StringFormat::GenericTypographic());
  TextFormat(fmt);
  RectF out;
  g.MeasureString(line.c_str(), -1, &font, PointF(0, 0), &fmt, &out);
  return out.Width;
}
static void MeasureAnnotationText(Graphics& g, Annotation& a) {
  FontFamily family(L"Segoe UI");
  Font font(&family, (REAL)a.width, a.fontStyle, UnitPixel);
  const auto lines = SplitLines(a.text);
  REAL widest = 0;
  for (const auto& line : lines) widest = std::max(widest, LineWidth(g, line, font));
  // La cursiva sobresale por la derecha de lo que mide MeasureString.
  const LONG slant = (a.fontStyle & FontStyleItalic) ? a.width / 4 : 0;
  a.textSize = {(LONG)std::ceil(widest) + 1 + slant, (LONG)std::ceil(font.GetHeight(&g) * lines.size())};
}
static void DrawAnnotationText(Graphics& g, const Annotation& a) {
  FontFamily family(L"Segoe UI");
  Font font(&family, (REAL)a.width, a.fontStyle, UnitPixel);
  StringFormat fmt(StringFormat::GenericTypographic());
  TextFormat(fmt);
  SolidBrush brush(ToColor(a.color));
  const REAL lineHeight = font.GetHeight(&g);
  REAL y = (REAL)a.pts[0].y;
  for (const auto& line : SplitLines(a.text)) {
    if (!line.empty()) g.DrawString(line.c_str(), -1, &font, PointF((REAL)a.pts[0].x, y), &fmt, &brush);
    y += lineHeight;
  }
}

// --- Objetos -------------------------------------------------------------

static RECT Bounds(const Annotation& a) {
  if (a.pts.empty()) return {};
  if (a.kind == Kind::Text) return {a.pts[0].x, a.pts[0].y, a.pts[0].x + a.textSize.cx, a.pts[0].y + a.textSize.cy};
  if (a.kind == Kind::Number) {
    const int r = a.width / 2 + 1;
    return {a.pts[0].x - r, a.pts[0].y - r, a.pts[0].x + r, a.pts[0].y + r};
  }
  RECT b{a.pts[0].x, a.pts[0].y, a.pts[0].x, a.pts[0].y};
  for (const POINT& p : a.pts) {
    b.left = std::min(b.left, p.x);
    b.top = std::min(b.top, p.y);
    b.right = std::max(b.right, p.x);
    b.bottom = std::max(b.bottom, p.y);
  }
  if (a.kind == Kind::Pixelate) return b;
  const int m = a.kind == Kind::Arrow ? a.width * 3 : a.width / 2 + 1;
  InflateRect(&b, m, m);
  return b;
}

static void EnsurePixelated(const Overlay* s, Annotation& a) {
  RECT r = Normal(a.pts[0], a.pts[1]);
  r.left = std::max<LONG>(r.left, 0);
  r.top = std::max<LONG>(r.top, 0);
  r.right = std::min<LONG>(r.right, s->W);
  r.bottom = std::min<LONG>(r.bottom, s->H);
  const int w = Width(r), h = Height(r);
  if (w < 1 || h < 1) {
    a.cache.reset();
    return;
  }
  if (a.cache && EqualRect(&a.cacheRect, &r)) return;
  auto bmp = std::make_shared<Bitmap>(w, h, PixelFormat32bppRGB);
  Gdiplus::Rect all(0, 0, w, h);
  BitmapData data{};
  if (bmp->LockBits(&all, ImageLockModeWrite, PixelFormat32bppRGB, &data) != Ok) {
    a.cache.reset();
    return;
  }
  const int block = std::max(2, a.width);
  for (int by = 0; by < h; by += block) {
    for (int bx = 0; bx < w; bx += block) {
      const int ex = std::min(bx + block, w), ey = std::min(by + block, h);
      uint64_t sum[3] = {0, 0, 0};
      for (int y = by; y < ey; y++) {
        const uint32_t* row = s->bits + (size_t)(r.top + y) * s->W + r.left;
        for (int x = bx; x < ex; x++) {
          sum[0] += row[x] & 0xFF;
          sum[1] += (row[x] >> 8) & 0xFF;
          sum[2] += (row[x] >> 16) & 0xFF;
        }
      }
      const uint64_t n = (uint64_t)(ex - bx) * (ey - by);
      const uint32_t px = 0xFF000000u | (uint32_t)(sum[2] / n) << 16 | (uint32_t)(sum[1] / n) << 8 | (uint32_t)(sum[0] / n);
      for (int y = by; y < ey; y++) {
        auto out = (uint32_t*)((BYTE*)data.Scan0 + y * data.Stride);
        for (int x = bx; x < ex; x++) out[x] = px;
      }
    }
  }
  bmp->UnlockBits(&data);
  a.cache = bmp;
  a.cacheRect = r;
}

static void DrawArrowShape(Graphics& g, const Annotation& a) {
  const POINT p = a.pts[0], q = a.pts[1];
  const double angle = std::atan2((double)q.y - p.y, (double)q.x - p.x);
  const double head = std::max(14.0, a.width * 4.0);
  SolidBrush brush(ToColor(a.color));
  Pen pen(ToColor(a.color), (REAL)a.width);
  pen.SetStartCap(LineCapRound);
  const REAL ex = (REAL)(q.x - std::cos(angle) * head * .7), ey = (REAL)(q.y - std::sin(angle) * head * .7);
  g.DrawLine(&pen, (REAL)p.x, (REAL)p.y, ex, ey);
  PointF tri[3] = {PointF((REAL)q.x, (REAL)q.y),
                   PointF((REAL)(q.x - head * std::cos(angle - .5)), (REAL)(q.y - head * std::sin(angle - .5))),
                   PointF((REAL)(q.x - head * std::cos(angle + .5)), (REAL)(q.y - head * std::sin(angle + .5)))};
  g.FillPolygon(&brush, tri, 3);
}

static void DrawNumberBadge(Graphics& g, const Annotation& a) {
  const REAL d = (REAL)a.width, x = a.pts[0].x - d / 2, y = a.pts[0].y - d / 2;
  SolidBrush fill(ToColor(a.color));
  g.FillEllipse(&fill, x, y, d, d);
  const int luminance = (GetRValue(a.color) * 299 + GetGValue(a.color) * 587 + GetBValue(a.color) * 114) / 1000;
  SolidBrush ink(luminance > 170 ? Color(255, 20, 20, 20) : Color(255, 255, 255, 255));
  FontFamily family(L"Segoe UI");
  Font font(&family, d * .52f, FontStyleBold, UnitPixel);
  StringFormat fmt;
  fmt.SetAlignment(StringAlignmentCenter);
  fmt.SetLineAlignment(StringAlignmentCenter);
  g.DrawString(std::to_wstring(a.number).c_str(), -1, &font, RectF(x, y, d, d), &fmt, &ink);
}

static void RenderOne(Graphics& g, const Overlay* s, Annotation& a) {
  if (a.pts.empty()) return;
  switch (a.kind) {
    case Kind::Stroke:
    case Kind::Highlight: {
      // El trazo se pinta de una vez: asi el marcador translucido no oscurece las uniones.
      const Color c = ToColor(a.color, a.kind == Kind::Highlight ? 96 : 255);
      if (a.pts.size() < 2) {
        SolidBrush dot(c);
        const REAL d = (REAL)a.width;
        g.FillEllipse(&dot, a.pts[0].x - d / 2, a.pts[0].y - d / 2, d, d);
        return;
      }
      std::vector<PointF> line;
      line.reserve(a.pts.size());
      for (const POINT& p : a.pts) line.emplace_back((REAL)p.x, (REAL)p.y);
      GraphicsPath path;
      path.AddLines(line.data(), (INT)line.size());
      Pen pen(c, (REAL)a.width);
      pen.SetStartCap(LineCapRound);
      pen.SetEndCap(LineCapRound);
      pen.SetLineJoin(LineJoinRound);
      g.DrawPath(&pen, &path);
      return;
    }
    case Kind::Line: {
      Pen pen(ToColor(a.color), (REAL)a.width);
      pen.SetStartCap(LineCapRound);
      pen.SetEndCap(LineCapRound);
      g.DrawLine(&pen, (INT)a.pts[0].x, (INT)a.pts[0].y, (INT)a.pts[1].x, (INT)a.pts[1].y);
      return;
    }
    case Kind::Arrow: DrawArrowShape(g, a); return;
    case Kind::Rectangle:
    case Kind::Ellipse: {
      const RECT r = Normal(a.pts[0], a.pts[1]);
      Pen pen(ToColor(a.color), (REAL)a.width);
      pen.SetLineJoin(LineJoinRound);
      if (a.kind == Kind::Rectangle) g.DrawRectangle(&pen, ToRect(r));
      else g.DrawEllipse(&pen, ToRect(r));
      return;
    }
    case Kind::Text: DrawAnnotationText(g, a); return;
    case Kind::Number: DrawNumberBadge(g, a); return;
    case Kind::Pixelate: {
      EnsurePixelated(s, a);
      if (!a.cache) return;
      const auto state = g.Save();
      g.SetInterpolationMode(InterpolationModeNearestNeighbor);
      g.SetPixelOffsetMode(PixelOffsetModeHalf);
      g.DrawImage(a.cache.get(), ToRect(a.cacheRect), 0, 0, Width(a.cacheRect), Height(a.cacheRect), UnitPixel);
      g.Restore(state);
      return;
    }
  }
}

static void RenderItems(Graphics& g, Overlay* s) {
  for (int i = 0; i < (int)s->items.size(); i++)
    if (!(s->editing && i == s->editIndex)) RenderOne(g, s, s->items[i]);
}

static int HitItem(const Overlay* s, POINT p) {
  const double tol = 4 * s->scale;
  for (int i = (int)s->items.size() - 1; i >= 0; i--) {
    const Annotation& a = s->items[i];
    RECT b = Bounds(a);
    InflateRect(&b, (int)tol, (int)tol);
    if (!Inside(b, p)) continue;
    if (a.kind == Kind::Stroke || a.kind == Kind::Highlight || a.kind == Kind::Line || a.kind == Kind::Arrow) {
      if (a.pts.size() < 2) return i;
      for (size_t k = 1; k < a.pts.size(); k++)
        if (logic::SegmentDistance(p, a.pts[k - 1], a.pts[k]) <= a.width / 2.0 + tol) return i;
      continue;
    }
    return i;
  }
  return -1;
}

static int HitText(const Overlay* s, POINT p) {
  const int i = HitItem(s, p);
  return i >= 0 && s->items[i].kind == Kind::Text ? i : -1;
}

static void RecalcNextNumber(Overlay* s) {
  s->nextNumber = s->numberStart;
  for (auto it = s->items.rbegin(); it != s->items.rend(); ++it)
    if (it->kind == Kind::Number) {
      s->nextNumber = it->number + 1;
      break;
    }
}

static void PushUndo(Overlay* s) {
  if ((int)s->undo.size() >= kMaxUndo) s->undo.erase(s->undo.begin());
  s->undo.push_back({s->items, s->nextNumber});
  s->redo.clear();
  s->dirty = true;
}

static POINT ClampMove(const Overlay* s, const Annotation& a, int dx, int dy) {
  // Sin salir de la seleccion, salvo que el objeto ya sea mayor que ella. El 0 siempre cabe en
  // el rango: el margen del trazo puede dejar el objeto un poco fuera sin que deba saltar.
  const RECT b = Bounds(a);
  if (Width(b) <= Width(s->sel)) dx = std::clamp<int>(dx, std::min<int>(0, s->sel.left - b.left), std::max<int>(0, s->sel.right - b.right));
  if (Height(b) <= Height(s->sel)) dy = std::clamp<int>(dy, std::min<int>(0, s->sel.top - b.top), std::max<int>(0, s->sel.bottom - b.bottom));
  return {dx, dy};
}

static POINT MoveItem(const Overlay* s, Annotation& a, int dx, int dy) {
  const POINT d = ClampMove(s, a, dx, dy);
  for (POINT& p : a.pts) {
    p.x += d.x;
    p.y += d.y;
  }
  return d;
}

// Mete dentro de la seleccion un objeto recien creado, salvo que sea mayor que ella.
static void FitItem(const Overlay* s, Annotation& a) {
  const RECT b = Bounds(a);
  const int dx = Width(b) <= Width(s->sel) ? std::clamp<int>(0, s->sel.left - b.left, s->sel.right - b.right) : 0;
  const int dy = Height(b) <= Height(s->sel) ? std::clamp<int>(0, s->sel.top - b.top, s->sel.bottom - b.bottom) : 0;
  for (POINT& p : a.pts) {
    p.x += dx;
    p.y += dy;
  }
}

// --- Disposicion ---------------------------------------------------------

static RECT MonitorRectAt(const Overlay* s, POINT client, double* scale = nullptr) {
  const POINT screen{client.x + s->origin.x, client.y + s->origin.y};
  HMONITOR m = MonitorFromPoint(screen, MONITOR_DEFAULTTONEAREST);
  MONITORINFO info{};
  info.cbSize = sizeof(info);
  GetMonitorInfoW(m, &info);
  if (scale) {
    UINT dx = 96, dy = 96;
    if (SUCCEEDED(GetDpiForMonitor(m, MDT_EFFECTIVE_DPI, &dx, &dy))) *scale = dx / 96.0;
  }
  RECT r = info.rcMonitor;
  OffsetRect(&r, -s->origin.x, -s->origin.y);
  return r;
}

static void UpdateLayout(Overlay* s) {
  const POINT center{(s->sel.left + s->sel.right) / 2, (s->sel.top + s->sel.bottom) / 2};
  const RECT monitor = MonitorRectAt(s, center, &s->scale);
  s->btn = Sc(s, kButtonSize);
  s->bars = logic::PlaceToolbars(s->sel, monitor, s->btn, Sc(s, kToolbarGap), kToolCount, kBarCount);
  s->formatBar = logic::PlaceFormatBar(s->bars.tools, s->bars.actions, monitor, s->btn, Sc(s, kToolbarGap), kTextToolIndex, kFormatCount);
}

static bool FormatBarVisible(const Overlay* s) {
  return s->tool == Action::Text || (s->selected >= 0 && s->selected < (int)s->items.size() && s->items[s->selected].kind == Kind::Text);
}

static Action HitButton(const Overlay* s, POINT p) {
  if (FormatBarVisible(s) && Inside(s->formatBar, p)) return kFormatActions[std::clamp<int>((p.x - s->formatBar.left) / s->btn, 0, kFormatCount - 1)];
  if (Inside(s->bars.tools, p)) return kToolActions[std::clamp<int>((p.y - s->bars.tools.top) / s->btn, 0, kToolCount - 1)];
  if (Inside(s->bars.actions, p)) return kBarActions[std::clamp<int>((p.x - s->bars.actions.left) / s->btn, 0, kBarCount - 1)];
  return Action::None;
}

static RECT ButtonRect(const Overlay* s, Action a) {
  for (int i = 0; i < kFormatCount; i++)
    if (kFormatActions[i] == a) return {s->formatBar.left + i * s->btn, s->formatBar.top, s->formatBar.left + (i + 1) * s->btn, s->formatBar.bottom};
  for (int i = 0; i < kToolCount; i++)
    if (kToolActions[i] == a) return {s->bars.tools.left, s->bars.tools.top + i * s->btn, s->bars.tools.right, s->bars.tools.top + (i + 1) * s->btn};
  for (int i = 0; i < kBarCount; i++)
    if (kBarActions[i] == a) return {s->bars.actions.left + i * s->btn, s->bars.actions.top, s->bars.actions.left + (i + 1) * s->btn, s->bars.actions.bottom};
  return {};
}

static void EnterEditing(Overlay* s) {
  s->phase = Phase::Editing;
  s->drag = Drag::None;
  s->hoverTarget = -1;
  UpdateLayout(s);
}

// --- Exportar ------------------------------------------------------------

// Parte de los pixeles originales, sin escalar ni pasar por el dibujado de pantalla.
static std::unique_ptr<Bitmap> Crop(Overlay* s) {
  const int w = Width(s->sel), h = Height(s->sel);
  if (w < 1 || h < 1) return {};
  auto out = std::make_unique<Bitmap>(w, h, PixelFormat32bppARGB);
  Gdiplus::Rect all(0, 0, w, h);
  BitmapData data{};
  if (out->GetLastStatus() != Ok || out->LockBits(&all, ImageLockModeWrite, PixelFormat32bppARGB, &data) != Ok) return {};
  for (int y = 0; y < h; y++) {
    const uint32_t* src = s->bits + (size_t)(s->sel.top + y) * s->W + s->sel.left;
    auto dst = (uint32_t*)((BYTE*)data.Scan0 + y * data.Stride);
    for (int x = 0; x < w; x++) dst[x] = src[x] | 0xFF000000u;
  }
  out->UnlockBits(&data);
  Graphics g(out.get());
  g.SetSmoothingMode(SmoothingModeAntiAlias);
  g.SetTextRenderingHint(TextRenderingHintAntiAlias);
  g.TranslateTransform((REAL)-s->sel.left, (REAL)-s->sel.top);
  RenderItems(g, s);
  return out;
}

// --- Edicion de texto ----------------------------------------------------

static int StyleFromSettings() {
  return (gSettings.textBold ? FontStyleBold : 0) | (gSettings.textItalic ? FontStyleItalic : 0) | (gSettings.textUnderline ? FontStyleUnderline : 0);
}

static int FormatBit(Action a) {
  return a == Action::TextBold ? FontStyleBold : a == Action::TextItalic ? FontStyleItalic : FontStyleUnderline;
}

static int CurrentTextStyle(const Overlay* s) {
  if (s->editing) return s->editStyle;
  if (s->selected >= 0 && s->selected < (int)s->items.size() && s->items[s->selected].kind == Kind::Text) return s->items[s->selected].fontStyle;
  return StyleFromSettings();
}

static void StoreStyleInSettings(int style) {
  gSettings.textBold = (style & FontStyleBold) != 0;
  gSettings.textItalic = (style & FontStyleItalic) != 0;
  gSettings.textUnderline = (style & FontStyleUnderline) != 0;
  SaveSettings();
}

static Annotation* SelectedText(Overlay* s) {
  if (s->selected < 0 || s->selected >= (int)s->items.size() || s->items[s->selected].kind != Kind::Text) return nullptr;
  return &s->items[s->selected];
}

// Afecta al texto que se escribe o al seleccionado; sin ninguno, a los textos siguientes.
static void ToggleTextStyle(HWND h, Overlay* s, int bit) {
  int style;
  if (s->editing) {
    style = s->editStyle ^= bit;
    s->caretOn = true;
  } else if (SelectedText(s)) {
    PushUndo(s);
    Annotation* text = SelectedText(s);
    style = text->fontStyle ^= bit;
    Graphics g(s->backDC);
    g.SetTextRenderingHint(TextRenderingHintAntiAlias);
    MeasureAnnotationText(g, *text);
  } else {
    style = StyleFromSettings() ^ bit;
  }
  StoreStyleInSettings(style);
  Repaint(h);
}

static void BeginEdit(HWND h, Overlay* s, POINT at, int index) {
  s->editing = true;
  s->caretOn = true;
  s->editIndex = index;
  s->editStyle = index >= 0 ? s->items[index].fontStyle : StyleFromSettings();
  s->edit = {};
  s->editAt = at;
  s->selected = -1;
  if (index >= 0) {
    s->edit.text = s->items[index].text;
    s->edit.caret = s->edit.text.size();
    s->editAt = s->items[index].pts[0];
  }
  SetTimer(h, TIMER_CARET, GetCaretBlinkTime(), nullptr);
  Repaint(h);
}

static void EndEdit(HWND h, Overlay* s) {
  s->editing = false;
  s->editIndex = -1;
  KillTimer(h, TIMER_CARET);
}

static void CancelEdit(HWND h, Overlay* s) {
  EndEdit(h, s);
  Repaint(h);
}

static void CommitEdit(HWND h, Overlay* s) {
  if (!s->editing) return;
  const wstring text = s->edit.text;
  const int index = s->editIndex;
  const POINT at = s->editAt;
  EndEdit(h, s);
  if (index >= 0) {
    if (text == s->items[index].text && s->editStyle == s->items[index].fontStyle) {
      s->selected = index;
    } else {
      PushUndo(s);
      if (text.empty()) {
        s->items.erase(s->items.begin() + index);
      } else {
        s->items[index].text = text;
        s->items[index].fontStyle = s->editStyle;
        Graphics g(s->backDC);
        g.SetTextRenderingHint(TextRenderingHintAntiAlias);
        MeasureAnnotationText(g, s->items[index]);
        s->selected = index;
      }
    }
  } else if (!text.empty()) {
    PushUndo(s);
    Annotation a;
    a.kind = Kind::Text;
    a.pts = {at};
    a.color = s->color;
    a.width = Sc(s, gSettings.textSize);
    a.fontStyle = s->editStyle;
    a.text = text;
    Graphics g(s->backDC);
    g.SetTextRenderingHint(TextRenderingHintAntiAlias);
    MeasureAnnotationText(g, a);
    s->items.push_back(std::move(a));
    s->selected = (int)s->items.size() - 1;
  }
  Repaint(h);
}

static wstring ClipboardText(HWND h) {
  wstring out;
  if (!OpenClipboard(h)) return out;
  if (HANDLE data = GetClipboardData(CF_UNICODETEXT)) {
    if (auto text = (const wchar_t*)GlobalLock(data)) {
      for (const wchar_t* c = text; *c; c++)
        if (*c == L'\n' || *c == L'\t' || *c >= 0x20) out += *c == L'\t' ? L' ' : *c;
      GlobalUnlock(data);
    }
  }
  CloseClipboard();
  return out;
}

// --- Acciones ------------------------------------------------------------

static void Close(HWND h, Overlay* s) {
  if (s->phase == Phase::Editing) {
    RECT r = s->sel;
    OffsetRect(&r, s->origin.x, s->origin.y);
    RememberSelection(r);
  }
  DestroyWindow(h);
}

static bool SaveCurrent(HWND h, Overlay* s) {
  auto img = Crop(s);
  if (!img) {
    MessageBoxW(h, L"No hay memoria suficiente para generar la imagen.", L"CaptureClean", MB_OK | MB_ICONERROR);
    return false;
  }
  if (gSettings.autoSave) {
    if (!AutoSaveImage(img.get(), nullptr)) {
      MessageBoxW(h, L"No se pudo guardar automáticamente. Revisa la carpeta configurada en Preferencias.", L"CaptureClean", MB_OK | MB_ICONERROR);
      return false;
    }
    ShowTip(L"Captura guardada automáticamente");
    return true;
  }
  switch (SaveWithDialog(h, img.get())) {
    case SaveResult::Saved: ShowTip(L"Captura guardada"); return true;
    case SaveResult::Failed: MessageBoxW(h, L"No se pudo guardar la imagen. Comprueba que la carpeta exista y permita escribir.", L"CaptureClean", MB_OK | MB_ICONERROR); return false;
    case SaveResult::Cancelled: return false;
  }
  return false;
}

static void CopyCurrent(HWND h, Overlay* s) {
  auto img = Crop(s);
  if (!img) {
    MessageBoxW(h, L"No hay memoria suficiente para generar la imagen.", L"CaptureClean", MB_OK | MB_ICONERROR);
    return;
  }
  if (!CopyToClipboard(h, img.get())) {
    MessageBoxW(h, L"No se pudo copiar la imagen. Cierra cualquier gestor del portapapeles e intenta de nuevo.", L"CaptureClean", MB_OK | MB_ICONERROR);
    return;
  }
  if (gSettings.autoSave) {
    if (AutoSaveImage(img.get(), nullptr)) ShowTip(L"Captura copiada y guardada automáticamente");
    else MessageBoxW(h, L"La imagen se copió, pero no se pudo guardar automáticamente. Revisa la carpeta configurada en Preferencias.", L"CaptureClean", MB_OK | MB_ICONWARNING);
  } else {
    ShowTip(L"Captura copiada al portapapeles");
  }
  if (!IsWindow(h)) return;
  Close(h, s);
}

static void RequestClose(HWND h, Overlay* s) {
  if (s->phase == Phase::Editing && s->dirty && gSettings.confirmOnEscape) {
    const int answer = MessageBoxW(h, L"La captura tiene cambios sin guardar.\n¿Deseas guardarlos antes de cerrar?", L"CaptureClean", MB_YESNOCANCEL | MB_ICONQUESTION);
    if (!IsWindow(h)) return;  // el instalador puede cerrar el programa con la pregunta abierta
    if (answer == IDCANCEL) return;
    if (answer == IDYES && (!SaveCurrent(h, s) || !IsWindow(h))) return;
  }
  Close(h, s);
}

static void SetColor(HWND h, Overlay* s, COLORREF color) {
  s->color = color;
  gSettings.color = color;
  SaveSettings();
  if (s->selected >= 0 && s->selected < (int)s->items.size() && s->items[s->selected].kind != Kind::Pixelate && s->items[s->selected].color != color) {
    PushUndo(s);
    s->items[s->selected].color = color;
  }
  Repaint(h);
}

static POINT MenuPoint(const Overlay* s, Action a) {
  const RECT b = ButtonRect(s, a);
  return {b.left + s->origin.x, b.bottom + s->origin.y};
}

static void ShowColorMenu(HWND h, Overlay* s) {
  struct Option { const wchar_t* name; COLORREF color; };
  static const Option options[] = {{L"Rojo", RGB(235, 52, 62)}, {L"Azul", RGB(0, 120, 215)}, {L"Verde", RGB(24, 160, 88)},
                                   {L"Amarillo", RGB(255, 205, 0)}, {L"Negro", RGB(20, 20, 20)}, {L"Blanco", RGB(255, 255, 255)}};
  HMENU q = CreatePopupMenu();
  int id = 1;
  for (const auto& o : options) AppendMenuW(q, MF_STRING | (o.color == s->color ? MF_CHECKED : 0), id++, o.name);
  AppendMenuW(q, MF_SEPARATOR, 0, nullptr);
  AppendMenuW(q, MF_STRING, 100, L"Más colores…");
  const POINT p = MenuPoint(s, Action::Color);
  const int c = TrackPopupMenu(q, TPM_RETURNCMD, p.x, p.y, 0, h, nullptr);
  DestroyMenu(q);
  if (c >= 1 && c <= 6) {
    SetColor(h, s, options[c - 1].color);
  } else if (c == 100) {
    static COLORREF custom[16]{};
    CHOOSECOLORW d{};
    d.lStructSize = sizeof(d);
    d.hwndOwner = h;
    d.rgbResult = s->color;
    d.lpCustColors = custom;
    d.Flags = CC_FULLOPEN | CC_RGBINIT;
    if (ChooseColorW(&d)) SetColor(h, s, d.rgbResult);
  }
  Repaint(h);
}

static void ShowSizeMenu(HWND h, Overlay* s) {
  struct Choice { const wchar_t* name; int value; };
  const Choice* choices;
  int* setting;
  static const Choice strokes[] = {{L"Trazo fino (2 px)", 2}, {L"Trazo medio (4 px)", 4}, {L"Trazo grueso (7 px)", 7}};
  static const Choice marks[] = {{L"Resaltador fino (12 px)", 12}, {L"Resaltador medio (20 px)", 20}, {L"Resaltador ancho (32 px)", 32}};
  static const Choice texts[] = {{L"Texto pequeño (18 px)", 18}, {L"Texto mediano (24 px)", 24}, {L"Texto grande (32 px)", 32}};
  static const Choice pixels[] = {{L"Suave (6 px)", 6}, {L"Medio (12 px)", 12}, {L"Fuerte (20 px)", 20}};
  static const Choice numbers[] = {{L"Círculo pequeño (24 px)", 24}, {L"Círculo mediano (30 px)", 30}, {L"Círculo grande (40 px)", 40}};
  switch (s->tool) {
    case Action::Highlighter: choices = marks; setting = &gSettings.highlighterWidth; break;
    case Action::Text: choices = texts; setting = &gSettings.textSize; break;
    case Action::Pixelate: choices = pixels; setting = &gSettings.pixelBlock; break;
    case Action::Number: choices = numbers; setting = &gSettings.numberSize; break;
    default: choices = strokes; setting = &gSettings.thickness; break;
  }
  enum { ID_RESET1 = 10, ID_RESET5, ID_RESET10 };
  HMENU q = CreatePopupMenu();
  for (int i = 0; i < 3; i++) AppendMenuW(q, MF_STRING | (choices[i].value == *setting ? MF_CHECKED : 0), i + 1, choices[i].name);
  if (s->tool == Action::Number) {
    AppendMenuW(q, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(q, MF_STRING, ID_RESET1, L"Reiniciar numeración en 1");
    AppendMenuW(q, MF_STRING, ID_RESET5, L"Comenzar en 5");
    AppendMenuW(q, MF_STRING, ID_RESET10, L"Comenzar en 10");
  }
  const POINT p = MenuPoint(s, Action::SizeMenu);
  const int c = TrackPopupMenu(q, TPM_RETURNCMD, p.x, p.y, 0, h, nullptr);
  DestroyMenu(q);
  if (c >= 1 && c <= 3) *setting = choices[c - 1].value;
  else if (c >= ID_RESET1 && c <= ID_RESET10) s->numberStart = s->nextNumber = c == ID_RESET1 ? 1 : c == ID_RESET5 ? 5 : 10;
  if (c) SaveSettings();
  Repaint(h);
}

static void DoAction(HWND h, Overlay* s, Action a) {
  CommitEdit(h, s);
  if (IsTool(a)) {
    s->tool = a;
    s->selected = -1;
    gSettings.lastTool = (int)a - 1;
    SaveSettings();
    Repaint(h);
    return;
  }
  switch (a) {
    case Action::Color: ShowColorMenu(h, s); return;
    case Action::SizeMenu: ShowSizeMenu(h, s); return;
    case Action::Undo:
    case Action::Redo: {
      auto& from = a == Action::Undo ? s->undo : s->redo;
      auto& to = a == Action::Undo ? s->redo : s->undo;
      if (from.empty()) return;
      to.push_back({std::move(s->items), s->nextNumber});
      s->items = std::move(from.back().items);
      s->nextNumber = from.back().nextNumber;
      from.pop_back();
      s->selected = -1;
      s->dirty = true;
      Repaint(h);
      return;
    }
    case Action::Copy: CopyCurrent(h, s); return;
    case Action::Save: if (SaveCurrent(h, s) && IsWindow(h)) Close(h, s); return;
    case Action::Close: RequestClose(h, s); return;
    default: return;
  }
}

// --- Pintado -------------------------------------------------------------

static void RoundPath(GraphicsPath& p, RectF r, REAL radius) {
  const REAL d = radius * 2;
  p.StartFigure();
  p.AddArc(r.X, r.Y, d, d, 180, 90);
  p.AddArc(r.GetRight() - d, r.Y, d, d, 270, 90);
  p.AddArc(r.GetRight() - d, r.GetBottom() - d, d, d, 0, 90);
  p.AddArc(r.X, r.GetBottom() - d, d, d, 90, 90);
  p.CloseFigure();
}

// Iconos sobre un lienzo de 38x40, reescalados al tamano del boton.
static void DrawIcon(Graphics& g, Action a, RECT r, Color c, double uiScale) {
  const REAL scale = (REAL)(.72 * uiScale);
  const auto state = g.Save();
  const REAL ox = (REAL)r.left + (Width(r) - 38 * scale) / 2, oy = (REAL)r.top + (Height(r) - 40 * scale) / 2;
  Matrix transform(scale, 0, 0, scale, ox, oy);
  g.SetTransform(&transform);
  Pen p(c, 2.2f);
  p.SetStartCap(LineCapRound);
  p.SetEndCap(LineCapRound);
  p.SetLineJoin(LineJoinRound);
  SolidBrush b(c);
  switch (a) {
    case Action::Select: {
      PointF pts[] = {PointF(12, 8), PointF(12, 30), PointF(18, 24), PointF(22, 32), PointF(26, 30), PointF(22, 22), PointF(29, 22)};
      g.FillPolygon(&b, pts, 7);
      break;
    }
    case Action::Pencil: {
      PointF pts[] = {PointF(10, 29), PointF(13, 22), PointF(25, 10), PointF(29, 14), PointF(17, 26)};
      g.FillPolygon(&b, pts, 5);
      break;
    }
    case Action::Line: g.DrawLine(&p, 10, 29, 28, 11); break;
    case Action::Arrow: g.DrawLine(&p, 9, 29, 28, 11); g.DrawLine(&p, 28, 11, 19, 12); g.DrawLine(&p, 28, 11, 27, 20); break;
    case Action::Rectangle: g.DrawRectangle(&p, 10.0f, 12.0f, 18.0f, 16.0f); break;
    case Action::Ellipse: g.DrawEllipse(&p, 10.0f, 11.0f, 18.0f, 17.0f); break;
    case Action::Text: {
      FontFamily family(L"Segoe UI");
      Font f(&family, 21, FontStyleRegular, UnitPixel);
      StringFormat sf;
      sf.SetAlignment(StringAlignmentCenter);
      sf.SetLineAlignment(StringAlignmentCenter);
      g.DrawString(L"T", -1, &f, RectF(0, 0, 38, 40), &sf, &b);
      break;
    }
    case Action::Highlighter: {
      PointF pts[] = {PointF(11, 27), PointF(14, 19), PointF(24, 9), PointF(30, 15), PointF(20, 25)};
      g.FillPolygon(&b, pts, 5);
      g.DrawLine(&p, 10, 31, 28, 31);
      break;
    }
    case Action::Pixelate:
      for (int y = 0; y < 4; y++)
        for (int x = 0; x < 4; x++)
          if ((x + y) % 2 == 0) g.FillRectangle(&b, 11 + x * 4.5f, 11 + y * 4.5f, 4.5f, 4.5f);
      g.DrawRectangle(&p, 11.0f, 11.0f, 18.0f, 18.0f);
      break;
    case Action::Number: {
      g.FillEllipse(&b, 9.0f, 10.0f, 20.0f, 20.0f);
      FontFamily family(L"Segoe UI");
      Font f(&family, 14, FontStyleBold, UnitPixel);
      StringFormat sf;
      sf.SetAlignment(StringAlignmentCenter);
      sf.SetLineAlignment(StringAlignmentCenter);
      SolidBrush white(Color(255, 255, 255, 255));
      g.DrawString(L"1", -1, &f, RectF(9, 10, 20, 20), &sf, &white);
      break;
    }
    case Action::Color: {
      g.FillEllipse(&b, 11.0f, 12.0f, 17.0f, 17.0f);
      Pen ring(Color(150, 30, 30, 30), 1.1f);
      g.DrawEllipse(&ring, 11.0f, 12.0f, 17.0f, 17.0f);
      break;
    }
    case Action::SizeMenu: {
      Pen thin(c, 1.2f), mid(c, 2.4f), thick(c, 4.2f);
      g.DrawLine(&thin, 10, 13, 28, 13);
      g.DrawLine(&mid, 10, 20, 28, 20);
      g.DrawLine(&thick, 10, 28, 28, 28);
      break;
    }
    case Action::Undo:
      g.DrawArc(&p, 10.0f, 11.0f, 19.0f, 18.0f, 205.0f, 250.0f);
      g.DrawLine(&p, 10, 20, 10, 13);
      g.DrawLine(&p, 10, 20, 17, 19);
      break;
    case Action::Redo:
      g.DrawArc(&p, 9.0f, 11.0f, 19.0f, 18.0f, -95.0f, 250.0f);
      g.DrawLine(&p, 28, 20, 28, 13);
      g.DrawLine(&p, 28, 20, 21, 19);
      break;
    case Action::Copy:
      g.DrawRectangle(&p, 13.0f, 10.0f, 14.0f, 16.0f);
      g.DrawLine(&p, 9, 14, 9, 30);
      g.DrawLine(&p, 9, 30, 23, 30);
      break;
    case Action::Save: {
      g.FillRectangle(&b, 10.0f, 9.0f, 19.0f, 22.0f);
      SolidBrush paper(Color(235, 249, 247, 238));
      g.FillRectangle(&paper, 14, 10, 10, 7);
      g.FillRectangle(&paper, 14, 23, 11, 6);
      break;
    }
    case Action::Close: g.DrawLine(&p, 12, 13, 26, 27); g.DrawLine(&p, 26, 13, 12, 27); break;
    case Action::TextBold:
    case Action::TextItalic:
    case Action::TextUnderline: {
      FontFamily family(L"Segoe UI");
      Font f(&family, 21, FormatBit(a), UnitPixel);
      StringFormat sf;
      sf.SetAlignment(StringAlignmentCenter);
      sf.SetLineAlignment(StringAlignmentCenter);
      g.DrawString(a == Action::TextBold ? L"B" : a == Action::TextItalic ? L"I" : L"U", -1, &f, RectF(0, 0, 38, 40), &sf, &b);
      break;
    }
    default: break;
  }
  g.Restore(state);
}

static const wchar_t* ActionName(Action a) {
  switch (a) {
    case Action::Select: return L"Seleccionar y mover";
    case Action::Pencil: return L"Lápiz";
    case Action::Line: return L"Línea";
    case Action::Arrow: return L"Flecha";
    case Action::Rectangle: return L"Rectángulo";
    case Action::Ellipse: return L"Elipse";
    case Action::Text: return L"Texto";
    case Action::Highlighter: return L"Resaltador";
    case Action::Pixelate: return L"Pixelar";
    case Action::Number: return L"Numerar";
    case Action::Color: return L"Colores rápidos";
    case Action::SizeMenu: return L"Tamaño y grosor";
    case Action::Undo: return L"Deshacer (Ctrl+Z)";
    case Action::Redo: return L"Rehacer (Ctrl+Y)";
    case Action::Copy: return L"Copiar (Ctrl+C)";
    case Action::Save: return gSettings.autoSave ? L"Guardar automático (Ctrl+S)" : L"Guardar (Ctrl+S)";
    case Action::Close: return L"Cerrar (Esc)";
    case Action::TextBold: return L"Negrita (Ctrl+B)";
    case Action::TextItalic: return L"Cursiva (Ctrl+I)";
    case Action::TextUnderline: return L"Subrayado (Ctrl+U)";
    default: return L"";
  }
}

static void PaintBar(Graphics& g, const Overlay* s, RECT bar, const Action* actions, int count, bool vertical) {
  const REAL radius = (REAL)Sc(s, 6);
  RectF box((REAL)bar.left, (REAL)bar.top, (REAL)Width(bar), (REAL)Height(bar));
  RectF shadowBox(box.X + 1, box.Y + 3, box.Width, box.Height);
  GraphicsPath shadowPath, boxPath;
  RoundPath(shadowPath, shadowBox, radius);
  RoundPath(boxPath, box, radius);
  SolidBrush shadow(Color(42, 0, 0, 0)), background(Color(246, 249, 247, 238));
  Pen edge(Color(170, 208, 207, 197), 1);
  g.FillPath(&shadow, &shadowPath);
  g.FillPath(&background, &boxPath);
  g.DrawPath(&edge, &boxPath);
  for (int i = 0; i < count; i++) {
    const RECT r = vertical ? RECT{bar.left, bar.top + i * s->btn, bar.right, bar.top + (i + 1) * s->btn}
                            : RECT{bar.left + i * s->btn, bar.top, bar.left + (i + 1) * s->btn, bar.bottom};
    const Action a = actions[i];
    const bool active = IsFormat(a) ? (CurrentTextStyle(s) & FormatBit(a)) != 0 : s->tool == a;
    const bool hot = s->hover == a;
    const bool disabled = (a == Action::Undo && s->undo.empty()) || (a == Action::Redo && s->redo.empty());
    if (active || (hot && !disabled)) {
      const REAL inset = (REAL)Sc(s, 3);
      RectF hi(r.left + inset, r.top + inset, s->btn - inset * 2, s->btn - inset * 2);
      GraphicsPath hp;
      RoundPath(hp, hi, (REAL)Sc(s, 5));
      SolidBrush b(active ? Color(54, 0, 154, 221) : Color(24, 60, 62, 58));
      g.FillPath(&b, &hp);
    }
    Color ink = Color(238, 72, 74, 69);
    if (a == Action::Color) ink = ToColor(s->color);
    else if (disabled) ink = Color(90, 72, 74, 69);
    else if (hot && a == Action::Close) ink = Color(255, 194, 44, 51);
    else if (active) ink = Color(255, 0, 135, 205);
    DrawIcon(g, a, r, ink, s->scale);
  }
}

static void PaintLabel(Graphics& g, const Overlay* s, const wstring& text, RectF box) {
  GraphicsPath path;
  RoundPath(path, box, (REAL)Sc(s, 6));
  SolidBrush background(Color(210, 25, 29, 33)), white(Color(245, 255, 255, 255));
  g.FillPath(&background, &path);
  FontFamily family(L"Segoe UI");
  Font font(&family, (REAL)Sc(s, 12), FontStyleRegular, UnitPixel);
  StringFormat sf;
  sf.SetAlignment(StringAlignmentCenter);
  sf.SetLineAlignment(StringAlignmentCenter);
  g.DrawString(text.c_str(), -1, &font, box, &sf, &white);
}

static void PaintTooltip(Graphics& g, const Overlay* s) {
  if (s->hover == Action::None) return;
  const wstring name = ActionName(s->hover);
  const RECT b = ButtonRect(s, s->hover);
  const RECT monitor = MonitorRectAt(s, {b.left, b.top});
  const REAL w = (REAL)Sc(s, 12 + 6.2 * name.size()), height = (REAL)Sc(s, 25), gap = (REAL)Sc(s, 6);
  REAL x, y;
  if (Inside(s->bars.tools, {b.left, b.top})) {
    x = b.right + gap;
    if (x + w > monitor.right) x = b.left - gap - w;
    y = b.top + (Height(b) - height) / 2;
  } else {
    x = b.left + (Width(b) - w) / 2;
    y = b.top - height - gap;
    if (y < monitor.top) y = b.bottom + gap;
  }
  x = std::clamp(x, (REAL)monitor.left + 2, std::max((REAL)monitor.left + 2, monitor.right - w - 2));
  PaintLabel(g, s, name, RectF(x, y, w, height));
}

static void PaintMagnifier(Graphics& g, const Overlay* s) {
  const POINT p = s->current;
  const RECT monitor = MonitorRectAt(s, p);
  const int side = kMagnifierSource * kMagnifierZoom * std::max(1, (int)std::lround(s->scale));
  const int label = Sc(s, 22), gap = Sc(s, 20);
  int x = p.x + gap, y = p.y + gap;
  if (x + side > monitor.right) x = p.x - gap - side;
  if (y + side + label > monitor.bottom) y = p.y - gap - side - label;
  const int half = kMagnifierSource / 2;
  const auto state = g.Save();
  g.SetInterpolationMode(InterpolationModeNearestNeighbor);
  g.SetPixelOffsetMode(PixelOffsetModeHalf);
  g.SetSmoothingMode(SmoothingModeNone);
  const Gdiplus::Rect dest(x, y, side, side);
  SolidBrush black(Color(255, 0, 0, 0));
  g.FillRectangle(&black, dest);
  g.DrawImage(s->base.get(), dest, p.x - half, p.y - half, kMagnifierSource, kMagnifierSource, UnitPixel);
  const int cell = side / kMagnifierSource;
  Pen cross(Color(190, 0, 166, 235), 1);
  g.DrawRectangle(&cross, x + half * cell, y + half * cell, cell, cell);
  Pen frame(Color(230, 255, 255, 255), 1);
  g.DrawRectangle(&frame, dest);
  g.Restore(state);
  const uint32_t px = p.x >= 0 && p.y >= 0 && p.x < s->W && p.y < s->H ? s->bits[(size_t)p.y * s->W + p.x] : 0;
  wchar_t hex[8];
  static const wchar_t* digits = L"0123456789ABCDEF";
  hex[0] = L'#';
  for (int i = 0; i < 6; i++) hex[1 + i] = digits[(px >> (20 - i * 4)) & 0xF];
  hex[7] = 0;
  PaintLabel(g, s, std::to_wstring(p.x + s->origin.x) + L", " + std::to_wstring(p.y + s->origin.y) + L"   " + hex, RectF((REAL)x, (REAL)(y + side + 2), (REAL)side, (REAL)label - 2));
}

static void PaintCaret(Graphics& g, const Overlay* s) {
  FontFamily family(L"Segoe UI");
  const int px = Sc(s, gSettings.textSize);
  const Annotation* source = s->editIndex >= 0 ? &s->items[s->editIndex] : nullptr;
  Font font(&family, (REAL)(source ? source->width : px), s->editStyle, UnitPixel);
  const wstring before = s->edit.text.substr(0, s->edit.caret);
  const auto lines = SplitLines(before);
  const REAL h = font.GetHeight(&g);
  const REAL x = s->editAt.x + LineWidth(g, lines.back(), font), y = s->editAt.y + h * (lines.size() - 1);
  Pen caret(ToColor(source ? source->color : s->color), std::max(1.5f, (REAL)s->scale * 1.5f));
  g.DrawLine(&caret, x, y, x, y + h);
}

static RECT ActiveRect(const Overlay* s) {
  if (s->phase == Phase::Editing) return s->sel;
  if (s->drag == Drag::Create) return Normal(s->start, s->current);
  if (s->hoverTarget >= 0) return s->targets[s->hoverTarget];
  return {};
}

static void PaintOverlay(Overlay* s) {
  HDC dc = s->backDC;
  BitBlt(dc, 0, 0, s->W, s->H, s->baseDC, 0, 0, SRCCOPY);
  Graphics g(dc);
  g.SetSmoothingMode(SmoothingModeAntiAlias);
  g.SetTextRenderingHint(TextRenderingHintAntiAlias);
  const RECT r = ActiveRect(s);
  const bool hasRect = Width(r) > 0 && Height(r) > 0;

  SolidBrush dim(Color(70, 13, 17, 20));
  g.FillRectangle(&dim, 0, 0, s->W, std::max<int>(0, r.top));
  g.FillRectangle(&dim, 0, (INT)r.bottom, s->W, std::max<int>(0, s->H - r.bottom));
  g.FillRectangle(&dim, 0, (INT)r.top, std::max<int>(0, r.left), Height(r));
  g.FillRectangle(&dim, (INT)r.right, (INT)r.top, std::max<int>(0, s->W - r.right), Height(r));

  if (s->phase == Phase::Editing) {
    const auto state = g.Save();
    g.SetClip(ToRect(s->sel));
    RenderItems(g, s);
    if (s->drag == Drag::Draw) RenderOne(g, s, s->draft);
    if (s->editing) {
      Annotation preview;
      preview.kind = Kind::Text;
      preview.pts = {s->editAt};
      const Annotation* source = s->editIndex >= 0 ? &s->items[s->editIndex] : nullptr;
      preview.color = source ? source->color : s->color;
      preview.width = source ? source->width : Sc(s, gSettings.textSize);
      preview.fontStyle = s->editStyle;
      preview.text = s->edit.text;
      DrawAnnotationText(g, preview);
      if (s->caretOn) PaintCaret(g, s);
    }
    if (s->selected >= 0 && s->selected < (int)s->items.size() && !s->editing) {
      RECT b = Bounds(s->items[s->selected]);
      InflateRect(&b, Sc(s, 3), Sc(s, 3));
      Pen dash(Color(220, 0, 153, 230), 1.2f);
      dash.SetDashStyle(DashStyleDash);
      g.DrawRectangle(&dash, ToRect(b));
    }
    g.Restore(state);
  }

  if (hasRect) {
    Pen border(Color(200, 0, 166, 235), (REAL)std::max(1, Sc(s, 1)));
    g.DrawRectangle(&border, (INT)r.left, (INT)r.top, Width(r) - 1, Height(r) - 1);
  }
  if (s->phase == Phase::Editing) {
    const int k = Sc(s, 3);
    const int xs[3] = {(int)r.left, (int)(r.left + r.right) / 2, (int)r.right}, ys[3] = {(int)r.top, (int)(r.top + r.bottom) / 2, (int)r.bottom};
    SolidBrush white(Color(255, 255, 255, 255));
    Pen handle(Color(255, 0, 166, 235), 1);
    for (int yi = 0; yi < 3; yi++)
      for (int xi = 0; xi < 3; xi++) {
        if (xi == 1 && yi == 1) continue;
        g.FillRectangle(&white, xs[xi] - k, ys[yi] - k, k * 2, k * 2);
        g.DrawRectangle(&handle, xs[xi] - k, ys[yi] - k, k * 2, k * 2);
      }
  }
  const bool showSize = hasRect && (s->drag == Drag::Create || s->drag == Drag::ResizeSelection || (s->phase == Phase::Selecting && s->hoverTarget >= 0));
  if (showSize) {
    const REAL w = (REAL)Sc(s, 104), lh = (REAL)Sc(s, 23);
    const RECT mon = MonitorRectAt(s, {r.left, r.top});
    PaintLabel(g, s, std::to_wstring(Width(r)) + L" × " + std::to_wstring(Height(r)), RectF((REAL)r.left, std::max<REAL>((REAL)mon.top + 3, r.top - lh - 4), w, lh));
  }
  if (s->phase == Phase::Editing) {
    PaintBar(g, s, s->bars.tools, kToolActions, kToolCount, true);
    PaintBar(g, s, s->bars.actions, kBarActions, kBarCount, false);
    if (FormatBarVisible(s)) PaintBar(g, s, s->formatBar, kFormatActions, kFormatCount, false);
    PaintTooltip(g, s);
  } else if (gSettings.showMagnifier) {
    PaintMagnifier(g, s);
  }
}

// --- Raton ---------------------------------------------------------------

static void BeginDraw(HWND h, Overlay* s, POINT p) {
  Annotation a;
  a.color = s->color;
  a.pts = {p};
  a.width = Sc(s, gSettings.thickness);
  switch (s->tool) {
    case Action::Pencil: a.kind = Kind::Stroke; break;
    case Action::Highlighter: a.kind = Kind::Highlight; a.width = Sc(s, gSettings.highlighterWidth); break;
    case Action::Line: a.kind = Kind::Line; a.pts.push_back(p); break;
    case Action::Arrow: a.kind = Kind::Arrow; a.pts.push_back(p); break;
    case Action::Rectangle: a.kind = Kind::Rectangle; a.pts.push_back(p); break;
    case Action::Ellipse: a.kind = Kind::Ellipse; a.pts.push_back(p); break;
    case Action::Pixelate:
      a.kind = Kind::Pixelate;
      a.width = Sc(s, gSettings.pixelBlock);
      a.pts.push_back(p);
      // El rectangulo tiene fin exclusivo: empezar pegado al borde debe cubrir tambien ese ultimo pixel.
      if (p.x >= s->sel.right - 1) a.pts[0].x = s->sel.right;
      if (p.y >= s->sel.bottom - 1) a.pts[0].y = s->sel.bottom;
      break;
    default: return;
  }
  s->draft = std::move(a);
  s->drag = Drag::Draw;
  SetCapture(h);
}

static void OnLeftDown(HWND h, Overlay* s, POINT p) {
  s->current = p;
  if (s->phase == Phase::Selecting) {
    s->drag = Drag::Create;
    s->start = p;
    SetCapture(h);
    return;
  }
  // Antes de confirmar el texto: dar formato no debe interrumpir lo que se esta escribiendo.
  const Action format = HitButton(s, p);
  if (IsFormat(format)) {
    ToggleTextStyle(h, s, FormatBit(format));
    return;
  }
  const bool wasEditing = s->editing;
  CommitEdit(h, s);
  const Action button = HitButton(s, p);
  if (button != Action::None) {
    DoAction(h, s, button);
    return;
  }
  const bool selectTool = s->tool == Action::Select;
  const int tol = Sc(s, 6);
  const int item = selectTool ? HitItem(s, p) : s->tool == Action::Text ? HitText(s, p) : -1;
  const Grip grip = logic::HitGrip(s->sel, p, tol, selectTool, selectTool && item < 0);
  if (item >= 0) {
    s->selected = item;
    s->drag = Drag::MoveObject;
    s->movedOnce = false;
    s->last = p;
    SetCapture(h);
    Repaint(h);
    return;
  }
  if (grip != Grip::None) {
    s->selected = -1;
    s->grip = grip;
    s->gripOrigin = s->sel;
    s->start = p;
    s->drag = Drag::ResizeSelection;
    SetCapture(h);
    Repaint(h);
    return;
  }
  s->selected = -1;
  if (!Inside(s->sel, p)) {
    Repaint(h);
    return;
  }
  if (s->tool == Action::Text) {
    // Un clic fuera solo confirma el texto en curso; el siguiente empieza otro.
    if (!wasEditing) BeginEdit(h, s, p, -1);
    return;
  }
  if (s->tool == Action::Number) {
    PushUndo(s);
    Annotation a;
    a.kind = Kind::Number;
    a.pts = {p};
    a.color = s->color;
    a.width = Sc(s, gSettings.numberSize);
    a.number = s->nextNumber++;
    FitItem(s, a);
    s->items.push_back(std::move(a));
    Repaint(h);
    return;
  }
  BeginDraw(h, s, p);
  Repaint(h);
}

static void OnMouseMove(HWND h, Overlay* s, POINT p) {
  s->current = p;
  switch (s->drag) {
    case Drag::Create:
      MonitorRectAt(s, p, &s->scale);
      Repaint(h);
      return;
    case Drag::ResizeSelection:
      s->sel = logic::ApplyGrip(s->gripOrigin, s->grip, p.x - s->start.x, p.y - s->start.y, RECT{0, 0, s->W, s->H}, kMinSelection);
      UpdateLayout(s);
      Repaint(h);
      return;
    case Drag::MoveObject:
      if ((p.x != s->last.x || p.y != s->last.y) && s->selected >= 0 && s->selected < (int)s->items.size()) {
        const POINT d = ClampMove(s, s->items[s->selected], p.x - s->last.x, p.y - s->last.y);
        if (d.x || d.y) {
          if (!s->movedOnce) {
            PushUndo(s);  // solo si de verdad se movio: un clic no gasta un paso de Deshacer
            s->movedOnce = true;
          }
          MoveItem(s, s->items[s->selected], d.x, d.y);
          s->last.x += d.x;
          s->last.y += d.y;
          Repaint(h);
        }
      }
      return;
    case Drag::Draw: {
      POINT q = logic::ClampPoint(p, s->sel);
      if (s->draft.kind == Kind::Pixelate) {
        // Pixelar usa fin exclusivo: pegado al borde debe cubrir tambien la ultima fila y columna.
        if (p.x >= s->sel.right - 1) q.x = s->sel.right;
        if (p.y >= s->sel.bottom - 1) q.y = s->sel.bottom;
      }
      if (s->draft.kind == Kind::Stroke || s->draft.kind == Kind::Highlight) s->draft.pts.push_back(q);
      else s->draft.pts[1] = q;
      Repaint(h);
      return;
    }
    case Drag::None: break;
  }
  if (s->phase == Phase::Selecting) {
    MonitorRectAt(s, p, &s->scale);
    int target = -1;
    for (int i = 0; i < (int)s->targets.size(); i++)
      if (Inside(s->targets[i], p)) {
        target = i;
        break;
      }
    s->hoverTarget = target;
    Repaint(h);
    return;
  }
  const Action over = HitButton(s, p);
  if (over != s->hover) {
    s->hover = over;
    Repaint(h);
  }
}

static void OnLeftUp(HWND h, Overlay* s, POINT p) {
  const Drag drag = s->drag;
  s->drag = Drag::None;
  if (drag != Drag::None) ReleaseCapture();
  s->current = p;
  switch (drag) {
    case Drag::Create: {
      RECT r = Normal(s->start, p);
      const bool click = Width(r) <= kClickSlop && Height(r) <= kClickSlop;
      if (click && s->hoverTarget >= 0) {
        r = s->targets[s->hoverTarget];
      } else if (Width(r) < kMinSelection || Height(r) < kMinSelection) {
        Repaint(h);  // arrastre accidental: se sigue seleccionando, sin cerrar la captura
        return;
      } else {
        // El raton nunca llega al borde exclusivo de un monitor: pegado a el, la seleccion lo alcanza.
        const RECT mon = MonitorRectAt(s, p);
        if (r.right >= mon.right - 1 && r.right <= mon.right) r.right = mon.right;
        if (r.bottom >= mon.bottom - 1 && r.bottom <= mon.bottom) r.bottom = mon.bottom;
        if (r.right >= s->W - 1) r.right = s->W;
        if (r.bottom >= s->H - 1) r.bottom = s->H;
      }
      s->sel = r;
      EnterEditing(s);
      if (CtrlDown()) {
        DoAction(h, s, Action::Copy);
        return;
      }
      break;
    }
    case Drag::Draw: {
      const bool freehand = s->draft.kind == Kind::Stroke || s->draft.kind == Kind::Highlight;
      const POINT a = s->draft.pts.front(), b = s->draft.pts.back();
      if (freehand || std::abs(a.x - b.x) > 1 || std::abs(a.y - b.y) > 1) {
        PushUndo(s);
        s->items.push_back(std::move(s->draft));
      }
      s->draft = {};
      break;
    }
    default: break;
  }
  Repaint(h);
}

static void OnRightDown(HWND h, Overlay* s) {
  if (s->drag == Drag::Create || s->drag == Drag::Draw) {
    s->drag = Drag::None;
    s->draft = {};
    ReleaseCapture();
    Repaint(h);
    return;
  }
  if (s->phase == Phase::Selecting) Close(h, s);
}

// Con CS_DBLCLKS el segundo clic rapido llega por aqui: si no es para reeditar un texto,
// cuenta como un clic normal (numerar seguido, Deshacer dos veces...).
static void OnDoubleClick(HWND h, Overlay* s, POINT p) {
  if (s->editing && s->editIndex < 0 && s->edit.text.empty() && HitButton(s, p) == Action::None && Inside(s->sel, p)) return;
  if (s->phase == Phase::Editing && !s->editing && (s->tool == Action::Select || s->tool == Action::Text) && HitButton(s, p) == Action::None) {
    const int i = HitText(s, p);
    if (i >= 0) {
      BeginEdit(h, s, s->items[i].pts[0], i);
      return;
    }
  }
  OnLeftDown(h, s, p);
}

static void Nudge(HWND h, Overlay* s, int dx, int dy, bool repeat) {
  const int step = ShiftDown() ? 10 : 1;
  dx *= step;
  dy *= step;
  if (s->selected >= 0 && s->selected < (int)s->items.size()) {
    Annotation& a = s->items[s->selected];
    const POINT d = ClampMove(s, a, dx, dy);
    if (!d.x && !d.y) return;
    if (!repeat) PushUndo(s);
    MoveItem(s, a, d.x, d.y);
  } else {
    s->sel = logic::ApplyGrip(s->sel, Grip::Move, dx, dy, RECT{0, 0, s->W, s->H}, kMinSelection);
    UpdateLayout(s);
  }
  Repaint(h);
}

static void OnEditKey(HWND h, Overlay* s, WPARAM key) {
  switch (key) {
    case VK_ESCAPE: CancelEdit(h, s); return;
    case VK_RETURN:
      if (ShiftDown()) s->edit.Insert(L"\n");
      else CommitEdit(h, s);
      break;
    case VK_LEFT: s->edit.Left(); break;
    case VK_RIGHT: s->edit.Right(); break;
    case VK_HOME: s->edit.Home(); break;
    case VK_END: s->edit.End(); break;
    case VK_BACK: s->edit.Backspace(); break;
    case VK_DELETE: s->edit.Delete(); break;
    default:
      if (CtrlDown() && key == 'V') {
        wstring text = ClipboardText(h);
        text.erase(std::remove(text.begin(), text.end(), L'\r'), text.end());
        s->edit.Insert(text);
      } else if (CtrlDown() && key == 'B') {
        ToggleTextStyle(h, s, FontStyleBold);
        return;
      } else if (CtrlDown() && key == 'I') {
        ToggleTextStyle(h, s, FontStyleItalic);
        return;
      } else if (CtrlDown() && key == 'U') {
        ToggleTextStyle(h, s, FontStyleUnderline);
        return;
      } else if (CtrlDown() && key == 'C') {
        DoAction(h, s, Action::Copy);
        return;
      } else if (CtrlDown() && key == 'S') {
        DoAction(h, s, Action::Save);
        return;
      } else {
        return;
      }
  }
  s->caretOn = true;
  Repaint(h);
}

static void OnKeyDown(HWND h, Overlay* s, WPARAM key, LPARAM l) {
  if (s->editing) {
    OnEditKey(h, s, key);
    return;
  }
  const bool ctrl = CtrlDown();
  // Durante un arrastre, Deshacer o Supr dejarian al arrastre sin su objeto.
  if (s->drag != Drag::None && key != VK_ESCAPE && !(ctrl && key == 'A')) return;
  if (key == VK_ESCAPE || (ctrl && key == 'X')) {
    RequestClose(h, s);
    return;
  }
  if (ctrl && key == 'A') {
    s->sel = {0, 0, s->W, s->H};
    if (s->drag != Drag::None) {
      s->drag = Drag::None;
      ReleaseCapture();
    }
    if (s->phase == Phase::Selecting) EnterEditing(s);
    else UpdateLayout(s);
    Repaint(h);
    return;
  }
  if (s->phase != Phase::Editing) return;
  if (ctrl && key == 'B') ToggleTextStyle(h, s, FontStyleBold);
  else if (ctrl && key == 'I') ToggleTextStyle(h, s, FontStyleItalic);
  else if (ctrl && key == 'U') ToggleTextStyle(h, s, FontStyleUnderline);
  else if (ctrl && key == 'C') DoAction(h, s, Action::Copy);
  else if (ctrl && key == 'S') DoAction(h, s, Action::Save);
  else if (ctrl && key == 'Z') DoAction(h, s, ShiftDown() ? Action::Redo : Action::Undo);
  else if (ctrl && key == 'Y') DoAction(h, s, Action::Redo);
  else if (key == VK_DELETE && s->selected >= 0 && s->selected < (int)s->items.size()) {
    const bool wasNumber = s->items[s->selected].kind == Kind::Number;
    PushUndo(s);
    s->items.erase(s->items.begin() + s->selected);
    s->selected = -1;
    if (wasNumber) RecalcNextNumber(s);
    Repaint(h);
  } else if (s->drag == Drag::None) {
    const bool repeat = (l & (1 << 30)) != 0;
    if (key == VK_LEFT) Nudge(h, s, -1, 0, repeat);
    else if (key == VK_RIGHT) Nudge(h, s, 1, 0, repeat);
    else if (key == VK_UP) Nudge(h, s, 0, -1, repeat);
    else if (key == VK_DOWN) Nudge(h, s, 0, 1, repeat);
  }
}

static LPCWSTR CursorFor(const Overlay* s, POINT p) {
  if (s->phase == Phase::Selecting) return IDC_CROSS;
  if (HitButton(s, p) != Action::None) return IDC_ARROW;
  const bool selectTool = s->tool == Action::Select;
  const int item = selectTool ? HitItem(s, p) : s->tool == Action::Text ? HitText(s, p) : -1;
  if (item >= 0) return IDC_SIZEALL;
  const Grip grip = s->drag == Drag::ResizeSelection ? s->grip : logic::HitGrip(s->sel, p, Sc(s, 6), selectTool, selectTool);
  switch (grip) {
    case Grip::Move: return IDC_SIZEALL;
    case Grip::Left: case Grip::Right: return IDC_SIZEWE;
    case Grip::Top: case Grip::Bottom: return IDC_SIZENS;
    case Grip::TopLeft: case Grip::BottomRight: return IDC_SIZENWSE;
    case Grip::TopRight: case Grip::BottomLeft: return IDC_SIZENESW;
    case Grip::None: break;
  }
  if (!Inside(s->sel, p)) return IDC_ARROW;
  return s->tool == Action::Text ? IDC_IBEAM : IDC_CROSS;
}

LRESULT CALLBACK OverlayProc(HWND h, UINT m, WPARAM w, LPARAM l) {
  auto s = (Overlay*)GetWindowLongPtrW(h, GWLP_USERDATA);
  if (m == WM_CREATE) {
    s = (Overlay*)((CREATESTRUCTW*)l)->lpCreateParams;
    SetWindowLongPtrW(h, GWLP_USERDATA, (LONG_PTR)s);
    return 0;
  }
  if (!s) return DefWindowProcW(h, m, w, l);
  const POINT p{GET_X_LPARAM(l), GET_Y_LPARAM(l)};
  switch (m) {
    case WM_LBUTTONDOWN: OnLeftDown(h, s, p); return 0;
    case WM_MOUSEMOVE: OnMouseMove(h, s, p); return 0;
    case WM_LBUTTONUP: OnLeftUp(h, s, p); return 0;
    case WM_LBUTTONDBLCLK: OnDoubleClick(h, s, p); return 0;
    case WM_RBUTTONDOWN: OnRightDown(h, s); return 0;
    case WM_KEYDOWN: OnKeyDown(h, s, w, l); return 0;
    case WM_CAPTURECHANGED:
      // Un cuadro de mensaje puede quitar la captura en pleno arrastre: sin ella no llega el clic de soltar.
      if (s->drag != Drag::None && (HWND)l != h) {
        s->drag = Drag::None;
        s->draft = {};
        Repaint(h);
      }
      return 0;
    case WM_CHAR:
      if (s->editing && w >= 0x20 && w != 0x7F) {
        s->edit.Insert(wstring(1, (wchar_t)w));
        s->caretOn = true;
        Repaint(h);
      }
      return 0;
    case WM_TIMER:
      if (w == TIMER_CARET) {
        s->caretOn = !s->caretOn;
        Repaint(h);
      }
      return 0;
    case WM_PAINT: {
      PAINTSTRUCT ps;
      HDC dc = BeginPaint(h, &ps);
      PaintOverlay(s);
      BitBlt(dc, ps.rcPaint.left, ps.rcPaint.top, ps.rcPaint.right - ps.rcPaint.left, ps.rcPaint.bottom - ps.rcPaint.top, s->backDC, ps.rcPaint.left, ps.rcPaint.top, SRCCOPY);
      EndPaint(h, &ps);
      return 0;
    }
    case WM_ERASEBKGND: return 1;
    case WM_SETCURSOR: {
      POINT cursor;
      GetCursorPos(&cursor);
      ScreenToClient(h, &cursor);
      SetCursor(LoadCursor(nullptr, CursorFor(s, cursor)));
      return TRUE;
    }
    case WM_DESTROY:
      KillTimer(h, TIMER_CARET);
      SetWindowLongPtrW(h, GWLP_USERDATA, 0);
      s->items.clear();
      s->undo.clear();
      s->redo.clear();
      s->draft = {};
      s->base.reset();
      if (s->backDC) {
        SelectObject(s->backDC, s->backOld);
        DeleteObject(s->backBmp);
        DeleteDC(s->backDC);
      }
      if (s->baseDC) {
        SelectObject(s->baseDC, s->baseOld);
        DeleteObject(s->baseBmp);
        DeleteDC(s->baseDC);
      }
      delete s;
      return 0;
  }
  return DefWindowProcW(h, m, w, l);
}

// --- Apertura ------------------------------------------------------------

static BOOL CALLBACK MonitorBounds(HMONITOR, HDC, LPRECT r, LPARAM data) {
  auto bounds = (RECT*)data;
  if (bounds->right <= bounds->left) {
    *bounds = *r;
  } else {
    bounds->left = std::min(bounds->left, r->left);
    bounds->top = std::min(bounds->top, r->top);
    bounds->right = std::max(bounds->right, r->right);
    bounds->bottom = std::max(bounds->bottom, r->bottom);
  }
  return TRUE;
}

static RECT VirtualDesktop() {
  RECT bounds{0, 0, 0, 0};
  EnumDisplayMonitors(nullptr, nullptr, MonitorBounds, (LPARAM)&bounds);
  if (bounds.right <= bounds.left || bounds.bottom <= bounds.top) {
    const int x = GetSystemMetrics(SM_XVIRTUALSCREEN), y = GetSystemMetrics(SM_YVIRTUALSCREEN);
    bounds = {x, y, x + GetSystemMetrics(SM_CXVIRTUALSCREEN), y + GetSystemMetrics(SM_CYVIRTUALSCREEN)};
  }
  return bounds;
}

static bool WindowBounds(HWND w, RECT* out) {
  // El marco extendido excluye la sombra invisible que GetWindowRect si cuenta.
  if (SUCCEEDED(DwmGetWindowAttribute(w, DWMWA_EXTENDED_FRAME_BOUNDS, out, sizeof(*out)))) return true;
  return GetWindowRect(w, out) != FALSE;
}

struct TargetSearch {
  Overlay* overlay;
  RECT desktop;
};

static BOOL CALLBACK CollectWindow(HWND w, LPARAM data) {
  auto search = (TargetSearch*)data;
  if (!IsWindowVisible(w) || IsIconic(w)) return TRUE;
  // Capas invisibles a pantalla completa (NVIDIA y similares) no son ventanas elegibles.
  const LONG_PTR ex = GetWindowLongPtrW(w, GWL_EXSTYLE);
  if (ex & (WS_EX_TRANSPARENT | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE)) return TRUE;
  if (GetWindowTextLengthW(w) == 0) return TRUE;
  DWORD cloaked = 0;
  if (SUCCEEDED(DwmGetWindowAttribute(w, DWMWA_CLOAKED, &cloaked, sizeof(cloaked))) && cloaked) return TRUE;
  wchar_t cls[32]{};
  GetClassNameW(w, cls, 32);
  if (!wcscmp(cls, L"Progman") || !wcscmp(cls, L"WorkerW") || !wcscmp(cls, L"Shell_TrayWnd")) return TRUE;
  RECT r;
  if (!WindowBounds(w, &r) || !IntersectRect(&r, &r, &search->desktop)) return TRUE;
  if (Width(r) < 40 || Height(r) < 40) return TRUE;
  OffsetRect(&r, -search->desktop.left, -search->desktop.top);
  search->overlay->targets.push_back(r);
  return TRUE;
}

static BOOL CALLBACK CollectMonitor(HMONITOR, HDC, LPRECT r, LPARAM data) {
  auto search = (TargetSearch*)data;
  RECT c = *r;
  OffsetRect(&c, -search->desktop.left, -search->desktop.top);
  search->overlay->targets.push_back(c);
  return TRUE;
}

static HBITMAP MakeDib(HDC reference, int w, int h, uint32_t** bits) {
  BITMAPINFO info{};
  info.bmiHeader.biSize = sizeof(info.bmiHeader);
  info.bmiHeader.biWidth = w;
  info.bmiHeader.biHeight = -h;  // de arriba abajo: bits[y * w + x]
  info.bmiHeader.biPlanes = 1;
  info.bmiHeader.biBitCount = 32;
  info.bmiHeader.biCompression = BI_RGB;
  void* raw = nullptr;
  HBITMAP bmp = CreateDIBSection(reference, &info, DIB_RGB_COLORS, &raw, nullptr, 0);
  if (bits) *bits = (uint32_t*)raw;
  return bmp;
}

void OpenOverlay(CaptureMode mode, HWND target, const RECT* lastSelection) {
  if (FindWindowW(L"CaptureCleanOverlay", nullptr)) return;
  const DPI_AWARENESS_CONTEXT previous = SetThreadDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
  const RECT desktop = VirtualDesktop();
  const int w = Width(desktop), h = Height(desktop);
  if (w < 1 || h < 1) {
    if (previous) SetThreadDpiAwarenessContext(previous);
    return;
  }

  auto s = new Overlay;
  s->W = w;
  s->H = h;
  s->origin = {desktop.left, desktop.top};
  s->color = gSettings.color;
  s->tool = (Action)(gSettings.lastTool + 1);
  GetCursorPos(&s->current);
  s->current.x -= desktop.left;
  s->current.y -= desktop.top;
  MonitorRectAt(s, s->current, &s->scale);

  HDC screen = GetDC(nullptr);
  s->baseDC = CreateCompatibleDC(screen);
  s->backDC = CreateCompatibleDC(screen);
  s->baseBmp = MakeDib(screen, w, h, &s->bits);
  s->backBmp = MakeDib(screen, w, h, nullptr);
  if (!s->baseBmp || !s->backBmp) {
    ReleaseDC(nullptr, screen);
    if (s->baseBmp) DeleteObject(s->baseBmp);
    if (s->backBmp) DeleteObject(s->backBmp);
    DeleteDC(s->baseDC);
    DeleteDC(s->backDC);
    delete s;
    if (previous) SetThreadDpiAwarenessContext(previous);
    ShowTip(L"No hay memoria suficiente para capturar la pantalla.");
    return;
  }
  s->baseOld = SelectObject(s->baseDC, s->baseBmp);
  s->backOld = SelectObject(s->backDC, s->backBmp);
  BitBlt(s->baseDC, 0, 0, w, h, screen, desktop.left, desktop.top, SRCCOPY | CAPTUREBLT);
  ReleaseDC(nullptr, screen);
  GdiFlush();
  s->base = std::make_unique<Bitmap>(w, h, w * 4, PixelFormat32bppRGB, (BYTE*)s->bits);

  TargetSearch search{s, desktop};
  EnumWindows(CollectWindow, (LPARAM)&search);
  EnumDisplayMonitors(nullptr, nullptr, CollectMonitor, (LPARAM)&search);

  RECT initial{};
  bool direct = false;
  if (mode == CaptureMode::FullDesktop) {
    initial = {0, 0, w, h};
    direct = true;
  } else if (mode == CaptureMode::RepeatLast && lastSelection) {
    initial = *lastSelection;
    OffsetRect(&initial, -desktop.left, -desktop.top);
    direct = true;
  } else if (mode == CaptureMode::ActiveWindow && target && IsWindow(target) && !IsIconic(target) && WindowBounds(target, &initial)) {
    OffsetRect(&initial, -desktop.left, -desktop.top);
    direct = true;
  }
  if (direct) {
    const RECT all{0, 0, w, h};
    IntersectRect(&initial, &initial, &all);
    if (Width(initial) >= kMinSelection && Height(initial) >= kMinSelection) {
      s->sel = initial;
      EnterEditing(s);
    }
  }

  HWND o = CreateWindowExW(WS_EX_TOPMOST | WS_EX_TOOLWINDOW, L"CaptureCleanOverlay", nullptr, WS_POPUP | WS_VISIBLE, desktop.left, desktop.top, w, h, nullptr, nullptr, gInst, s);
  if (previous) SetThreadDpiAwarenessContext(previous);
  if (!o) {
    SelectObject(s->baseDC, s->baseOld);
    SelectObject(s->backDC, s->backOld);
    s->base.reset();
    DeleteObject(s->baseBmp);
    DeleteObject(s->backBmp);
    DeleteDC(s->baseDC);
    DeleteDC(s->backDC);
    delete s;
    return;
  }
  SetForegroundWindow(o);
  SetFocus(o);
}
