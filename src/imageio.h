#pragma once
#include <windows.h>
#include <gdiplus.h>
#include <string>

// Devuelve false si no se pudo; el mensaje al usuario lo decide quien llama.
bool CopyToClipboard(HWND owner, Gdiplus::Bitmap* image);

// Escribe primero a un temporal: un fallo a medias no deja un archivo roto en destino.
bool WriteImage(Gdiplus::Bitmap* image, const std::wstring& path);

// Resultado del dialogo: guardado, cancelado por el usuario o error de escritura.
enum class SaveResult { Saved, Cancelled, Failed };
SaveResult SaveWithDialog(HWND owner, Gdiplus::Bitmap* image);

bool AutoSaveImage(Gdiplus::Bitmap* image, std::wstring* savedPath);
