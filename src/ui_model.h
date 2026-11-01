#pragma once

// Orden de los botones. La posicion en el enum de las herramientas (Select..Number)
// es el valor LastTool guardado en el registro: no reordenar.
enum class Action {
  None,
  Select, Pencil, Line, Arrow, Rectangle, Ellipse, Text, Highlighter, Pixelate, Number,
  Color, SizeMenu, Undo, Redo,
  Copy, Save, Close
};

static const Action kToolActions[] = {
  Action::Select, Action::Pencil, Action::Line, Action::Arrow, Action::Rectangle,
  Action::Ellipse, Action::Text, Action::Highlighter, Action::Pixelate, Action::Number,
  Action::Color, Action::SizeMenu, Action::Undo, Action::Redo
};
static const Action kBarActions[] = {Action::Copy, Action::Save, Action::Close};
static constexpr int kToolCount = sizeof(kToolActions) / sizeof(kToolActions[0]);
static constexpr int kBarCount = sizeof(kBarActions) / sizeof(kBarActions[0]);

static constexpr int kButtonSize = 34;
static constexpr int kToolbarGap = 7;

inline bool IsTool(Action a) { return a >= Action::Select && a <= Action::Number; }
