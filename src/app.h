#pragma once
#include <windows.h>

enum class CaptureMode { Area, ActiveWindow, FullDesktop, RepeatLast };

extern HINSTANCE gInst;
extern HWND gHost;

void ShowTip(const wchar_t* text);
void RememberSelection(const RECT& screenRect);
void ShowPreferences();
bool PreferencesDialogMessage(MSG* msg);

// overlay.cpp
LRESULT CALLBACK OverlayProc(HWND h, UINT m, WPARAM w, LPARAM l);
void OpenOverlay(CaptureMode mode, HWND target, const RECT* lastSelection);
