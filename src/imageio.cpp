#include "imageio.h"
#include "logic.h"
#include "settings.h"
#include <commdlg.h>
#include <shlobj.h>
#include <cstring>
#include <vector>

using namespace Gdiplus;

bool CopyToClipboard(HWND owner, Bitmap* image) {
  const int w = (int)image->GetWidth(), h = (int)image->GetHeight();
  Gdiplus::Rect all(0, 0, w, h);
  BitmapData data{};
  if (image->LockBits(&all, ImageLockModeRead, PixelFormat24bppRGB, &data) != Ok) return false;

  const DWORD stride = ((w * 24 + 31) / 32) * 4, bytes = stride * h;
  HGLOBAL mem = GlobalAlloc(GMEM_MOVEABLE, sizeof(BITMAPINFOHEADER) + bytes);
  BYTE* raw = mem ? (BYTE*)GlobalLock(mem) : nullptr;
  if (!raw) {
    image->UnlockBits(&data);
    if (mem) GlobalFree(mem);
    return false;
  }
  BITMAPINFOHEADER bi{};
  bi.biSize = sizeof(bi);
  bi.biWidth = w;
  bi.biHeight = h;  // positivo: DIB de abajo arriba, el formato que mejor aceptan las demas apps
  bi.biPlanes = 1;
  bi.biBitCount = 24;
  bi.biCompression = BI_RGB;
  bi.biSizeImage = bytes;
  memcpy(raw, &bi, sizeof(bi));
  for (int y = 0; y < h; y++)
    memcpy(raw + sizeof(bi) + (h - 1 - y) * stride, (BYTE*)data.Scan0 + y * data.Stride, w * 3);
  GlobalUnlock(mem);
  image->UnlockBits(&data);

  bool opened = false;
  for (int i = 0; i < 10 && !opened; i++) {
    opened = OpenClipboard(owner) != FALSE;
    if (!opened) Sleep(25);
  }
  if (!opened) {
    GlobalFree(mem);
    return false;
  }
  const bool ok = EmptyClipboard() && SetClipboardData(CF_DIB, mem);
  CloseClipboard();
  if (!ok) GlobalFree(mem);
  return ok;
}

static bool EncoderFor(const wchar_t* mime, CLSID* clsid) {
  UINT count = 0, size = 0;
  if (GetImageEncodersSize(&count, &size) != Ok || !size) return false;
  std::vector<BYTE> buffer(size);
  auto encoders = (ImageCodecInfo*)buffer.data();
  if (GetImageEncoders(count, size, encoders) != Ok) return false;
  for (UINT i = 0; i < count; i++)
    if (wcscmp(encoders[i].MimeType, mime) == 0) {
      *clsid = encoders[i].Clsid;
      return true;
    }
  return false;
}

bool WriteImage(Bitmap* image, const std::wstring& path) {
  const bool jpeg = logic::HasExtension(path, L"jpg") || logic::HasExtension(path, L"jpeg");
  CLSID clsid{};
  if (!EncoderFor(jpeg ? L"image/jpeg" : L"image/png", &clsid)) return false;

  const std::wstring temp = path + L".tmp";
  Status status;
  if (jpeg) {
    ULONG quality = 92;
    EncoderParameters params{};
    params.Count = 1;
    params.Parameter[0].Guid = EncoderQuality;
    params.Parameter[0].Type = EncoderParameterValueTypeLong;
    params.Parameter[0].NumberOfValues = 1;
    params.Parameter[0].Value = &quality;
    status = image->Save(temp.c_str(), &clsid, &params);
  } else {
    status = image->Save(temp.c_str(), &clsid, nullptr);
  }
  if (status != Ok || !MoveFileExW(temp.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING)) {
    DeleteFileW(temp.c_str());
    return false;
  }
  return true;
}

static std::wstring FolderOf(const std::wstring& path) {
  const size_t slash = path.find_last_of(L"\\/");
  return slash == std::wstring::npos ? L"" : path.substr(0, slash);
}

SaveResult SaveWithDialog(HWND owner, Bitmap* image) {
  SYSTEMTIME now;
  GetLocalTime(&now);
  std::vector<wchar_t> path(32768);
  wcscpy(path.data(), logic::CaptureFileName(now, L"png").c_str());
  const std::wstring folder = gSettings.lastSaveFolder;

  OPENFILENAMEW f{};
  f.lStructSize = sizeof(f);
  f.hwndOwner = owner;
  f.lpstrFilter = L"Imagen PNG (*.png)\0*.png\0Imagen JPEG (*.jpg)\0*.jpg\0";
  f.lpstrFile = path.data();
  f.nMaxFile = (DWORD)path.size();
  f.lpstrDefExt = L"png";
  f.lpstrInitialDir = folder.empty() ? nullptr : folder.c_str();
  f.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR | OFN_EXPLORER;
  if (!GetSaveFileNameW(&f)) return SaveResult::Cancelled;

  std::wstring chosen = path.data();
  // La extension escrita manda sobre el filtro: si no es de imagen, se anade la del filtro.
  if (!logic::HasExtension(chosen, L"png") && !logic::HasExtension(chosen, L"jpg") && !logic::HasExtension(chosen, L"jpeg"))
    chosen += f.nFilterIndex == 2 ? L".jpg" : L".png";
  if (!WriteImage(image, chosen)) return SaveResult::Failed;
  gSettings.lastSaveFolder = FolderOf(chosen);
  SaveSettings();
  return SaveResult::Saved;
}

bool AutoSaveImage(Bitmap* image, std::wstring* savedPath) {
  const std::wstring folder = gSettings.autoSaveFolder;
  if (folder.empty()) return false;
  const int made = SHCreateDirectoryExW(nullptr, folder.c_str(), nullptr);
  if (made != ERROR_SUCCESS && made != ERROR_ALREADY_EXISTS && made != ERROR_FILE_EXISTS) return false;

  SYSTEMTIME now;
  GetLocalTime(&now);
  const wchar_t* ext = gSettings.autoSaveFormat == 1 ? L"jpg" : L"png";
  for (int attempt = 0; attempt < 1000; attempt++) {
    const std::wstring path = folder + L"\\" + logic::CaptureFileName(now, ext, attempt);
    if (GetFileAttributesW(path.c_str()) != INVALID_FILE_ATTRIBUTES) continue;
    if (!WriteImage(image, path)) return false;
    if (savedPath) *savedPath = path;
    return true;
  }
  return false;
}
