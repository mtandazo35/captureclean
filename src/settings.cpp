#include "settings.h"
#include "logic.h"
#include <shlobj.h>

// Misma clave y mismos valores que las versiones 3.x, para conservar las preferencias.
static const wchar_t* kKey = L"Software\\CaptureClean";
static const wchar_t* kRunKey = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
static const wchar_t* kRunValue = L"CaptureClean";

Settings gSettings;
const int kThicknessValues[3] = {2, 4, 7};
const int kHighlighterValues[3] = {12, 20, 32};
const int kTextSizeValues[3] = {18, 24, 32};
const int kPixelBlockValues[3] = {6, 12, 20};
const int kNumberSizeValues[3] = {24, 30, 40};

static DWORD ReadDword(const wchar_t* name, DWORD fallback) {
  DWORD value = 0, size = sizeof(value);
  return RegGetValueW(HKEY_CURRENT_USER, kKey, name, RRF_RT_REG_DWORD, nullptr, &value, &size) == ERROR_SUCCESS ? value : fallback;
}
static void WriteDword(const wchar_t* name, DWORD value) {
  RegSetKeyValueW(HKEY_CURRENT_USER, kKey, name, REG_DWORD, &value, sizeof(value));
}
static std::wstring ReadString(const wchar_t* name) {
  wchar_t buffer[32768];
  DWORD size = sizeof(buffer);
  return RegGetValueW(HKEY_CURRENT_USER, kKey, name, RRF_RT_REG_SZ, nullptr, buffer, &size) == ERROR_SUCCESS ? buffer : L"";
}
static void WriteString(const wchar_t* name, const std::wstring& value) {
  RegSetKeyValueW(HKEY_CURRENT_USER, kKey, name, REG_SZ, value.c_str(), (DWORD)((value.size() + 1) * sizeof(wchar_t)));
}

static std::wstring DefaultFolder() {
  PWSTR pictures = nullptr;
  std::wstring out;
  if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_Pictures, 0, nullptr, &pictures))) out = std::wstring(pictures) + L"\\CaptureClean";
  CoTaskMemFree(pictures);
  return out;
}

void LoadSettings() {
  Settings& s = gSettings;
  s.thickness = logic::NearestOf((int)ReadDword(L"Thickness", s.thickness), kThicknessValues);
  s.highlighterWidth = logic::NearestOf((int)ReadDword(L"HighlighterWidth", s.highlighterWidth), kHighlighterValues);
  s.textSize = logic::NearestOf((int)ReadDword(L"TextSize", s.textSize), kTextSizeValues);
  s.textBold = ReadDword(L"TextBold", s.textBold) != 0;
  s.pixelBlock = logic::NearestOf((int)ReadDword(L"PixelBlock", s.pixelBlock), kPixelBlockValues);
  s.numberSize = logic::NearestOf((int)ReadDword(L"NumberSize", s.numberSize), kNumberSizeValues);
  s.lastTool = std::clamp((int)ReadDword(L"LastTool", s.lastTool), 0, 9);
  s.color = ReadDword(L"Color", s.color) & 0xFFFFFF;
  s.showMagnifier = ReadDword(L"ShowMagnifier", s.showMagnifier) != 0;
  s.confirmOnEscape = ReadDword(L"ConfirmOnEscape", s.confirmOnEscape) != 0;
  s.autoSave = ReadDword(L"AutoSave", s.autoSave) != 0;
  s.autoSaveFormat = ReadDword(L"AutoSaveFormat", 0) == 1 ? 1 : 0;
  s.autoSaveFolder = ReadString(L"AutoSaveFolder");
  if (s.autoSaveFolder.empty()) s.autoSaveFolder = DefaultFolder();
  s.lastSaveFolder = ReadString(L"LastSaveFolder");
}

void SaveSettings() {
  const Settings& s = gSettings;
  WriteDword(L"Thickness", s.thickness);
  WriteDword(L"HighlighterWidth", s.highlighterWidth);
  WriteDword(L"TextSize", s.textSize);
  WriteDword(L"TextBold", s.textBold);
  WriteDword(L"PixelBlock", s.pixelBlock);
  WriteDword(L"NumberSize", s.numberSize);
  WriteDword(L"LastTool", s.lastTool);
  WriteDword(L"Color", s.color);
  WriteDword(L"ShowMagnifier", s.showMagnifier);
  WriteDword(L"ConfirmOnEscape", s.confirmOnEscape);
  WriteDword(L"AutoSave", s.autoSave);
  WriteDword(L"AutoSaveFormat", s.autoSaveFormat);
  WriteString(L"AutoSaveFolder", s.autoSaveFolder);
  WriteString(L"LastSaveFolder", s.lastSaveFolder);
}

bool AutoStartEnabled() { return ReadDword(L"AutoStartEnabled", 1) != 0; }

bool SetAutoStart(bool enabled) {
  WriteDword(L"AutoStartConfigured", 1);
  WriteDword(L"AutoStartEnabled", enabled);
  if (!enabled) {
    const LSTATUS r = RegDeleteKeyValueW(HKEY_CURRENT_USER, kRunKey, kRunValue);
    return r == ERROR_SUCCESS || r == ERROR_FILE_NOT_FOUND;
  }
  wchar_t path[32768];
  if (!GetModuleFileNameW(nullptr, path, 32768)) return false;
  const std::wstring command = L"\"" + std::wstring(path) + L"\" --background";
  return RegSetKeyValueW(HKEY_CURRENT_USER, kRunKey, kRunValue, REG_SZ, command.c_str(), (DWORD)((command.size() + 1) * sizeof(wchar_t))) == ERROR_SUCCESS;
}

void SyncAutoStart() {
  // Tambien reescribe el valor si ya estaba activo: las 3.x lo guardaban sin --background.
  const bool configured = ReadDword(L"AutoStartConfigured", 0) != 0;
  if (!configured || AutoStartEnabled()) SetAutoStart(true);
}
