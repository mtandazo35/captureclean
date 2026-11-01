#pragma once
#include <windows.h>
#include <string>

struct Settings {
  int thickness = 4;
  int highlighterWidth = 20;
  int textSize = 24;
  bool textBold = true;
  bool textItalic = false;
  bool textUnderline = false;
  int pixelBlock = 12;
  int numberSize = 30;
  int lastTool = 1;
  COLORREF color = RGB(235, 52, 62);
  bool showMagnifier = true;
  bool confirmOnEscape = true;
  bool autoSave = false;
  int autoSaveFormat = 0;  // 0 PNG, 1 JPEG
  std::wstring autoSaveFolder;
  std::wstring lastSaveFolder;
};

extern Settings gSettings;
extern const int kThicknessValues[3];
extern const int kHighlighterValues[3];
extern const int kTextSizeValues[3];
extern const int kPixelBlockValues[3];
extern const int kNumberSizeValues[3];

void LoadSettings();
void SaveSettings();

bool AutoStartEnabled();
bool SetAutoStart(bool enabled);
// Primera ejecucion: activa el inicio con Windows; despues solo respeta lo elegido.
void SyncAutoStart();
