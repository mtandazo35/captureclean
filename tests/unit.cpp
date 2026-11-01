// Pruebas de la logica pura. Sale con codigo != 0 si algo falla.
#include "../src/logic.h"
#include "../src/ui_model.h"
#include <cstdio>
#include <vector>

using namespace logic;

static int failures = 0;
#define CHECK(cond) do { if (!(cond)) { printf("FALLA linea %d: %s\n", __LINE__, #cond); failures++; } } while (0)

static bool Contained(const RECT& in, const RECT& out) {
  return in.left >= out.left && in.top >= out.top && in.right <= out.right && in.bottom <= out.bottom;
}

static void TestToolbarsStayOnMonitor() {
  // Barrido de selecciones, incluido un monitor a la izquierda con coordenadas negativas.
  const std::vector<RECT> monitors = {{0, 0, 1920, 1080}, {-1920, 1, 0, 1081}, {0, 0, 1024, 768}};
  for (const RECT& mon : monitors) {
    for (int x = 0; x < 6; x++) for (int y = 0; y < 6; y++) for (int w = 1; w <= 6; w++) for (int h = 1; h <= 6; h++) {
      const int sw = logic::Width(mon) * w / 6, sh = logic::Height(mon) * h / 6;
      const int sx = mon.left + (logic::Width(mon) - sw) * x / 5, sy = mon.top + (logic::Height(mon) - sh) * y / 5;
      const RECT sel{sx, sy, sx + sw, sy + sh};
      const BarLayout b = PlaceToolbars(sel, mon, kButtonSize, kToolbarGap, kToolCount, kBarCount);
      CHECK(Contained(b.tools, mon));
      CHECK(Contained(b.actions, mon));
      RECT overlap;
      CHECK(!IntersectRect(&overlap, &b.tools, &b.actions));
      CHECK(logic::Height(b.tools) == kButtonSize * kToolCount);
      CHECK(logic::Width(b.actions) == kButtonSize * kBarCount);
    }
  }
}

static void TestToolbarPreference() {
  const RECT mon{0, 0, 1920, 1080}, sel{300, 200, 900, 700};
  const BarLayout b = PlaceToolbars(sel, mon, 34, 7, kToolCount, kBarCount);
  CHECK(b.tools.left == 907);       // a la derecha
  CHECK(b.tools.top == 200);
  CHECK(b.actions.top == 707);      // debajo
  CHECK(b.actions.right == 900);
}

static void TestGrips() {
  const RECT r{100, 100, 500, 400};
  CHECK(HitGrip(r, {100, 100}, 6, false, false) == Grip::TopLeft);
  CHECK(HitGrip(r, {503, 397}, 6, false, false) == Grip::BottomRight);
  CHECK(HitGrip(r, {300, 100}, 6, false, false) == Grip::Top);
  CHECK(HitGrip(r, {200, 100}, 6, false, false) == Grip::None);   // herramienta de dibujo: solo los 8 puntos
  CHECK(HitGrip(r, {200, 100}, 6, true, false) == Grip::Top);     // Seleccionar: todo el borde
  CHECK(HitGrip(r, {300, 250}, 6, false, false) == Grip::None);
  CHECK(HitGrip(r, {300, 250}, 6, true, true) == Grip::Move);
  CHECK(HitGrip(r, {50, 50}, 6, true, true) == Grip::None);
}

static void TestApplyGrip() {
  const RECT bounds{0, 0, 1000, 800}, r{100, 100, 500, 400};
  RECT m = ApplyGrip(r, Grip::Move, 50, 20, bounds, 12);
  CHECK(m.left == 150 && m.right == 550 && m.top == 120 && m.bottom == 420);
  m = ApplyGrip(r, Grip::Move, 5000, 5000, bounds, 12);  // sin salirse y sin cambiar de tamano
  CHECK(m.right == 1000 && m.bottom == 800 && logic::Width(m) == 400 && logic::Height(m) == 300);
  m = ApplyGrip(r, Grip::Move, -5000, -5000, bounds, 12);
  CHECK(m.left == 0 && m.top == 0 && logic::Width(m) == 400);
  m = ApplyGrip(r, Grip::BottomRight, 20, 30, bounds, 12);
  CHECK(m.left == 100 && m.top == 100 && m.right == 520 && m.bottom == 430);
  m = ApplyGrip(r, Grip::Right, -5000, 0, bounds, 12);   // no se invierte: tamano minimo
  CHECK(logic::Width(m) == 12);
  m = ApplyGrip(r, Grip::TopLeft, 5000, 5000, bounds, 12);
  CHECK(logic::Width(m) == 12 && logic::Height(m) == 12 && m.right == 500 && m.bottom == 400);
}

static void TestSegmentDistance() {
  CHECK(SegmentDistance({5, 3}, {0, 0}, {10, 0}) == 3.0);
  CHECK(SegmentDistance({13, 4}, {0, 0}, {10, 0}) == 5.0);   // pasado el extremo
  CHECK(SegmentDistance({1, 1}, {1, 1}, {1, 1}) == 0.0);     // segmento de longitud 0
}

static void TestTextEdit() {
  TextEdit t;
  t.Insert(L"hola");
  CHECK(t.text == L"hola" && t.caret == 4);
  t.Left(); t.Left();
  t.Insert(L"XY");
  CHECK(t.text == L"hoXYla" && t.caret == 4);
  t.Backspace();
  CHECK(t.text == L"hoXla" && t.caret == 3);
  t.Delete();
  CHECK(t.text == L"hoXa" && t.caret == 3);
  t.Home();
  CHECK(t.caret == 0);
  t.Backspace(); t.Left();       // al principio no hace nada
  CHECK(t.text == L"hoXa" && t.caret == 0);
  t.End();
  CHECK(t.caret == 4);
  t.Delete(); t.Right();         // al final no hace nada
  CHECK(t.text == L"hoXa" && t.caret == 4);

  TextEdit lines;
  lines.Insert(L"uno\ndos");
  lines.Home();
  CHECK(lines.caret == 4);
  lines.Left(); lines.Home();
  CHECK(lines.caret == 0);
  lines.End();
  CHECK(lines.caret == 3);

  TextEdit emoji;
  emoji.Insert(L"a\xD83D\xDE00z");
  emoji.Left();                  // antes de z
  emoji.Backspace();             // borra el emoji entero, sin dejar media pareja
  CHECK(emoji.text == L"az" && emoji.caret == 1);
  emoji.Insert(L"\xD83D\xDE00");
  emoji.Home(); emoji.Right(); emoji.Right();
  CHECK(emoji.caret == 3);
  emoji.Home(); emoji.Right(); emoji.Delete();
  CHECK(emoji.text == L"az");
}

static void TestFileNames() {
  SYSTEMTIME t{};
  t.wYear = 2026; t.wMonth = 3; t.wDay = 7; t.wHour = 4; t.wMinute = 5; t.wSecond = 9;
  CHECK(CaptureFileName(t, L"png") == L"Captura_2026-03-07_04-05-09.png");
  CHECK(CaptureFileName(t, L"jpg", 2) == L"Captura_2026-03-07_04-05-09_2.jpg");
  CHECK(HasExtension(L"C:\\a\\b.PNG", L"png"));
  CHECK(!HasExtension(L"C:\\a.b\\c", L"png"));
  CHECK(!HasExtension(L"foto.png.txt", L"png"));
}

static void TestNearestOf() {
  const int allowed[3] = {2, 4, 7};
  CHECK(NearestOf(4, allowed) == 4);
  CHECK(NearestOf(0, allowed) == 2);
  CHECK(NearestOf(100, allowed) == 7);
  CHECK(NearestOf(5, allowed) == 4);
}

static void TestToolIdsMatchSavedPreference() {
  // LastTool se guarda como posicion de la herramienta: cambiarla cambiaria la de los usuarios.
  CHECK((int)Action::Select - 1 == 0);
  CHECK((int)Action::Text - 1 == 6);
  CHECK((int)Action::Number - 1 == 9);
  for (Action a : kToolActions) CHECK(a != Action::None);
  for (Action a : kBarActions) CHECK(a != Action::None && !IsTool(a));
}

int main() {
  TestToolbarsStayOnMonitor();
  TestToolbarPreference();
  TestGrips();
  TestApplyGrip();
  TestSegmentDistance();
  TestTextEdit();
  TestFileNames();
  TestNearestOf();
  TestToolIdsMatchSavedPreference();
  printf(failures ? "%d FALLOS\n" : "OK (%d fallos)\n", failures);
  return failures ? 1 : 0;
}
