#include <windows.h>
int WINAPI WinMain(HINSTANCE,HINSTANCE,LPSTR,int){
  Sleep(1800);
  SetCursorPos(140,110);
  mouse_event(MOUSEEVENTF_LEFTDOWN,0,0,0,0);
  Sleep(150);
  SetCursorPos(850,520);
  mouse_event(MOUSEEVENTF_MOVE,0,0,0,0);
  Sleep(150);
  mouse_event(MOUSEEVENTF_LEFTUP,0,0,0,0);
  Sleep(1200);
  return 0;
}
