#pragma once
#include <Windows.h>
#include <atomic>
#include <condition_variable>
#include <mutex>
#include <string>
#include <thread>

namespace LS {
class PracticeHud{
 std::mutex gate_;std::thread thread_;std::atomic<DWORD> threadId_{0};std::atomic<HWND> window_{nullptr};
 bool visible_=false,started_=false;std::condition_variable startedEvent_;std::wstring text_;HFONT font_=nullptr;
 static constexpr UINT RefreshMessage=WM_APP+37;
 static std::wstring Utf16(const std::string& text){if(text.empty())return {};const int size=MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,text.data(),int(text.size()),nullptr,0);if(size<=0)return L"Invalid HUD text";
  std::wstring out(size,L'\0');MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,text.data(),int(text.size()),out.data(),size);return out;}
 static LRESULT CALLBACK Procedure(HWND window,UINT message,WPARAM wparam,LPARAM lparam){
  auto self=reinterpret_cast<PracticeHud*>(GetWindowLongPtrW(window,GWLP_USERDATA));
  if(message==WM_NCCREATE){self=static_cast<PracticeHud*>(reinterpret_cast<CREATESTRUCTW*>(lparam)->lpCreateParams);SetWindowLongPtrW(window,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(self));}
  if(!self)return DefWindowProcW(window,message,wparam,lparam);
  switch(message){
   case WM_NCHITTEST:return HTTRANSPARENT;
   case WM_MOUSEACTIVATE:return MA_NOACTIVATE;
   case RefreshMessage:case WM_TIMER:{auto foreground=GetForegroundWindow();DWORD process=0;GetWindowThreadProcessId(foreground,&process);RECT rectangle{};bool visible=false;
    {std::lock_guard lock(self->gate_);visible=self->visible_;}
    if(!visible||process!=GetCurrentProcessId()||!GetWindowRect(foreground,&rectangle)||IsIconic(foreground)){ShowWindow(window,SW_HIDE);return 0;}
    SetWindowPos(window,HWND_TOPMOST,rectangle.left+24,rectangle.top+90,370,136,SWP_NOACTIVATE|SWP_SHOWWINDOW);InvalidateRect(window,nullptr,FALSE);return 0;}
   case WM_PAINT:{PAINTSTRUCT paint{};const auto dc=BeginPaint(window,&paint);RECT area{};GetClientRect(window,&area);const auto brush=CreateSolidBrush(RGB(12,17,22));FillRect(dc,&area,brush);DeleteObject(brush);
    auto previous=SelectObject(dc,self->font_);SetTextColor(dc,RGB(240,242,238));SetBkMode(dc,TRANSPARENT);InflateRect(&area,-12,-9);
    {std::lock_guard lock(self->gate_);DrawTextW(dc,self->text_.data(),int(self->text_.size()),&area,DT_LEFT|DT_TOP|DT_NOPREFIX|DT_WORDBREAK);}
    SelectObject(dc,previous);EndPaint(window,&paint);return 0;}
   case WM_CLOSE:DestroyWindow(window);return 0;
   case WM_DESTROY:PostQuitMessage(0);return 0;
  }
  return DefWindowProcW(window,message,wparam,lparam);
 }
public:
 ~PracticeHud(){Stop();}
 void Start(){
  if(thread_.joinable())return;
  {std::lock_guard lock(gate_);started_=false;}
  thread_=std::thread([this]{threadId_=GetCurrentThreadId();const auto instance=GetModuleHandleW(nullptr);constexpr wchar_t ClassName[]=L"MHE.LocalPracticeHud.v1";
   WNDCLASSEXW info{sizeof(WNDCLASSEXW)};info.lpfnWndProc=Procedure;info.hInstance=instance;info.lpszClassName=ClassName;RegisterClassExW(&info);
   font_=CreateFontW(-17,0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH,L"Segoe UI");
   auto window=CreateWindowExW(WS_EX_TOOLWINDOW|WS_EX_LAYERED|WS_EX_TRANSPARENT|WS_EX_NOACTIVATE,ClassName,L"Monster Hunter Endfield local practice",WS_POPUP,0,0,370,136,nullptr,nullptr,instance,this);
   window_=window;
   MSG initial{};PeekMessageW(&initial,nullptr,0,0,PM_NOREMOVE);
   {std::lock_guard lock(gate_);started_=true;}startedEvent_.notify_all();
   if(window){SetLayeredWindowAttributes(window,0,220,LWA_ALPHA);SetTimer(window,1,100,nullptr);PostMessageW(window,RefreshMessage,0,0);MSG message{};
    while(GetMessageW(&message,nullptr,0,0)>0){TranslateMessage(&message);DispatchMessageW(&message);}if(IsWindow(window))DestroyWindow(window);}
   window_=nullptr;if(font_)DeleteObject(font_);font_=nullptr;UnregisterClassW(ClassName,instance);threadId_=0;
  });
 }
 void Update(bool visible,const std::string& text){{std::lock_guard lock(gate_);visible_=visible;text_=Utf16(text);}auto window=window_.load();if(window)PostMessageW(window,RefreshMessage,0,0);}
 void Stop(){Update(false,"");if(!thread_.joinable())return;
  // Wait only for queue creation, never for game focus or Unity callbacks.
  {std::unique_lock lock(gate_);startedEvent_.wait(lock,[this]{return started_;});}
  auto window=window_.load();if(window)PostMessageW(window,WM_CLOSE,0,0);else if(threadId_)PostThreadMessageW(threadId_,WM_QUIT,0,0);
  thread_.join();
 }
};
} // namespace LS
