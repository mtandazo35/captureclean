#pragma once

// Orden de los botones. La posicion en el enum de las herramientas (Select..Number)
// es el valor LastTool guardado en el registro: no reordenar.
enum class Action {
  None,
  Select, Pencil, Line, Arrow, Rectangle, Ellipse, Text, Highlighter, Pixelate, Number,
  Color, SizeMenu, Undo, Redo,
  Copy, Save, Close,
  TextBold, TextItalic, TextUnderline
};

static const Action kToolActions[] = {
  Action::Select, Action::Pencil, Action::Line, Action::Arrow, Action::Rectangle,
  Action::Ellipse, Action::Text, Action::Highlighter, Action::Pixelate, Action::Number,
  Action::Color, Action::SizeMenu, Action::Undo, Action::Redo
};
static const Action kBarActions[] = {Action::Copy, Action::Save, Action::Close};
// Mini barra junto al boton Texto. No es parte de las otras dos: no interrumpe la escritura.
static const Action kFormatActions[] = {Action::TextBold, Action::TextItalic, Action::TextUnderline};
static constexpr int kFormatCount = sizeof(kFormatActions) / sizeof(kFormatActions[0]);
static constexpr int kTextToolIndex = 6;  // posicion de Action::Text en kToolActions
static constexpr int kToolCount = sizeof(kToolActions) / sizeof(kToolActions[0]);
static constexpr int kBarCount = sizeof(kBarActions) / sizeof(kBarActions[0]);

static constexpr int kButtonSize = 34;
static constexpr int kToolbarGap = 7;

inline bool IsTool(Action a) { return a >= Action::Select && a <= Action::Number; }
inline bool IsFormat(Action a) { return a >= Action::TextBold && a <= Action::TextUnderline; }
