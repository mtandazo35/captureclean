#pragma once
// Logica sin ventanas ni dibujo, para poder probarla sola (tests/unit.cpp).
#include <windows.h>
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <string>

namespace logic {

inline int Width(const RECT& r) { return r.right - r.left; }
inline int Height(const RECT& r) { return r.bottom - r.top; }
inline RECT Normal(POINT a, POINT b) {
  return {std::min(a.x, b.x), std::min(a.y, b.y), std::max(a.x, b.x), std::max(a.y, b.y)};
}
inline bool Inside(const RECT& r, POINT p) {
  return p.x >= r.left && p.x < r.right && p.y >= r.top && p.y < r.bottom;
}
inline POINT ClampPoint(POINT p, const RECT& r) {
  return {std::clamp(p.x, r.left, std::max(r.left, r.right - 1)),
          std::clamp(p.y, r.top, std::max(r.top, r.bottom - 1))};
}
inline double SegmentDistance(POINT p, POINT a, POINT b) {
  const double vx = b.x - a.x, vy = b.y - a.y, wx = p.x - a.x, wy = p.y - a.y;
  const double len = vx * vx + vy * vy;
  const double t = len > 0 ? std::clamp((wx * vx + wy * vy) / len, 0.0, 1.0) : 0.0;
  return std::hypot(wx - t * vx, wy - t * vy);
}

// Tiradores de la seleccion.
enum class Grip { None, Move, Left, Right, Top, Bottom, TopLeft, TopRight, BottomLeft, BottomRight };

// wholeEdges: todo el borde sirve de tirador (herramienta Seleccionar); si no, solo los
// 8 puntos visibles, para no estorbar al dibujar cerca del borde.
inline Grip HitGrip(const RECT& r, POINT p, int tol, bool wholeEdges, bool allowMove) {
  const bool nl = std::abs(p.x - r.left) <= tol, nr = std::abs(p.x - r.right) <= tol;
  const bool nt = std::abs(p.y - r.top) <= tol, nb = std::abs(p.y - r.bottom) <= tol;
  if (nl && nt) return Grip::TopLeft;
  if (nr && nt) return Grip::TopRight;
  if (nl && nb) return Grip::BottomLeft;
  if (nr && nb) return Grip::BottomRight;
  const bool inX = p.x >= r.left - tol && p.x <= r.right + tol;
  const bool inY = p.y >= r.top - tol && p.y <= r.bottom + tol;
  const bool midX = std::abs(p.x - (r.left + r.right) / 2) <= tol;
  const bool midY = std::abs(p.y - (r.top + r.bottom) / 2) <= tol;
  if (nt && (wholeEdges ? inX : midX)) return Grip::Top;
  if (nb && (wholeEdges ? inX : midX)) return Grip::Bottom;
  if (nl && (wholeEdges ? inY : midY)) return Grip::Left;
  if (nr && (wholeEdges ? inY : midY)) return Grip::Right;
  if (allowMove && Inside(r, p)) return Grip::Move;
  return Grip::None;
}

inline RECT ApplyGrip(RECT r, Grip g, int dx, int dy, const RECT& bounds, int minSize) {
  if (g == Grip::Move) {
    const int w = Width(r), h = Height(r);
    r.left = std::clamp<LONG>(r.left + dx, bounds.left, std::max<LONG>(bounds.left, bounds.right - w));
    r.top = std::clamp<LONG>(r.top + dy, bounds.top, std::max<LONG>(bounds.top, bounds.bottom - h));
    r.right = r.left + w;
    r.bottom = r.top + h;
    return r;
  }
  if (g == Grip::Left || g == Grip::TopLeft || g == Grip::BottomLeft)
    r.left = std::clamp<LONG>(r.left + dx, bounds.left, std::max<LONG>(bounds.left, r.right - minSize));
  if (g == Grip::Right || g == Grip::TopRight || g == Grip::BottomRight)
    r.right = std::clamp<LONG>(r.right + dx, std::min<LONG>(bounds.right, r.left + minSize), bounds.right);
  if (g == Grip::Top || g == Grip::TopLeft || g == Grip::TopRight)
    r.top = std::clamp<LONG>(r.top + dy, bounds.top, std::max<LONG>(bounds.top, r.bottom - minSize));
  if (g == Grip::Bottom || g == Grip::BottomLeft || g == Grip::BottomRight)
    r.bottom = std::clamp<LONG>(r.bottom + dy, std::min<LONG>(bounds.bottom, r.top + minSize), bounds.bottom);
  return r;
}

struct BarLayout { RECT tools, actions; };

// Coloca las barras dentro del monitor de la seleccion: nunca en zonas muertas del
// escritorio virtual ni fuera de pantalla, ni una encima de la otra.
inline BarLayout PlaceToolbars(const RECT& sel, const RECT& mon, int button, int gap, int toolCount, int barCount) {
  const int m = 5;
  const int th = button * toolCount, aw = button * barCount;
  auto fit = [](int v, int lo, int hi) { return std::clamp(v, lo, std::max(lo, hi)); };

  int tx = sel.right + gap;
  if (tx + button > mon.right - m) tx = sel.left - button - gap;
  if (tx < mon.left + m) tx = sel.right - button - gap;
  tx = fit(tx, mon.left + m, mon.right - button - m);
  const int ty = fit(sel.top, mon.top + m, mon.bottom - th - m);
  RECT tools{tx, ty, tx + button, ty + th};

  int ax = fit(sel.right - aw, mon.left + m, mon.right - aw - m);
  int ay = sel.bottom + gap;
  if (ay + button > mon.bottom - m) ay = sel.top - button - gap;
  if (ay < mon.top + m) ay = sel.bottom - button - gap;
  ay = fit(ay, mon.top + m, mon.bottom - button - m);
  RECT actions{ax, ay, ax + aw, ay + button};

  RECT overlap;
  if (IntersectRect(&overlap, &tools, &actions)) {
    ax = fit(tools.left - aw - gap, mon.left + m, mon.right - aw - m);
    actions = {ax, ay, ax + aw, ay + button};
    if (IntersectRect(&overlap, &tools, &actions)) {
      ax = fit(tools.right + gap, mon.left + m, mon.right - aw - m);
      actions = {ax, ay, ax + aw, ay + button};
    }
  }
  return {tools, actions};
}

// Edicion de texto en la propia captura. No parte pares sustitutos (emoji).
struct TextEdit {
  std::wstring text;
  size_t caret = 0;

  static bool Low(wchar_t c) { return c >= 0xDC00 && c <= 0xDFFF; }
  void Insert(const std::wstring& s) { text.insert(caret, s); caret += s.size(); }
  void Left() { if (caret) { caret--; if (caret && Low(text[caret])) caret--; } }
  void Right() { if (caret < text.size()) { caret++; if (caret < text.size() && Low(text[caret])) caret++; } }
  void Backspace() { const size_t end = caret; Left(); text.erase(caret, end - caret); }
  void Delete() { const size_t from = caret; Right(); text.erase(from, caret - from); caret = from; }
  void Home() { const size_t p = caret ? text.rfind(L'\n', caret - 1) : std::wstring::npos; caret = p == std::wstring::npos ? 0 : p + 1; }
  void End() { const size_t p = text.find(L'\n', caret); caret = p == std::wstring::npos ? text.size() : p; }
};

inline std::wstring Pad(unsigned value, size_t width) {
  std::wstring s = std::to_wstring(value);
  return s.size() < width ? std::wstring(width - s.size(), L'0') + s : s;
}

// Captura_2026-11-01_14-05-09.png; attempt > 0 anade _N para no pisar otro archivo.
inline std::wstring CaptureFileName(const SYSTEMTIME& t, const std::wstring& ext, int attempt = 0) {
  std::wstring n = L"Captura_" + Pad(t.wYear, 4) + L"-" + Pad(t.wMonth, 2) + L"-" + Pad(t.wDay, 2) + L"_" +
                   Pad(t.wHour, 2) + L"-" + Pad(t.wMinute, 2) + L"-" + Pad(t.wSecond, 2);
  if (attempt > 0) n += L"_" + std::to_wstring(attempt);
  return n + L"." + ext;
}

inline bool HasExtension(const std::wstring& path, const wchar_t* ext) {
  const size_t dot = path.rfind(L'.');
  return dot != std::wstring::npos && _wcsicmp(path.c_str() + dot + 1, ext) == 0;
}

// Un valor del registro fuera de la lista se lleva al permitido mas cercano.
template <size_t N>
int NearestOf(int value, const int (&allowed)[N]) {
  int best = allowed[0];
  for (int a : allowed) if (std::abs(a - value) < std::abs(best - value)) best = a;
  return best;
}

}  // namespace logic
