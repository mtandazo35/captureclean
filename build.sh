#!/usr/bin/env bash
# Compilacion cruzada desde Linux. Requiere: mingw-w64 nsis
set -euo pipefail
cd "$(dirname "$0")"
VERSION=$(sed -n 's/^#define CC_VERSION_A "\(.*\)"/\1/p' src/version.h)
mkdir -p dist
FLAGS="-std=c++17 -O2 -Wall -Wextra -static -DUNICODE -D_UNICODE -D_WIN32_WINNT=0x0A00 -DWINVER=0x0A00"
x86_64-w64-mingw32-windres -I src src/CaptureClean.rc -O coff -o dist/CaptureClean-res.o
# shellcheck disable=SC2086
x86_64-w64-mingw32-g++ $FLAGS -municode -mwindows -s -Wl,--no-insert-timestamp \
  src/main.cpp src/overlay.cpp src/settings.cpp src/imageio.cpp src/preferences.cpp dist/CaptureClean-res.o \
  -o dist/CaptureClean.exe \
  -lgdiplus -lcomdlg32 -lshcore -ldwmapi -lole32 -luuid
# shellcheck disable=SC2086
x86_64-w64-mingw32-g++ $FLAGS tests/unit.cpp -o dist/unit.exe
# shellcheck disable=SC2086
x86_64-w64-mingw32-g++ $FLAGS -municode -mconsole tests/ui_test.cpp -o dist/ui_test.exe -lgdiplus -lgdi32
makensis -V2 -INPUTCHARSET UTF8 -DVERSION="$VERSION" installer/CaptureClean.nsi
cp -f dist/CaptureClean.exe "dist/CaptureClean-$VERSION.exe"
(cd dist && sha256sum "CaptureClean-Setup-$VERSION.exe" > "CaptureClean-Setup-$VERSION.exe.sha256")
echo "CaptureClean $VERSION listo en dist/"
