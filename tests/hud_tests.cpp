#include "../native/practice_hud.h"
#include <iostream>
#include <stdexcept>
static void Require(bool ok,const char* reason){if(!ok)throw std::runtime_error(reason);}
static constexpr UINT DragPairMessage=WM_APP+450;
static WNDPROC originalHudProcedure=nullptr;
static LRESULT CALLBACK HudFixture(HWND window,UINT message,WPARAM wparam,LPARAM lparam){
 if(message==DragPairMessage){
  // Exercise the actual two native input branches in one owner-thread event.
  // A posted foreground refresh can otherwise invalidate an artificial drag
  // between two cross-thread sends when no gameplay window has focus.
  CallWindowProcW(originalHudProcedure,window,WM_LBUTTONDOWN,0,0);
  CallWindowProcW(originalHudProcedure,window,WM_LBUTTONUP,0,0);return 0;
 }
 return CallWindowProcW(originalHudProcedure,window,message,wparam,lparam);
}
int main(){try{
 for(const auto size:std::vector<POINT>{{1920,1080},{1920,1200},{3440,1440},{390,600},{10,10}}){
  for(const int dpi:{96,144,192,288})for(const float scale:{.75f,1.f,2.f})for(const float anchor:{0.f,.5f,1.f}){
   LS::HudSettings settings;settings.scale=scale;settings.anchorX=settings.anchorY=anchor;
   const RECT client{-1000,200,-1000+size.x,200+size.y};const auto g=LS::CalculateHudGeometry(client,settings,400,dpi);
   Require(g.left>=client.left&&g.top>=client.top&&g.left+g.width<=client.right&&g.top+g.height<=client.bottom,"HUD stays within translated client at every DPI/anchor");
  }
 }
 LS::HudSettings invalid;invalid.scale=NAN;invalid.anchorX=INFINITY;invalid.anchorY=-INFINITY;invalid.margin=999999;
 const auto g=LS::CalculateHudGeometry({0,0,1280,720},invalid,400,96);Require(g.width>0&&g.height>0&&g.left>=0&&g.top>=0,"invalid settings get finite bounded layout");
 LS::HudSnapshot snapshot;snapshot.active=true;snapshot.modeLabel="长枪";snapshot.motionLabel="上段突刺";snapshot.derivations.push_back({"反击","Ctrl + 右键",true,false});snapshot.resourceValid=true;snapshot.resourceLabel="蓄力";snapshot.targetValid=true;
 LS::PracticeHud hud;const auto before=GetGuiResources(GetCurrentProcess(),GR_GDIOBJECTS);DWORD settled=0;for(int i=0;i<5;++i){hud.Start();hud.Update(snapshot,{});Require(hud.WaitUntilReady(),"both HUD windows initialized before use");Require(hud.Window()!=nullptr&&hud.CrosshairWindow()!=nullptr,"both HUD windows created");KillTimer(hud.Window(),1); // Deterministic input-message test without a foreground gameplay window.
  Require(SendMessageW(hud.Window(),WM_NCHITTEST,0,0)==HTTRANSPARENT,"normal HUD click-through");Require(SendMessageW(hud.CrosshairWindow(),WM_NCHITTEST,0,0)==HTTRANSPARENT,"crosshair always click-through");
  LS::HudSettings edit;edit.editing=true;hud.Update(snapshot,edit);Require(SendMessageW(hud.Window(),WM_NCHITTEST,0,0)==HTCLIENT,"HUD editing accepts drag only");Require(SendMessageW(hud.Window(),WM_MOUSEACTIVATE,0,0)==MA_NOACTIVATE,"HUD edit does not activate window");
  originalHudProcedure=reinterpret_cast<WNDPROC>(SetWindowLongPtrW(hud.Window(),GWLP_WNDPROC,reinterpret_cast<LONG_PTR>(HudFixture)));Require(originalHudProcedure!=nullptr,"HUD fixture installs owner-thread input transaction");SendMessageW(hud.Window(),DragPairMessage,0,0);SetWindowLongPtrW(hud.Window(),GWLP_WNDPROC,reinterpret_cast<LONG_PTR>(originalHudProcedure));float x=0,y=0;Require(hud.TakeLayoutEdit(x,y),"drag releases layout edit for persistence");Require(!hud.TakeLayoutEdit(x,y),"layout edit delivered once");
  hud.Update(false,"");hud.Stop();Require(hud.Window()==nullptr&&hud.CrosshairWindow()==nullptr,"HUD stop destroys both windows and joins thread");const auto count=GetGuiResources(GetCurrentProcess(),GR_GDIOBJECTS);if(i==0)settled=count;else Require(count<=settled,"no accumulated GDI objects after first Win32 GUI initialization");}
 LS::PracticeHud canceled;canceled.Start();canceled.Stop();Require(canceled.Window()==nullptr&&canceled.CrosshairWindow()==nullptr,"immediate stop joins initialization and cleans both windows");
 const auto after=GetGuiResources(GetCurrentProcess(),GR_GDIOBJECTS);std::cout<<"GDI initial "<<before<<" after GUI initialization "<<settled<<" after five rounds "<<after<<'\n';Require(after<=settled,"no accumulated GDI objects across warmed lifecycle rounds");
 std::cout<<"HUD geometry, invalid-value fallback, click-through/edit drag and five lifecycle/GDI rounds passed\n";return 0;
 }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
