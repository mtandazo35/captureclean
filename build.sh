#!/usr/bin/env bash
set -euo pipefail
mkdir -p dist
x86_64-w64-mingw32-windres client/CaptureClean.rc -O coff -o dist/CaptureClean-res.o
x86_64-w64-mingw32-g++ -std=c++17 -O2 -static -municode -mwindows \
  -Wl,--no-insert-timestamp client/CaptureCleanV2.cpp dist/CaptureClean-res.o \
  -o dist/CaptureClean.exe \
  -lgdiplus -lcomdlg32
makensis installer/CaptureClean.nsi
cp -f dist/CaptureClean-Setup-2.10.1.exe dist/CaptureClean-Setup.exe
cp -f dist/CaptureClean.exe dist/CaptureClean-2.10.1.exe
