#pragma once
#include <Windows.h>
#include <algorithm>
#include <atomic>
#include <cmath>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace LS {
struct HudDerivation {std::string label,key;bool available=true,buffered=false;bool operator==(const HudDerivation&)const=default;};
struct HudSnapshot {
 bool active=false,autoDemo=false,paused=false,aiming=false,resourceValid=false,targetValid=false;
 std::string modeLabel,motionLabel,detail,feedback,chapter,resourceLabel,targetName,exitKey="Esc";
 float resource=0,resourceMax=1,charge=0,chargeMax=1,targetHp=0,targetMaxHp=1;
 uint64_t hits=0;double totalDamage=0,lastDamage=0;
 std::vector<HudDerivation> derivations;
 bool operator==(const HudSnapshot&)const=default;
};
struct HudSettings {
 bool enabled=true,showDerivations=true,showTarget=true,showCrosshair=true,editing=false;
 float scale=1,anchorX=0,anchorY=.08f;int margin=24;
 std::string layout="teaching",theme="forest";
 bool operator==(const HudSettings&)const=default;
};
struct HudGeometry {int left=0,top=0,width=0,height=0,dpi=96;};
// Pure layout helper, also exercised at 16:9, wide, small and high DPI sizes.
inline HudGeometry CalculateHudGeometry(RECT client,const HudSettings& settings,int desiredHeight,int dpi=96){
 const float ratio=std::clamp(std::isfinite(settings.scale)?settings.scale:1.f,.75f,2.f)*std::clamp(dpi,72,384)/96.f;
 const int clientWidth=std::max(1,int(client.right-client.left)),clientHeight=std::max(1,int(client.bottom-client.top));
 const int margin=std::min({int(std::clamp(settings.margin,0,120)*dpi/96.f),std::max(0,(clientWidth-1)/2),std::max(0,(clientHeight-1)/2)});
 const int width=std::clamp(int(440*ratio),1,std::max(1,clientWidth-margin*2));
 const int height=std::clamp(int(desiredHeight*ratio),1,std::max(1,clientHeight-margin*2));
 const float x=std::clamp(std::isfinite(settings.anchorX)?settings.anchorX:0.f,0.f,1.f),y=std::clamp(std::isfinite(settings.anchorY)?settings.anchorY:.08f,0.f,1.f);
 return{client.left+margin+int((clientWidth-margin*2-width)*x),client.top+margin+int((clientHeight-margin*2-height)*y),width,height,dpi};
}
class PracticeHud {
 // Window messages can re-enter synchronously during ReleaseCapture and painting.
 std::recursive_mutex gate_;std::thread thread_;std::atomic<DWORD> threadId_{0};std::atomic<HWND> window_{nullptr},crosshair_{nullptr};
 std::condition_variable_any startedEvent_;bool started_=false,legacy_=false,visible_=false,layoutChanged_=false;
 HudSnapshot snapshot_;HudSettings settings_;std::wstring legacyText_;HFONT bodyFont_=nullptr,titleFont_=nullptr;
 HudGeometry geometry_;RECT gameClient_{};int fontPixels_=0;bool dragging_=false;POINT dragStart_{};POINT dragOrigin_{};
 static constexpr UINT RefreshMessage=WM_APP+37;
 static std::wstring Utf16(const std::string& text){if(text.empty())return {};const int length=int(std::min<size_t>(text.size(),4096));const int size=MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,text.data(),length,nullptr,0);if(size<=0)return L"Invalid HUD text";std::wstring out(size,L'\0');MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,text.data(),length,out.data(),size);return out;}
 static float Fraction(float value,float maximum){return std::isfinite(value)&&std::isfinite(maximum)&&maximum>0?std::clamp(value/maximum,0.f,1.f):0.f;}
 static std::string Amount(double value){return std::isfinite(value)?std::to_string(int64_t(std::clamp(value,0.,1.e12))):"0";}
 static int WindowDpi(HWND window){using Getter=UINT(WINAPI*)(HWND);static const auto getter=reinterpret_cast<Getter>(GetProcAddress(GetModuleHandleW(L"user32.dll"),"GetDpiForWindow"));return getter?int(getter(window)):96;}
 static void Fill(HDC dc,RECT area,COLORREF colour){auto brush=CreateSolidBrush(colour);FillRect(dc,&area,brush);DeleteObject(brush);}
 static void Text(HDC dc,const std::wstring& text,RECT area,COLORREF colour,UINT flags=DT_LEFT|DT_TOP|DT_NOPREFIX|DT_END_ELLIPSIS){SetTextColor(dc,colour);SetBkMode(dc,TRANSPARENT);DrawTextW(dc,text.data(),int(text.size()),&area,flags);}
 int DesiredHeight()const {if(legacy_)return 136;int h=96;if(!snapshot_.chapter.empty())h+=25;if(settings_.layout=="compact")return h;if(snapshot_.resourceValid)h+=39;if(settings_.showDerivations&&!snapshot_.derivations.empty())h+=int(std::min<size_t>(snapshot_.derivations.size(),4))*25+18;if(settings_.showTarget&&snapshot_.targetValid)h+=76;if(!snapshot_.feedback.empty()||!snapshot_.detail.empty())h+=35;if(settings_.editing)h+=27;return std::min(440,h);}
 void Fonts(int dpi){const int pixels=std::max(10,int(15*settings_.scale*dpi/96.f));if(pixels==fontPixels_)return;if(bodyFont_)DeleteObject(bodyFont_);if(titleFont_)DeleteObject(titleFont_);bodyFont_=CreateFontW(-pixels,0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH,L"Segoe UI");titleFont_=CreateFontW(-int(pixels*1.25f),0,0,0,FW_SEMIBOLD,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH,L"Segoe UI");fontPixels_=pixels;}
 void Refresh(HWND window){std::lock_guard lock(gate_);const auto foreground=GetForegroundWindow();DWORD process=0;GetWindowThreadProcessId(foreground,&process);RECT client{};POINT origin{};const bool show=visible_&&(settings_.enabled||settings_.editing)&&(settings_.layout!="photo"||settings_.editing)&&process==GetCurrentProcessId()&&foreground&&foreground!=window&&foreground!=crosshair_.load()&&!IsIconic(foreground)&&GetClientRect(foreground,&client)&&ClientToScreen(foreground,&origin);
  if(!show){ShowWindow(window,SW_HIDE);if(crosshair_)ShowWindow(crosshair_,SW_HIDE);if(dragging_){ReleaseCapture();dragging_=false;}return;}
  OffsetRect(&client,origin.x,origin.y);gameClient_=client;const int dpi=WindowDpi(foreground);const auto next=CalculateHudGeometry(client,settings_,DesiredHeight(),dpi);const bool changed=next.left!=geometry_.left||next.top!=geometry_.top||next.width!=geometry_.width||next.height!=geometry_.height||next.dpi!=geometry_.dpi;geometry_=next;Fonts(dpi);
  const LONG_PTR style=WS_EX_TOOLWINDOW|WS_EX_LAYERED|WS_EX_NOACTIVATE|(settings_.editing?0:WS_EX_TRANSPARENT);if(GetWindowLongPtrW(window,GWL_EXSTYLE)!=style)SetWindowLongPtrW(window,GWL_EXSTYLE,style);
  if(changed||!IsWindowVisible(window))SetWindowPos(window,HWND_TOPMOST,next.left,next.top,next.width,next.height,SWP_NOACTIVATE|SWP_SHOWWINDOW);
  if(changed)InvalidateRect(window,nullptr,FALSE);
  auto aim=crosshair_.load();if(aim){if(settings_.showCrosshair&&snapshot_.aiming&&!settings_.editing){const int size=std::max(30,int(76*settings_.scale*dpi/96.f));SetWindowPos(aim,HWND_TOPMOST,(client.left+client.right-size)/2,(client.top+client.bottom-size)/2,size,size,SWP_NOACTIVATE|SWP_SHOWWINDOW);}else ShowWindow(aim,SW_HIDE);}
 }
 void Paint(HDC dc,RECT area,bool crosshair){std::lock_guard lock(gate_);const COLORREF accent=settings_.theme=="ember"?RGB(244,192,141):settings_.theme=="tide"?RGB(164,221,232):RGB(205,235,148),ink=RGB(242,245,238),muted=RGB(184,195,186);if(crosshair){Fill(dc,area,RGB(1,2,3));const int cx=(area.right-area.left)/2,cy=(area.bottom-area.top)/2,r=std::max(9,int(cx*.64f));auto pen=CreatePen(PS_SOLID,2,ink);auto old=SelectObject(dc,pen);auto brush=SelectObject(dc,GetStockObject(NULL_BRUSH));MoveToEx(dc,cx-9,cy,nullptr);LineTo(dc,cx-3,cy);MoveToEx(dc,cx+4,cy,nullptr);LineTo(dc,cx+10,cy);MoveToEx(dc,cx,cy-9,nullptr);LineTo(dc,cx,cy-3);MoveToEx(dc,cx,cy+4,nullptr);LineTo(dc,cx,cy+10);SelectObject(dc,old);DeleteObject(pen);if(snapshot_.chargeMax>0&&snapshot_.charge>0){pen=CreatePen(PS_SOLID,3,accent);old=SelectObject(dc,pen);const float angle=-1.5707963f+Fraction(snapshot_.charge,snapshot_.chargeMax)*6.2831853f;Arc(dc,cx-r,cy-r,cx+r,cy+r,cx,cy-r,cx+int(std::cos(angle)*r),cy+int(std::sin(angle)*r));SelectObject(dc,old);DeleteObject(pen);}SelectObject(dc,brush);return;}
  Fill(dc,area,RGB(17,23,21));RECT stripe{0,0,std::max(2,int(3*settings_.scale)),area.bottom};Fill(dc,stripe,accent);const float scale=settings_.scale*geometry_.dpi/96.f;const int pad=std::max(8,int(16*scale));RECT inner=area;InflateRect(&inner,-pad,-pad);auto old=SelectObject(dc,bodyFont_);
  if(legacy_){Text(dc,legacyText_,inner,ink,DT_LEFT|DT_TOP|DT_NOPREFIX|DT_WORDBREAK);SelectObject(dc,old);return;}
  int y=inner.top;const int line=std::max(16,int(23*scale));auto draw=[&](const std::string& value,COLORREF colour,int height=0){RECT row{inner.left,y,inner.right,std::min(int(inner.bottom),y+(height?height:line))};Text(dc,Utf16(value),row,colour);y+=height?height:line;};
  draw(snapshot_.modeLabel+" · "+(snapshot_.paused?"已暂停":snapshot_.autoDemo?"自动演示":"手动")+"  ·  "+snapshot_.exitKey+" 结束",accent);SelectObject(dc,titleFont_);draw(snapshot_.motionLabel.empty()?"等待招式":snapshot_.motionLabel,ink,int(line*1.3f));SelectObject(dc,bodyFont_);if(!snapshot_.chapter.empty())draw(snapshot_.chapter,muted);
  if(settings_.layout!="compact"){
   if(snapshot_.resourceValid){draw("本地 "+snapshot_.resourceLabel+"  "+Amount(snapshot_.resource)+" / "+Amount(snapshot_.resourceMax),muted);RECT bar{inner.left,y,inner.right,y+std::max(3,int(5*scale))};Fill(dc,bar,RGB(56,70,60));bar.right=bar.left+int((bar.right-bar.left)*Fraction(snapshot_.resource,snapshot_.resourceMax));Fill(dc,bar,accent);y+=int(14*scale);}
   if(settings_.showDerivations&&!snapshot_.derivations.empty()){y+=int(5*scale);for(size_t i=0;i<std::min<size_t>(snapshot_.derivations.size(),4);++i){const auto& d=snapshot_.derivations[i];draw((d.buffered?"已缓存  ":d.available?"可派生  ":"等待窗口  ")+d.label+"  ["+d.key+"]",d.buffered?accent:d.available?ink:muted);}}
   if(settings_.showTarget&&snapshot_.targetValid){y+=int(9*scale);draw(snapshot_.targetName+" · 模拟 HP "+Amount(snapshot_.targetHp)+" / "+Amount(snapshot_.targetMaxHp),ink);RECT bar{inner.left,y,inner.right,y+std::max(3,int(5*scale))};Fill(dc,bar,RGB(56,70,60));bar.right=bar.left+int((bar.right-bar.left)*Fraction(snapshot_.targetHp,snapshot_.targetMaxHp));Fill(dc,bar,accent);y+=int(13*scale);draw(snapshot_.targetHp<=0?"练习完成":"模拟命中 "+std::to_string(snapshot_.hits)+" · 累计 "+Amount(snapshot_.totalDamage),muted);}
   if(!snapshot_.feedback.empty())draw(snapshot_.feedback,accent);else if(!snapshot_.detail.empty())draw(snapshot_.detail,muted);
  }
  if(settings_.editing)draw("布局编辑 · 拖动位置 · Esc 退出编辑",accent);SelectObject(dc,old);
 }
 static LRESULT CALLBACK Procedure(HWND window,UINT message,WPARAM wparam,LPARAM lparam){auto self=reinterpret_cast<PracticeHud*>(GetWindowLongPtrW(window,GWLP_USERDATA));if(message==WM_NCCREATE){self=static_cast<PracticeHud*>(reinterpret_cast<CREATESTRUCTW*>(lparam)->lpCreateParams);SetWindowLongPtrW(window,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(self));}if(!self)return DefWindowProcW(window,message,wparam,lparam);const bool aim=window==self->crosshair_.load();switch(message){
  case WM_NCHITTEST:{std::lock_guard lock(self->gate_);return !aim&&self->settings_.editing?HTCLIENT:HTTRANSPARENT;}
  case WM_MOUSEACTIVATE:return MA_NOACTIVATE;
  case WM_ERASEBKGND:return 1;
  case RefreshMessage:case WM_TIMER:if(!aim)self->Refresh(window);return 0;
  case WM_DPICHANGED:if(!aim)self->Refresh(window);return 0;
  case WM_LBUTTONDOWN:{std::lock_guard lock(self->gate_);if(!aim&&self->settings_.editing){GetCursorPos(&self->dragStart_);self->dragOrigin_={self->geometry_.left,self->geometry_.top};self->dragging_=true;SetCapture(window);}return 0;}
  case WM_MOUSEMOVE:{std::lock_guard lock(self->gate_);if(self->dragging_){POINT p{};GetCursorPos(&p);const int margin=int(self->settings_.margin*self->geometry_.dpi/96.f),left=self->gameClient_.left+margin,top=self->gameClient_.top+margin;const int dx=std::max(0,int(self->gameClient_.right-self->gameClient_.left)-2*margin-self->geometry_.width),dy=std::max(0,int(self->gameClient_.bottom-self->gameClient_.top)-2*margin-self->geometry_.height);self->settings_.anchorX=dx?std::clamp(float(self->dragOrigin_.x+p.x-self->dragStart_.x-left)/dx,0.f,1.f):0;self->settings_.anchorY=dy?std::clamp(float(self->dragOrigin_.y+p.y-self->dragStart_.y-top)/dy,0.f,1.f):0;PostMessageW(window,RefreshMessage,0,0);}return 0;}
  case WM_LBUTTONUP:{std::lock_guard lock(self->gate_);if(self->dragging_){self->dragging_=false;self->layoutChanged_=true;ReleaseCapture();}return 0;}
  case WM_CAPTURECHANGED:{std::lock_guard lock(self->gate_);self->dragging_=false;return 0;}
  case WM_PAINT:{PAINTSTRUCT paint{};auto dc=BeginPaint(window,&paint);RECT area{};GetClientRect(window,&area);auto memory=CreateCompatibleDC(dc);auto bitmap=CreateCompatibleBitmap(dc,std::max(1,int(area.right)),std::max(1,int(area.bottom)));auto old=SelectObject(memory,bitmap);self->Paint(memory,area,aim);BitBlt(dc,0,0,area.right,area.bottom,memory,0,0,SRCCOPY);SelectObject(memory,old);DeleteObject(bitmap);DeleteDC(memory);EndPaint(window,&paint);return 0;}
  case WM_CLOSE:DestroyWindow(window);return 0;
  case WM_DESTROY:if(!aim)PostQuitMessage(0);return 0;
 }return DefWindowProcW(window,message,wparam,lparam);}
public:
 ~PracticeHud(){Stop();}
 void Start(){
  if(thread_.joinable())return;
  {std::lock_guard lock(gate_);started_=false;}
  thread_=std::thread([this]{
   threadId_=GetCurrentThreadId();auto instance=GetModuleHandleW(nullptr);
   constexpr wchar_t name[]=L"MHE.LocalHud.v2";
   WNDCLASSEXW info{sizeof(WNDCLASSEXW)};info.lpfnWndProc=Procedure;info.hInstance=instance;info.lpszClassName=name;RegisterClassExW(&info);
   const DWORD style=WS_EX_TOOLWINDOW|WS_EX_LAYERED|WS_EX_TRANSPARENT|WS_EX_NOACTIVATE;
   auto window=CreateWindowExW(style,name,L"Monster Hunter Endfield local HUD",WS_POPUP,0,0,440,136,nullptr,nullptr,instance,this);window_=window;
   auto aim=CreateWindowExW(style,name,L"Monster Hunter Endfield local aim",WS_POPUP,0,0,76,76,nullptr,nullptr,instance,this);crosshair_=aim;
   MSG initial{};PeekMessageW(&initial,nullptr,0,0,PM_NOREMOVE);
   if(window){SetLayeredWindowAttributes(window,0,238,LWA_ALPHA);SetTimer(window,1,100,nullptr);PostMessageW(window,RefreshMessage,0,0);}
   if(aim)SetLayeredWindowAttributes(aim,RGB(1,2,3),0,LWA_COLORKEY);
   // Signal only after both creation attempts and timer/layer initialization.
   {std::lock_guard lock(gate_);started_=true;}startedEvent_.notify_all();
   if(window){MSG message{};while(GetMessageW(&message,nullptr,0,0)>0){TranslateMessage(&message);DispatchMessageW(&message);}}
   // Both creations can fail independently; always clean the surviving one.
   if(IsWindow(window))DestroyWindow(window);if(IsWindow(aim))DestroyWindow(aim);
   window_=nullptr;crosshair_=nullptr;
   if(bodyFont_)DeleteObject(bodyFont_);if(titleFont_)DeleteObject(titleFont_);
   bodyFont_=titleFont_=nullptr;fontPixels_=0;UnregisterClassW(name,instance);threadId_=0;
  });
 }

 void Update(const HudSnapshot& snapshot,const HudSettings& settings){bool changed=false;{std::lock_guard lock(gate_);HudSettings next=settings;next.scale=std::clamp(std::isfinite(next.scale)?next.scale:1.f,.75f,2.f);next.anchorX=std::clamp(std::isfinite(next.anchorX)?next.anchorX:0.f,0.f,1.f);next.anchorY=std::clamp(std::isfinite(next.anchorY)?next.anchorY:.08f,0.f,1.f);if(dragging_||layoutChanged_){next.anchorX=settings_.anchorX;next.anchorY=settings_.anchorY;}changed=legacy_||snapshot_!=snapshot||settings_!=next;legacy_=false;snapshot_=snapshot;settings_=next;visible_=snapshot.active||settings.editing;}if(changed){if(window_){PostMessageW(window_,RefreshMessage,0,0);InvalidateRect(window_,nullptr,FALSE);}if(crosshair_)InvalidateRect(crosshair_,nullptr,FALSE);}}
 void Update(bool visible,const std::string& text){{std::lock_guard lock(gate_);legacy_=true;visible_=visible;settings_=HudSettings{};legacyText_=Utf16(text);}if(window_){PostMessageW(window_,RefreshMessage,0,0);InvalidateRect(window_,nullptr,FALSE);}}
 bool TakeLayoutEdit(float& x,float& y){std::lock_guard lock(gate_);if(!layoutChanged_)return false;x=settings_.anchorX;y=settings_.anchorY;layoutChanged_=false;return true;}
 bool Editing(){std::lock_guard lock(gate_);return settings_.editing;}
 HWND Window()const{return window_.load();}HWND CrosshairWindow()const{return crosshair_.load();}
 // The body handle is published before the second window is created. Callers
 // needing both windows must wait for initialization, never infer readiness
 // from the first handle or a scheduling delay.
 bool WaitUntilReady(std::chrono::milliseconds timeout=std::chrono::seconds(5)){
  std::unique_lock lock(gate_);
  return startedEvent_.wait_for(lock,timeout,[this]{return started_;})&&window_.load()&&crosshair_.load();
 }
 void Stop(){Update(false,"");if(!thread_.joinable())return;{std::unique_lock lock(gate_);startedEvent_.wait(lock,[this]{return started_;});}auto window=window_.load();if(window)PostMessageW(window,WM_CLOSE,0,0);else if(threadId_)PostThreadMessageW(threadId_,WM_QUIT,0,0);thread_.join();}
};
} // namespace LS
