#include <windows.h>
#include <windowsx.h>
#include <shellapi.h>
#include <commdlg.h>
#include <gdiplus.h>
#include <string>
#include <vector>
#include <memory>
#include <algorithm>
#include <cmath>

using namespace Gdiplus;
using std::wstring;

static HINSTANCE gInst;
static ULONG_PTR gGdi;
static HWND gHost;
static HICON gIcon;
static constexpr UINT WM_TRAY=WM_APP+10;
static constexpr UINT CAPTURE_TIMER=77;

enum class Tool { Pencil, Line, Arrow, Ellipse, Rectangle, Highlighter, Text };
enum class Action { None, Pencil, Line, Arrow, Ellipse, Rectangle, Highlighter, Text, Color, Undo, Copy, Save, Close };

struct TextItem {
  wstring text;
  PointF position{};
  SizeF size{};
  Color color=Color(255,235,52,62);
};

struct UndoState {
  std::unique_ptr<Bitmap> canvas;
  std::vector<TextItem> texts;
};

struct Overlay {
  std::unique_ptr<Bitmap> screen, canvas;
  std::vector<UndoState> undo;
  std::vector<TextItem> texts;
  RECT sel{};
  POINT start{}, current{}, last{};
  bool selecting=true, dragging=false;
  Tool tool=Tool::Pencil;
  Action hover=Action::None;
  Color color=Color(255,235,52,62);
  HWND edit=nullptr;
  HFONT editFont=nullptr;
  POINT textAt{};
  int selectedText=-1;
  bool movingText=false;
  POINT textDragOffset{};
  HDC backDC=nullptr;
  HBITMAP backBitmap=nullptr;
  HGDIOBJ backOld=nullptr;
  HDC captureDC=nullptr;
  HBITMAP captureBitmap=nullptr;
  HGDIOBJ captureOld=nullptr;
  DWORD lastInvalidate=0;
};

static void DrawText(Graphics&g,const TextItem&item,REAL dx=0,REAL dy=0){FontFamily ff(L"Segoe UI");Font f(&ff,22,FontStyleBold,UnitPixel);SolidBrush br(item.color);g.DrawString(item.text.c_str(),-1,&f,PointF(item.position.X+dx,item.position.Y+dy),&br);}
static RECT TextBounds(const TextItem&item){return{(LONG)floor(item.position.X)-4,(LONG)floor(item.position.Y)-3,(LONG)ceil(item.position.X+item.size.Width)+4,(LONG)ceil(item.position.Y+item.size.Height)+3};}
static std::unique_ptr<Bitmap> Crop(Overlay*s){int w=s->sel.right-s->sel.left,h=s->sel.bottom-s->sel.top;if(w<1||h<1)return{};auto out=std::unique_ptr<Bitmap>(s->canvas->Clone(s->sel.left,s->sel.top,w,h,PixelFormat32bppARGB));Graphics g(out.get());g.SetTextRenderingHint(TextRenderingHintAntiAliasGridFit);for(const auto&item:s->texts)DrawText(g,item,(REAL)-s->sel.left,(REAL)-s->sel.top);return out;}
static bool CopyImage(HWND owner,Bitmap*b){
  HBITMAP hb{};if(b->GetHBITMAP(Color::White,&hb)!=Ok)return false;
  BITMAP bm{};if(!GetObject(hb,sizeof(bm),&bm)){DeleteObject(hb);return false;}
  BITMAPINFOHEADER bi{};bi.biSize=sizeof(bi);bi.biWidth=bm.bmWidth;bi.biHeight=bm.bmHeight;bi.biPlanes=1;bi.biBitCount=24;bi.biCompression=BI_RGB;
  DWORD stride=((bm.bmWidth*bi.biBitCount+31)/32)*4,bytes=stride*bm.bmHeight;
  HGLOBAL mem=GlobalAlloc(GMEM_MOVEABLE,sizeof(BITMAPINFOHEADER)+bytes);if(!mem){DeleteObject(hb);return false;}
  auto raw=(BYTE*)GlobalLock(mem);memcpy(raw,&bi,sizeof(bi));HDC dc=GetDC(owner);int rows=GetDIBits(dc,hb,0,bm.bmHeight,raw+sizeof(bi),(BITMAPINFO*)raw,DIB_RGB_COLORS);ReleaseDC(owner,dc);GlobalUnlock(mem);DeleteObject(hb);
  if(rows!=bm.bmHeight){GlobalFree(mem);return false;}
  bool opened=false;for(int i=0;i<10&&!opened;i++){opened=OpenClipboard(owner)!=FALSE;if(!opened)Sleep(25);}
  if(!opened){GlobalFree(mem);return false;}bool ok=EmptyClipboard()!=FALSE&&SetClipboardData(CF_DIB,mem)!=nullptr;CloseClipboard();if(!ok)GlobalFree(mem);return ok;
}
static int GetEncoderClsid(const WCHAR* mime,CLSID* clsid){UINT count=0,size=0;GetImageEncodersSize(&count,&size);if(!size)return -1;std::vector<BYTE> data(size);auto enc=(ImageCodecInfo*)data.data();if(GetImageEncoders(count,size,enc)!=Ok)return -1;for(UINT i=0;i<count;i++)if(wcscmp(enc[i].MimeType,mime)==0){*clsid=enc[i].Clsid;return (int)i;}return -1;}
static bool SaveImage(HWND owner,Bitmap*b){WCHAR path[MAX_PATH]=L"captura.png";OPENFILENAME f{};f.lStructSize=sizeof(f);f.hwndOwner=owner;f.lpstrFilter=L"Imagen PNG (*.png)\0*.png\0Todos los archivos (*.*)\0*.*\0";f.lpstrFile=path;f.nMaxFile=MAX_PATH;f.lpstrDefExt=L"png";f.Flags=OFN_OVERWRITEPROMPT|OFN_PATHMUSTEXIST;if(!GetSaveFileName(&f))return false;CLSID png{};return GetEncoderClsid(L"image/png",&png)>=0&&b->Save(path,&png,nullptr)==Ok;}

static HICON MakeIcon(){const int n=32;HDC dc=GetDC(nullptr),mem=CreateCompatibleDC(dc);HBITMAP c=CreateCompatibleBitmap(dc,n,n),mask=CreateBitmap(n,n,1,1,nullptr);auto old=SelectObject(mem,c);HBRUSH b=CreateSolidBrush(RGB(20,157,232));RECT r{0,0,n,n};FillRect(mem,&r,b);DeleteObject(b);HPEN p=CreatePen(PS_SOLID,3,RGB(255,255,255));auto op=SelectObject(mem,p);MoveToEx(mem,7,16,nullptr);LineTo(mem,25,16);MoveToEx(mem,16,7,nullptr);LineTo(mem,16,25);SelectObject(mem,op);DeleteObject(p);SelectObject(mem,old);DeleteDC(mem);ReleaseDC(nullptr,dc);ICONINFO i{TRUE,0,0,mask,c};HICON out=CreateIconIndirect(&i);DeleteObject(mask);DeleteObject(c);return out;}
static void PushUndo(Overlay*s){if(s->undo.size()>=15)s->undo.erase(s->undo.begin());UndoState state;state.canvas.reset(s->canvas->Clone(0,0,s->canvas->GetWidth(),s->canvas->GetHeight(),PixelFormat32bppARGB));state.texts=s->texts;s->undo.emplace_back(std::move(state));}
static INT I(LONG v){return static_cast<INT>(v);}
static void GL(Graphics&g,Pen*p,LONG x1,LONG y1,LONG x2,LONG y2){g.DrawLine(p,I(x1),I(y1),I(x2),I(y2));}
static void GR(Graphics&g,Pen*p,LONG x,LONG y,LONG w,LONG h){g.DrawRectangle(p,I(x),I(y),I(w),I(h));}
static void GF(Graphics&g,SolidBrush*p,LONG x,LONG y,LONG w,LONG h){g.FillRectangle(p,I(x),I(y),I(w),I(h));}
static void DrawArrow(Graphics&g,Pen&p,POINT a,POINT b){GL(g,&p,a.x,a.y,b.x,b.y);double q=atan2((double)b.y-a.y,b.x-a.x),l=18;GL(g,&p,b.x,b.y,b.x-(INT)(l*cos(q-.55)),b.y-(INT)(l*sin(q-.55)));GL(g,&p,b.x,b.y,b.x-(INT)(l*cos(q+.55)),b.y-(INT)(l*sin(q+.55)));}
static RECT Normal(POINT a,POINT b){return{std::min(a.x,b.x),std::min(a.y,b.y),std::max(a.x,b.x),std::max(a.y,b.y)};}
static bool Inside(RECT r,POINT p){return p.x>=r.left&&p.x<r.right&&p.y>=r.top&&p.y<r.bottom;}
static int HitText(Overlay*s,POINT p){for(int i=(int)s->texts.size()-1;i>=0;i--)if(Inside(TextBounds(s->texts[i]),p))return i;return -1;}
static void RequestPaint(HWND h,Overlay*s){DWORD now=GetTickCount();if(now-s->lastInvalidate>=16){s->lastInvalidate=now;InvalidateRect(h,nullptr,FALSE);}}
static void CommitText(HWND h,Overlay*s){if(!s->edit)return;WCHAR text[1024]{};GetWindowText(s->edit,text,1024);HWND e=s->edit;s->edit=nullptr;DestroyWindow(e);if(s->editFont){DeleteObject(s->editFont);s->editFont=nullptr;}if(*text){Graphics g(s->canvas.get());g.SetTextRenderingHint(TextRenderingHintAntiAliasGridFit);FontFamily ff(L"Segoe UI");Font f(&ff,22,FontStyleBold,UnitPixel);RectF measured;PointF at((REAL)s->textAt.x,(REAL)s->textAt.y);g.MeasureString(text,-1,&f,at,&measured);TextItem item;item.text=text;item.size=SizeF(std::max(1.0f,measured.Width),std::max(24.0f,measured.Height));item.position.X=std::min(at.X,(REAL)std::max(s->sel.left,s->sel.right-(LONG)ceil(item.size.Width)));item.position.Y=std::min(at.Y,(REAL)std::max(s->sel.top,s->sel.bottom-(LONG)ceil(item.size.Height)));item.color=s->color;s->texts.push_back(item);s->selectedText=(int)s->texts.size()-1;}InvalidateRect(h,nullptr,FALSE);}
static LRESULT CALLBACK EditProc(HWND h,UINT m,WPARAM w,LPARAM l){auto old=(WNDPROC)GetProp(h,L"CaptureClean.OldProc");if(m==WM_KEYDOWN&&(w==VK_RETURN||w==VK_ESCAPE)){SetFocus(GetParent(h));return 0;}if(m==WM_ERASEBKGND)return 1;return CallWindowProc(old,h,m,w,l);}
static void BeginText(HWND h,Overlay*s,POINT p){if(s->edit)CommitText(h,s);s->selectedText=-1;s->textAt=p;int width=std::clamp(I(s->sel.right-p.x),80,360);s->edit=CreateWindowEx(WS_EX_TRANSPARENT,L"EDIT",L"",WS_CHILD|WS_VISIBLE|ES_AUTOHSCROLL,p.x,p.y,width,32,h,(HMENU)(INT_PTR)500,gInst,nullptr);s->editFont=CreateFontW(-22,0,0,0,FW_BOLD,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH|FF_DONTCARE,L"Segoe UI");SendMessage(s->edit,WM_SETFONT,(WPARAM)s->editFont,TRUE);SendMessage(s->edit,EM_SETMARGINS,EC_LEFTMARGIN|EC_RIGHTMARGIN,MAKELPARAM(1,1));auto old=(WNDPROC)SetWindowLongPtr(s->edit,GWLP_WNDPROC,(LONG_PTR)EditProc);SetProp(s->edit,L"CaptureClean.OldProc",(HANDLE)old);SetFocus(s->edit);}

static constexpr int BW=34,BH=34,TOOL_N=9,ACTION_N=3,GAP=7;
static const Action TOOL_ACTIONS[TOOL_N]={Action::Pencil,Action::Line,Action::Arrow,Action::Ellipse,Action::Rectangle,Action::Highlighter,Action::Text,Action::Color,Action::Undo};
static const Action ACTION_ACTIONS[ACTION_N]={Action::Copy,Action::Save,Action::Close};
static_assert(sizeof(TOOL_ACTIONS)/sizeof(TOOL_ACTIONS[0])==TOOL_N,"La barra de herramientas debe conservar sus 9 funciones");
static_assert(sizeof(ACTION_ACTIONS)/sizeof(ACTION_ACTIONS[0])==ACTION_N,"La barra de acciones debe conservar Copiar, Guardar y Cerrar");
static RECT ToolToolbar(Overlay*s){int sw=GetSystemMetrics(SM_CXVIRTUALSCREEN),sh=GetSystemMetrics(SM_CYVIRTUALSCREEN),height=BH*TOOL_N;int x=I(s->sel.right)+GAP;if(x+BW>sw-5)x=I(s->sel.left)-BW-GAP;x=std::clamp(x,5,std::max(5,sw-BW-5));int y=std::clamp(I(s->sel.top),5,std::max(5,sh-height-5));return{x,y,x+BW,y+height};}
static RECT ActionToolbar(Overlay*s){int sw=GetSystemMetrics(SM_CXVIRTUALSCREEN),sh=GetSystemMetrics(SM_CYVIRTUALSCREEN),width=BW*ACTION_N;int x=std::clamp(I(s->sel.right)-width,5,std::max(5,sw-width-5));int y=I(s->sel.bottom)+GAP;if(y+BH>sh-5)y=I(s->sel.top)-BH-GAP;y=std::clamp(y,5,std::max(5,sh-BH-5));return{x,y,x+width,y+BH};}
static Action HitButton(Overlay*s,POINT p){RECT t=ToolToolbar(s);if(Inside(t,p))return TOOL_ACTIONS[std::clamp(I((p.y-t.top)/BH),0,TOOL_N-1)];t=ActionToolbar(s);if(Inside(t,p))return ACTION_ACTIONS[std::clamp(I((p.x-t.left)/BW),0,ACTION_N-1)];return Action::None;}
static void Tip(const WCHAR*text){NOTIFYICONDATA n{};n.cbSize=sizeof(n);n.hWnd=gHost;n.uID=1;n.uFlags=NIF_INFO;wcscpy(n.szInfoTitle,L"CaptureClean");wcsncpy(n.szInfo,text,255);n.dwInfoFlags=NIIF_INFO;Shell_NotifyIcon(NIM_MODIFY,&n);}
static void DoAction(HWND h,Overlay*s,Action a){
  if(s->edit)CommitText(h,s);
  if(a>=Action::Pencil&&a<=Action::Text){s->tool=(Tool)((int)a-1);s->selectedText=-1;InvalidateRect(h,nullptr,FALSE);return;}
  if(a==Action::Color){static COLORREF custom[16]{};CHOOSECOLOR c{};c.lStructSize=sizeof(c);c.hwndOwner=h;c.rgbResult=RGB(s->color.GetR(),s->color.GetG(),s->color.GetB());c.lpCustColors=custom;c.Flags=CC_FULLOPEN|CC_RGBINIT;if(ChooseColor(&c))s->color=Color(255,GetRValue(c.rgbResult),GetGValue(c.rgbResult),GetBValue(c.rgbResult));return;}
  if(a==Action::Undo){if(!s->undo.empty()){s->canvas=std::move(s->undo.back().canvas);s->texts=std::move(s->undo.back().texts);s->undo.pop_back();s->selectedText=-1;InvalidateRect(h,nullptr,FALSE);}return;}
  if(a==Action::Close){DestroyWindow(h);return;}
  auto img=Crop(s);if(!img)return;
  if(a==Action::Copy){if(CopyImage(h,img.get())){DestroyWindow(h);Tip(L"Captura copiada al portapapeles");}else MessageBox(h,L"No se pudo copiar la imagen. Cierra cualquier gestor del portapapeles e intenta de nuevo.",L"CaptureClean",MB_OK|MB_ICONERROR);return;}
  if(a==Action::Save){if(SaveImage(h,img.get())){DestroyWindow(h);Tip(L"Captura guardada como PNG");}return;}
}

static void RoundPath(GraphicsPath&p,RectF r,REAL radius){REAL d=radius*2;p.StartFigure();p.AddArc(r.X,r.Y,d,d,180,90);p.AddArc(r.GetRight()-d,r.Y,d,d,270,90);p.AddArc(r.GetRight()-d,r.GetBottom()-d,d,d,0,90);p.AddArc(r.X,r.GetBottom()-d,d,d,90,90);p.CloseFigure();}
static void DrawToolbarIcon(Graphics&g,Action a,RECT r,Color c){
  constexpr REAL scale=.72f;auto state=g.Save();REAL ox=(REAL)r.left+(BW-38*scale)/2,oy=(REAL)r.top+(BH-40*scale)/2;Matrix transform(scale,0,0,scale,ox,oy);g.SetTransform(&transform);Pen p(c,2.2f);p.SetStartCap(LineCapRound);p.SetEndCap(LineCapRound);p.SetLineJoin(LineJoinRound);
  auto draw=[&](){
    if(a==Action::Pencil){GraphicsPath q;PointF pts[]={{10,29},{13,22},{25,10},{29,14},{17,26}};q.AddPolygon(pts,5);SolidBrush b(Color(c.GetA(),c.GetR(),c.GetG(),c.GetB()));g.FillPath(&b,&q);Pen tip(Color(190,255,255,255),1);g.DrawLine(&tip,13,22,17,26);return;}
    if(a==Action::Line){g.DrawLine(&p,10,29,28,11);return;}
    if(a==Action::Arrow){g.DrawLine(&p,9,20,29,20);g.DrawLine(&p,29,20,23,14);g.DrawLine(&p,29,20,23,26);return;}
    if(a==Action::Ellipse){g.DrawEllipse(&p,10.0f,11.0f,18.0f,17.0f);return;}
    if(a==Action::Rectangle){g.DrawRectangle(&p,11.0f,11.0f,16.0f,18.0f);return;}
    if(a==Action::Highlighter){GraphicsPath q;PointF pts[]={{11,27},{14,19},{24,9},{30,15},{20,25}};q.AddPolygon(pts,5);SolidBrush b(c);g.FillPath(&b,&q);g.DrawLine(&p,10,29,23,29);return;}
    if(a==Action::Text){FontFamily ff(L"Segoe UI");Font f(&ff,21,FontStyleRegular,UnitPixel);SolidBrush b(c);StringFormat sf;sf.SetAlignment(StringAlignmentCenter);sf.SetLineAlignment(StringAlignmentCenter);g.DrawString(L"T",-1,&f,RectF(0,0,38,40),&sf,&b);return;}
    if(a==Action::Color){SolidBrush b(c);g.FillEllipse(&b,12.0f,12.0f,15.0f,15.0f);Pen ring(Color(150,30,30,30),1.1f);g.DrawEllipse(&ring,12.0f,12.0f,15.0f,15.0f);return;}
    if(a==Action::Undo){g.DrawArc(&p,10.0f,11.0f,19.0f,18.0f,205.0f,250.0f);g.DrawLine(&p,10,20,10,13);g.DrawLine(&p,10,20,17,19);return;}
    if(a==Action::Copy){g.DrawRectangle(&p,13.0f,10.0f,14.0f,16.0f);g.DrawLine(&p,9,14,9,30);g.DrawLine(&p,9,30,23,30);g.DrawLine(&p,9,14,13,14);return;}
    if(a==Action::Save){GraphicsPath q;q.AddRectangle(RectF(10,9,19,22));SolidBrush b(c);g.FillPath(&b,&q);SolidBrush paper(Color(235,249,247,238));g.FillRectangle(&paper,14,10,10,7);g.FillRectangle(&paper,14,23,11,6);Pen detail(Color(150,70,72,68),1);g.DrawLine(&detail,22,10,22,16);return;}
    if(a==Action::Close){g.DrawLine(&p,12,13,26,27);g.DrawLine(&p,26,13,12,27);return;}
  };draw();g.Restore(state);
}
static const WCHAR* ActionName(Action a){
  switch(a){case Action::Pencil:return L"Lápiz";case Action::Line:return L"Línea";case Action::Arrow:return L"Flecha";case Action::Ellipse:return L"Elipse";case Action::Rectangle:return L"Rectángulo";case Action::Highlighter:return L"Marcador";case Action::Text:return L"Texto";case Action::Color:return L"Color";case Action::Undo:return L"Deshacer";case Action::Copy:return L"Copiar";case Action::Save:return L"Guardar PNG";case Action::Close:return L"Cerrar";default:return L"";}
}
static bool IsActiveTool(Tool t,Action a){return (a>=Action::Pencil&&a<=Action::Text)&&(int)t==(int)a-1;}
static void PaintBar(Graphics&g,Overlay*s,RECT t,const Action* actions,int count,bool vertical){RectF box((REAL)t.left,(REAL)t.top,(REAL)(t.right-t.left),(REAL)(t.bottom-t.top)),sh(box.X+1,box.Y+3,box.Width,box.Height);GraphicsPath shadowPath,boxPath;RoundPath(shadowPath,sh,6);RoundPath(boxPath,box,6);SolidBrush shadow(Color(42,0,0,0)),bg(Color(246,249,247,238));Pen edge(Color(170,208,207,197),1);g.FillPath(&shadow,&shadowPath);g.FillPath(&bg,&boxPath);g.DrawPath(&edge,&boxPath);for(int i=0;i<count;i++){RECT r=vertical?RECT{t.left,t.top+i*BH,t.right,t.top+(i+1)*BH}:RECT{t.left+i*BW,t.top,t.left+(i+1)*BW,t.bottom};Action a=actions[i];bool active=IsActiveTool(s->tool,a),hot=s->hover==a;if(active||hot){RectF hi((REAL)r.left+3,(REAL)r.top+3,(REAL)BW-6,(REAL)BH-6);GraphicsPath hp;RoundPath(hp,hi,5);SolidBrush b(active?Color(54,0,154,221):Color(24,60,62,58));g.FillPath(&b,&hp);}Color ink=a==Action::Color?s->color:(hot&&a==Action::Close?Color(255,194,44,51):(active?Color(255,0,135,205):Color(238,72,74,69)));DrawToolbarIcon(g,a,r,ink);}}
static void PaintToolbars(Graphics&g,Overlay*s){PaintBar(g,s,ToolToolbar(s),TOOL_ACTIONS,TOOL_N,true);PaintBar(g,s,ActionToolbar(s),ACTION_ACTIONS,ACTION_N,false);if(s->hover!=Action::None){RECT t=ActionToolbar(s);REAL w=96,h=25,x=(REAL)t.left+(t.right-t.left-w)/2,y=(REAL)t.top-h-7;if(y<4)y=(REAL)t.bottom+7;RectF tip(x,y,w,h);GraphicsPath tp;RoundPath(tp,tip,6);SolidBrush tb(Color(210,25,29,33)),white(Color(245,255,255,255));g.FillPath(&tb,&tp);FontFamily ff(L"Segoe UI");Font f(&ff,12,FontStyleRegular,UnitPixel);StringFormat sf;sf.SetAlignment(StringAlignmentCenter);sf.SetLineAlignment(StringAlignmentCenter);g.DrawString(ActionName(s->hover),-1,&f,tip,&sf,&white);}}
static void PaintOverlay(Overlay*s,HDC dc){
  int W=(INT)s->screen->GetWidth(),H=(INT)s->screen->GetHeight();BitBlt(dc,0,0,W,H,s->captureDC,0,0,SRCCOPY);Graphics g(dc);g.SetSmoothingMode(SmoothingModeAntiAlias);RECT r=s->selecting?Normal(s->start,s->current):s->sel;SolidBrush dim(Color(46,13,17,20));
  GF(g,&dim,0,0,W,std::max(0,I(r.top)));GF(g,&dim,0,r.bottom,W,std::max(0,H-I(r.bottom)));GF(g,&dim,0,r.top,std::max(0,I(r.left)),std::max(0,I(r.bottom-r.top)));GF(g,&dim,r.right,r.top,std::max(0,W-I(r.right)),std::max(0,I(r.bottom-r.top)));
  if(!s->selecting){Gdiplus::Rect d(I(r.left),I(r.top),I(r.right-r.left),I(r.bottom-r.top));g.DrawImage(s->canvas.get(),d,I(r.left),I(r.top),I(r.right-r.left),I(r.bottom-r.top),UnitPixel);g.SetTextRenderingHint(TextRenderingHintAntiAliasGridFit);for(const auto&item:s->texts)DrawText(g,item);if(s->selectedText>=0&&s->selectedText<(int)s->texts.size()&&!s->edit){RECT b=TextBounds(s->texts[s->selectedText]);Pen selected(Color(220,0,153,230),1.2f);selected.SetDashStyle(DashStyleDash);GR(g,&selected,b.left,b.top,b.right-b.left,b.bottom-b.top);}}Pen border(Color(135,0,166,235),1.25f);GR(g,&border,r.left,r.top,std::max(0,I(r.right-r.left-1)),std::max(0,I(r.bottom-r.top-1)));Pen corner(Color(185,0,166,235),2.0f);int k=9;GL(g,&corner,r.left,r.top,r.left+k,r.top);GL(g,&corner,r.left,r.top,r.left,r.top+k);GL(g,&corner,r.right-k,r.top,r.right,r.top);GL(g,&corner,r.right,r.top,r.right,r.top+k);GL(g,&corner,r.left,r.bottom-k,r.left,r.bottom);GL(g,&corner,r.left,r.bottom,r.left+k,r.bottom);GL(g,&corner,r.right-k,r.bottom,r.right,r.bottom);GL(g,&corner,r.right,r.bottom-k,r.right,r.bottom);
  if(s->selecting&&r.right-r.left>2){wstring z=std::to_wstring(r.right-r.left)+L" × "+std::to_wstring(r.bottom-r.top);FontFamily ff(L"Segoe UI");Font f(&ff,12,FontStyleRegular,UnitPixel);RectF box((REAL)r.left,(REAL)std::max(3,I(r.top-27)),112,23);GraphicsPath bp;RoundPath(bp,box,6);SolidBrush bb(Color(188,25,29,33)),wt(Color(245,255,255,255));g.FillPath(&bb,&bp);StringFormat sf;sf.SetAlignment(StringAlignmentCenter);sf.SetLineAlignment(StringAlignmentCenter);g.DrawString(z.c_str(),-1,&f,box,&sf,&wt);}
  if(!s->selecting){PaintToolbars(g,s);if(s->dragging&&s->tool!=Tool::Pencil&&s->tool!=Tool::Highlighter){Pen p(s->color,3);p.SetStartCap(LineCapRound);p.SetEndCap(LineCapRound);if(s->tool==Tool::Line)GL(g,&p,s->start.x,s->start.y,s->current.x,s->current.y);else if(s->tool==Tool::Rectangle){auto q=Normal(s->start,s->current);GR(g,&p,q.left,q.top,q.right-q.left,q.bottom-q.top);}else if(s->tool==Tool::Ellipse){auto q=Normal(s->start,s->current);g.DrawEllipse(&p,I(q.left),I(q.top),I(q.right-q.left),I(q.bottom-q.top));}else if(s->tool==Tool::Arrow)DrawArrow(g,p,s->start,s->current);}}
}

static LRESULT CALLBACK OverlayProc(HWND h,UINT m,WPARAM w,LPARAM l){
  auto s=(Overlay*)GetWindowLongPtr(h,GWLP_USERDATA);
  if(m==WM_CREATE){s=(Overlay*)((CREATESTRUCT*)l)->lpCreateParams;SetWindowLongPtr(h,GWLP_USERDATA,(LONG_PTR)s);HDC dc=GetDC(h);s->backDC=CreateCompatibleDC(dc);s->backBitmap=CreateCompatibleBitmap(dc,(INT)s->screen->GetWidth(),(INT)s->screen->GetHeight());s->backOld=SelectObject(s->backDC,s->backBitmap);ReleaseDC(h,dc);SetFocus(h);return 0;}
  if(!s)return DefWindowProc(h,m,w,l);
  POINT p{GET_X_LPARAM(l),GET_Y_LPARAM(l)};
  switch(m){
    case WM_COMMAND:if(LOWORD(w)==500){if(HIWORD(w)==EN_KILLFOCUS)CommitText(h,s);else if(HIWORD(w)==EN_UPDATE&&s->edit){RECT editRect;GetWindowRect(s->edit,&editRect);MapWindowPoints(HWND_DESKTOP,h,(POINT*)&editRect,2);RedrawWindow(h,&editRect,nullptr,RDW_INVALIDATE|RDW_UPDATENOW);RedrawWindow(s->edit,nullptr,nullptr,RDW_INVALIDATE|RDW_UPDATENOW);}}return 0;
    case WM_CTLCOLOREDIT:if((HWND)l==s->edit){SetBkMode((HDC)w,TRANSPARENT);SetTextColor((HDC)w,RGB(s->color.GetR(),s->color.GetG(),s->color.GetB()));return (LRESULT)GetStockObject(HOLLOW_BRUSH);}break;
    case WM_KEYDOWN:
      if(w==VK_ESCAPE||(w=='X'&&(GetKeyState(VK_CONTROL)&0x8000)))DestroyWindow(h);
      else if(w=='C'&&(GetKeyState(VK_CONTROL)&0x8000)&&!s->selecting)DoAction(h,s,Action::Copy);
      else if(w=='S'&&(GetKeyState(VK_CONTROL)&0x8000)&&!s->selecting)DoAction(h,s,Action::Save);
      else if(w=='A'&&(GetKeyState(VK_CONTROL)&0x8000)){s->sel={0,0,(LONG)s->screen->GetWidth(),(LONG)s->screen->GetHeight()};s->selecting=false;InvalidateRect(h,nullptr,FALSE);}
      else if(w=='Z'&&(GetKeyState(VK_CONTROL)&0x8000))DoAction(h,s,Action::Undo);
      return 0;
    case WM_LBUTTONDOWN:
      if(!s->selecting){if(s->edit)CommitText(h,s);Action a=HitButton(s,p);if(a!=Action::None){DoAction(h,s,a);return 0;}if(!Inside(s->sel,p))return 0;int hit=s->tool==Tool::Text?HitText(s,p):-1;if(hit>=0){PushUndo(s);s->selectedText=hit;s->movingText=true;s->textDragOffset={p.x-(LONG)s->texts[hit].position.X,p.y-(LONG)s->texts[hit].position.Y};SetCapture(h);InvalidateRect(h,nullptr,FALSE);return 0;}s->selectedText=-1;PushUndo(s);}s->dragging=true;s->start=s->current=s->last=p;SetCapture(h);return 0;
    case WM_MOUSEMOVE:
      s->current=p;if(s->movingText&&s->selectedText>=0&&s->selectedText<(int)s->texts.size()){auto&item=s->texts[s->selectedText];REAL nx=(REAL)(p.x-s->textDragOffset.x),ny=(REAL)(p.y-s->textDragOffset.y);REAL maxX=(REAL)std::max(s->sel.left,s->sel.right-(LONG)ceil(item.size.Width)),maxY=(REAL)std::max(s->sel.top,s->sel.bottom-(LONG)ceil(item.size.Height));item.position.X=std::clamp(nx,(REAL)s->sel.left,maxX);item.position.Y=std::clamp(ny,(REAL)s->sel.top,maxY);RequestPaint(h,s);return 0;}if(!s->selecting&&!s->dragging){Action over=HitButton(s,p);if(over!=s->hover){s->hover=over;RequestPaint(h,s);}}if(s->dragging&&!s->selecting&&(s->tool==Tool::Pencil||s->tool==Tool::Highlighter)&&Inside(s->sel,p)){Graphics g(s->canvas.get());Color ink=s->tool==Tool::Highlighter?Color(92,s->color.GetR(),s->color.GetG(),s->color.GetB()):s->color;Pen pen(ink,s->tool==Tool::Highlighter?11.0f:3.0f);pen.SetStartCap(LineCapRound);pen.SetEndCap(LineCapRound);GL(g,&pen,s->last.x,s->last.y,p.x,p.y);s->last=p;}if(s->dragging)RequestPaint(h,s);return 0;
    case WM_LBUTTONUP:
      if(s->movingText){s->movingText=false;ReleaseCapture();InvalidateRect(h,nullptr,FALSE);return 0;}
      if(!s->dragging)return 0;
      s->dragging=false;ReleaseCapture();s->current=p;
      if(s->selecting){s->sel=Normal(s->start,s->current);if(s->sel.right-s->sel.left<12||s->sel.bottom-s->sel.top<12){DestroyWindow(h);return 0;}s->selecting=false;if(GetKeyState(VK_CONTROL)&0x8000){DoAction(h,s,Action::Copy);return 0;}}
      else{Graphics g(s->canvas.get());Pen pen(s->color,3);pen.SetStartCap(LineCapRound);pen.SetEndCap(LineCapRound);POINT e=p;e.x=std::clamp(e.x,s->sel.left,s->sel.right-1);e.y=std::clamp(e.y,s->sel.top,s->sel.bottom-1);if(s->tool==Tool::Line)GL(g,&pen,s->start.x,s->start.y,e.x,e.y);else if(s->tool==Tool::Rectangle){auto q=Normal(s->start,e);GR(g,&pen,q.left,q.top,q.right-q.left,q.bottom-q.top);}else if(s->tool==Tool::Ellipse){auto q=Normal(s->start,e);g.DrawEllipse(&pen,I(q.left),I(q.top),I(q.right-q.left),I(q.bottom-q.top));}else if(s->tool==Tool::Arrow)DrawArrow(g,pen,s->start,e);else if(s->tool==Tool::Text)BeginText(h,s,s->start);}InvalidateRect(h,nullptr,FALSE);return 0;
    case WM_PAINT:{PAINTSTRUCT ps;auto dc=BeginPaint(h,&ps);PaintOverlay(s,s->backDC);BitBlt(dc,ps.rcPaint.left,ps.rcPaint.top,ps.rcPaint.right-ps.rcPaint.left,ps.rcPaint.bottom-ps.rcPaint.top,s->backDC,ps.rcPaint.left,ps.rcPaint.top,SRCCOPY);EndPaint(h,&ps);return 0;}
    case WM_ERASEBKGND:return 1;
    case WM_SETCURSOR:{POINT cursor;GetCursorPos(&cursor);ScreenToClient(h,&cursor);bool overMovableText=s->tool==Tool::Text&&HitText(s,cursor)>=0;LPCWSTR shape=s->selecting?IDC_CROSS:(overMovableText?IDC_SIZEALL:(s->tool==Tool::Text?IDC_IBEAM:IDC_ARROW));SetCursor(LoadCursor(nullptr,shape));return TRUE;}
    case WM_DESTROY:if(s->editFont)DeleteObject(s->editFont);if(s->backDC){SelectObject(s->backDC,s->backOld);DeleteObject(s->backBitmap);DeleteDC(s->backDC);}s->screen.reset();s->canvas.reset();if(s->captureDC){SelectObject(s->captureDC,s->captureOld);DeleteObject(s->captureBitmap);DeleteDC(s->captureDC);}delete s;return 0;
  }
  return DefWindowProc(h,m,w,l);
}

static BOOL CALLBACK MonitorBounds(HMONITOR,HDC,LPRECT r,LPARAM data){auto bounds=(RECT*)data;if(bounds->right<=bounds->left){*bounds=*r;}else{bounds->left=std::min(bounds->left,r->left);bounds->top=std::min(bounds->top,r->top);bounds->right=std::max(bounds->right,r->right);bounds->bottom=std::max(bounds->bottom,r->bottom);}return TRUE;}
static RECT VirtualDesktop(){RECT bounds{0,0,0,0};EnumDisplayMonitors(nullptr,nullptr,MonitorBounds,(LPARAM)&bounds);if(bounds.right<=bounds.left||bounds.bottom<=bounds.top)bounds={GetSystemMetrics(SM_XVIRTUALSCREEN),GetSystemMetrics(SM_YVIRTUALSCREEN),GetSystemMetrics(SM_XVIRTUALSCREEN)+GetSystemMetrics(SM_CXVIRTUALSCREEN),GetSystemMetrics(SM_YVIRTUALSCREEN)+GetSystemMetrics(SM_CYVIRTUALSCREEN)};return bounds;}
static void Capture(){
  if(FindWindow(L"CaptureCleanOverlay",nullptr))return;
  auto previous=SetThreadDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);RECT desktop=VirtualDesktop();int x=desktop.left,y=desktop.top,w=desktop.right-desktop.left,h=desktop.bottom-desktop.top;if(w<1||h<1){if(previous)SetThreadDpiAwarenessContext(previous);return;}
  auto s=new Overlay;HDC src=GetDC(nullptr);s->captureDC=CreateCompatibleDC(src);s->captureBitmap=CreateCompatibleBitmap(src,w,h);s->captureOld=SelectObject(s->captureDC,s->captureBitmap);BitBlt(s->captureDC,0,0,w,h,src,x,y,SRCCOPY|CAPTUREBLT);ReleaseDC(nullptr,src);s->screen=std::make_unique<Bitmap>(s->captureBitmap,nullptr);s->canvas.reset(s->screen->Clone(0,0,w,h,PixelFormat32bppARGB));
  HWND o=CreateWindowEx(WS_EX_TOPMOST|WS_EX_TOOLWINDOW,L"CaptureCleanOverlay",nullptr,WS_POPUP|WS_VISIBLE,x,y,w,h,nullptr,nullptr,gInst,s);if(previous)SetThreadDpiAwarenessContext(previous);SetForegroundWindow(o);SetFocus(o);
}
static void RequestCapture(HWND h){if(FindWindow(L"CaptureCleanOverlay",nullptr))return;KillTimer(h,CAPTURE_TIMER);SetTimer(h,CAPTURE_TIMER,60,nullptr);}
static void AddTray(HWND h){NOTIFYICONDATA n{};n.cbSize=sizeof(n);n.hWnd=h;n.uID=1;n.uFlags=NIF_MESSAGE|NIF_ICON|NIF_TIP;n.uCallbackMessage=WM_TRAY;n.hIcon=gIcon;wcscpy(n.szTip,L"CaptureClean 2.10.1 — Impr Pant para capturar");Shell_NotifyIcon(NIM_ADD,&n);}
static LRESULT CALLBACK HostProc(HWND h,UINT m,WPARAM w,LPARAM l){
  switch(m){
    case WM_CREATE:AddTray(h);RegisterHotKey(h,1,MOD_NOREPEAT,VK_SNAPSHOT);RegisterHotKey(h,2,MOD_CONTROL|MOD_SHIFT|MOD_NOREPEAT,'S');return 0;
    case WM_HOTKEY:RequestCapture(h);return 0;
    case WM_TIMER:if(w==CAPTURE_TIMER){KillTimer(h,CAPTURE_TIMER);Capture();}return 0;
    case WM_TRAY:
      if(l==WM_LBUTTONDBLCLK)RequestCapture(h);else if(l==WM_RBUTTONUP){POINT p;GetCursorPos(&p);HMENU q=CreatePopupMenu();AppendMenu(q,MF_STRING|MF_GRAYED,0,L"CaptureClean 2.10.1");AppendMenu(q,MF_SEPARATOR,0,nullptr);AppendMenu(q,MF_STRING,1,L"Nueva captura");AppendMenu(q,MF_SEPARATOR,0,nullptr);AppendMenu(q,MF_STRING,2,L"Salir");SetForegroundWindow(h);int c=TrackPopupMenu(q,TPM_RETURNCMD|TPM_RIGHTBUTTON,p.x,p.y,0,h,nullptr);DestroyMenu(q);if(c==1)RequestCapture(h);else if(c==2)DestroyWindow(h);}return 0;
    case WM_DESTROY:{UnregisterHotKey(h,1);UnregisterHotKey(h,2);NOTIFYICONDATA n{};n.cbSize=sizeof(n);n.hWnd=h;n.uID=1;Shell_NotifyIcon(NIM_DELETE,&n);PostQuitMessage(0);return 0;}
  }
  return DefWindowProc(h,m,w,l);
}

int WINAPI wWinMain(HINSTANCE hi,HINSTANCE,LPWSTR,int){
  gInst=hi;SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);HANDLE mutex=CreateMutex(nullptr,TRUE,L"Local\\CaptureCleanV2.SingleInstance");if(GetLastError()==ERROR_ALREADY_EXISTS){if(auto h=FindWindow(L"CaptureCleanHost",nullptr))PostMessage(h,WM_HOTKEY,1,0);return 0;}
  GdiplusStartupInput in;GdiplusStartup(&gGdi,&in,nullptr);gIcon=(HICON)LoadImage(hi,MAKEINTRESOURCE(101),IMAGE_ICON,32,32,LR_DEFAULTCOLOR);if(!gIcon)gIcon=MakeIcon();WNDCLASS c{};c.hInstance=hi;c.hIcon=gIcon;c.hCursor=LoadCursor(nullptr,IDC_ARROW);c.lpfnWndProc=HostProc;c.lpszClassName=L"CaptureCleanHost";RegisterClass(&c);c.lpfnWndProc=OverlayProc;c.lpszClassName=L"CaptureCleanOverlay";RegisterClass(&c);gHost=CreateWindow(L"CaptureCleanHost",L"CaptureClean",0,0,0,0,0,HWND_MESSAGE,nullptr,hi,nullptr);
  MSG msg;while(GetMessage(&msg,nullptr,0,0)>0){TranslateMessage(&msg);DispatchMessage(&msg);}DestroyIcon(gIcon);GdiplusShutdown(gGdi);ReleaseMutex(mutex);CloseHandle(mutex);return 0;
}
