// Native WebView2 acceptance checks using a local ChatGPT-shaped fixture, never the user's account.
#include "../src/browser.cpp"
#include "theme.h"
#include <cstdio>
using namespace cronq;
using namespace cronq::browser;
template<class F> bool Wait(F condition, DWORD timeout=15000) {
    auto end=GetTickCount64()+timeout;
    do { MSG msg; while(PeekMessageW(&msg,nullptr,0,0,PM_REMOVE)){TranslateMessage(&msg);DispatchMessageW(&msg);} if(condition())return true; Sleep(10); }while(GetTickCount64()<end);
    return false;
}
std::string Eval(int i, const std::wstring& script) {
    bool done=false; std::string value;
    auto callback=MakeHandler<ICoreWebView2ExecuteScriptCompletedHandler,HRESULT,LPCWSTR>([&](HRESULT hr,LPCWSTR json)->HRESULT{value=SUCCEEDED(hr)&&json?Narrow(json):"ERROR";done=true;return S_OK;});
    g.views[i].core->ExecuteScript(script.c_str(),callback.Get());
    if(!Wait([&]{return done;})) return "TIMEOUT";
    return value;
}
COLORREF g_underlayColor=RGB(60,150,210);
LRESULT CALLBACK UnderlayProc(HWND hwnd,UINT msg,WPARAM wp,LPARAM lp){
    if(msg==WM_PAINT){PAINTSTRUCT ps;auto dc=BeginPaint(hwnd,&ps);RECT r;GetClientRect(hwnd,&r);auto b=CreateSolidBrush(g_underlayColor);FillRect(dc,&r,b);DeleteObject(b);EndPaint(hwnd,&ps);return 0;}
    return DefWindowProcW(hwnd,msg,wp,lp);
}
int main(int argc, char**) {
    CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    config::Init(); overlay::Init(true);
    SetWindowLongPtrW(overlay::Hwnd(),GWL_EXSTYLE,(GetWindowLongPtrW(overlay::Hwnd(),GWL_EXSTYLE)&~WS_EX_TOOLWINDOW)|WS_EX_APPWINDOW);
    theme::Init(); Init(overlay::Hwnd());
    int failures=0;
    auto check=[&](const char* name,bool ok){std::printf("%s %s\n",ok?"PASS":"FAIL",name);std::fflush(stdout);if(!ok)++failures;};
    bool ready=Wait([]{return ui::IsReady()&&g.views[kChat].core&&g.views[kChat].scriptReady;});
    check("controllers ready",ready);
    if(ready){
        auto fixture=DataDir()+L"\\theme-fixture.html";
        std::ofstream out(fixture.c_str()); out<<"<html class='dark'><head><title>Theme fixture</title></head><body><div id='root' style='background:linear-gradient(red,blue)'><header>ChatGPT controls</header><aside>Sidebar</aside><main><article><p>Conversation text</p><button>Copy</button></article><form><textarea>Readable text</textarea><button data-testid='send-button'><svg></svg></button></form></main><footer>Footer</footer></div></body></html>"; out.close();
        ComPtr<ICoreWebView2_3> host;
        g.views[kChat].core.As(&host);
        host->SetVirtualHostNameToFolderMapping(L"chatgpt.com",DataDir().c_str(),COREWEBVIEW2_HOST_RESOURCE_ACCESS_KIND_DENY_CORS);
        Navigate(g.views[kChat],"https://chatgpt.com/theme-fixture.html");
        check("local ChatGPT fixture loads",Wait([]{return g.views[kChat].title=="Theme fixture"&&!g.views[kChat].loading;}));
        for(int mode : {1,0,2,3,1}){
            check("mode saved",theme::SetMode(mode));
            const bool light=theme::Get().light;
            check("UI palette and mode applied",Wait([&]{return Eval(kUi,L"document.documentElement.dataset.appearance")=="\""+std::to_string(mode>=2 ? 2 : mode)+"\"";}));
            auto c=theme::Get().background;
            check("UI background applied",Eval(kUi,L"getComputedStyle(document.body).backgroundColor")== (mode>=2 ? std::string("\"rgba(0, 0, 0, 0)\"") : "\"rgb("+std::to_string(c.r)+", "+std::to_string(c.g)+", "+std::to_string(c.b)+")\""));
            ComPtr<ICoreWebView2Controller2> controller2;g.views[kChat].controller.As(&controller2);
            COREWEBVIEW2_COLOR background{};controller2->get_DefaultBackgroundColor(&background);
            check("WebView canvas matches native transparency key",background.A==(mode>=2?0:255)&&background.R==c.r&&background.G==c.g&&background.B==c.b);
            DWORD flags=0;COLORREF key=0;BYTE alpha=0;GetLayeredWindowAttributes(overlay::Hwnd(),&key,&alpha,&flags);
            check("native transparency follows mode",bool(flags&LWA_COLORKEY)==(mode>=2));
            check("ChatGPT theme class applied",Wait([&]{return Eval(kChat,light?L"document.documentElement.classList.contains('light')":L"document.documentElement.classList.contains('dark')")=="true";}));
            check("site receives preferred color scheme",Eval(kChat,light?L"matchMedia('(prefers-color-scheme: light)').matches":L"matchMedia('(prefers-color-scheme: dark)').matches")=="true");
            check("ChatGPT palette applied",Eval(kChat,L"getComputedStyle(document.documentElement).getPropertyValue('--bg-primary').trim()") == "\""+theme_logic::Hex(theme::Get().background)+"\"");
            check("Cronq controls hidden only in camouflage",Eval(kUi,L"getComputedStyle(document.querySelector('.status-bar')).display==='none'") == (mode>=2 ? "true" : "false"));
            check("ChatGPT controls hidden only in camouflage",Eval(kChat,L"getComputedStyle(document.querySelector('header')).display==='none'&&getComputedStyle(document.querySelector('aside')).display==='none'&&getComputedStyle(document.querySelector('article button')).display==='none'") == (mode>=2 ? "true" : "false"));
            check("essential ChatGPT text and composer stay visible",Eval(kChat,L"getComputedStyle(document.querySelector('article p')).display!=='none'&&getComputedStyle(document.querySelector('textarea')).display!=='none'&&getComputedStyle(document.querySelector('[data-testid=send-button]')).display!=='none'")=="true");
            if(mode>=2){
                check("camouflage is borderless",Eval(kUi,L"getComputedStyle(document.querySelector('.phone-root')).borderTopWidth")=="\"0px\"");
                check("home shows icons only",Eval(kUi,L"getComputedStyle(document.querySelector('.app-label')).display==='none'&&getComputedStyle(document.querySelector('.home-search-bar')).display==='none'")=="true");
                check("ChatGPT background is transparent",Eval(kChat,L"getComputedStyle(document.body).backgroundColor")=="\"rgba(0, 0, 0, 0)\"");
                check("page wrapper backgrounds removed",Eval(kChat,L"getComputedStyle(document.getElementById('root')).backgroundImage")=="\"none\"");
                check("camouflage text color follows Light/Dark",Eval(kChat,L"getComputedStyle(document.querySelector('article p')).color")== (mode==2 ? "\"rgb(0, 0, 0)\"" : "\"rgb(255, 255, 255)\""));
            }
            check("appearance persists through config reload",config::Reload()&&config::Get().appearanceMode==mode);
        }
        Eval(kChat,L"document.documentElement.classList.remove('light');document.documentElement.classList.add('dark')");
        check("ChatGPT rerender cannot undo Light mode",Wait([]{return Eval(kChat,L"document.documentElement.classList.contains('light')&&!document.documentElement.classList.contains('dark')")=="true";}));
        theme::SetMode(2);
        check("toggle restores previous Light appearance",theme::ToggleCamouflage()&&theme::Mode()==1);
        check("toggle reenters camouflage",theme::ToggleCamouflage()&&theme::Mode()==2);
        Eval(kChat,L"window.__cronqBeforeReload=true");
        g.views[kChat].core->Reload();
        check("ChatGPT navigation preserves camouflage",Wait([]{return Eval(kChat,L"typeof window.__cronqBeforeReload==='undefined'&&document.title==='Theme fixture'&&!!document.getElementById('cronq-appearance')")=="true";}));
        Eval(kUi,L"document.getElementById('appearance').click()"); // Camo -> Dark via the actual native bridge.
        check("appearance button reaches host",Wait([]{return theme::Mode()==0;}));
        // Test actual desktop composition, not just CSS or controller flags.
        overlay::SwitchApp("chatgpt");
        theme::SetMode(2);
        SetWindowDisplayAffinity(overlay::Hwnd(),WDA_NONE); // Test must see the overlay in screen pixels.
        RECT bounds;GetWindowRect(overlay::Hwnd(),&bounds);
        WNDCLASSW wc{};wc.hInstance=GetModuleHandleW(nullptr);wc.lpszClassName=L"CronqThemeUnderlay";wc.lpfnWndProc=UnderlayProc;RegisterClassW(&wc);
        HWND underlay=CreateWindowExW(WS_EX_TOPMOST|WS_EX_NOACTIVATE,wc.lpszClassName,L"Transparency fixture",WS_POPUP|WS_VISIBLE,bounds.left,bounds.top,bounds.right-bounds.left,bounds.bottom-bounds.top,nullptr,nullptr,wc.hInstance,nullptr);
        SetWindowPos(overlay::Hwnd(),HWND_TOPMOST,0,0,0,0,SWP_NOMOVE|SWP_NOSIZE|SWP_NOACTIVATE);
        auto screenPixel=[&]{auto dc=GetDC(nullptr);auto c=GetPixel(dc,bounds.left+40,bounds.bottom-40);ReleaseDC(nullptr,dc);return c;};
        check("background window visible through ChatGPT",Wait([&]{return screenPixel()==g_underlayColor;},3000));
        auto dc=GetDC(nullptr);int textPixels=0;
        for(int y=10;y<60 && textPixels<12;y+=2)for(int x=5;x<200 && textPixels<12;x+=2){auto c=GetPixel(dc,bounds.left+x,bounds.top+y);if(GetRValue(c)<90&&GetGValue(c)<90&&GetBValue(c)<90)++textPixels;}
        ReleaseDC(nullptr,dc);
        check("conversation text remains visible over the background",textPixels>=12);
        g_underlayColor=RGB(180,210,80);InvalidateRect(underlay,nullptr,FALSE);UpdateWindow(underlay);
        check("changing underlying content remains visible",Wait([&]{return screenPixel()==g_underlayColor;},3000));
        std::printf("Light background pixel=%06lx expected=%06lx\n",screenPixel(),g_underlayColor);
        check("switch to white text preserves camouflage",theme::ToggleTone()&&theme::Mode()==3);
        check("white text CSS reaches ChatGPT",Wait([]{return Eval(kChat,L"getComputedStyle(document.querySelector('article p')).color")=="\"rgb(255, 255, 255)\"";}));
        check("Dark camouflage remains see-through",Wait([&]{return screenPixel()==g_underlayColor;},3000));
        std::printf("Dark background pixel=%06lx expected=%06lx\n",screenPixel(),g_underlayColor);
        dc=GetDC(nullptr);textPixels=0;
        for(int y=10;y<60 && textPixels<12;y+=2)for(int x=5;x<200 && textPixels<12;x+=2){auto c=GetPixel(dc,bounds.left+x,bounds.top+y);if(GetRValue(c)>225&&GetGValue(c)>225&&GetBValue(c)>225)++textPixels;}
        ReleaseDC(nullptr,dc);
        check("white conversation text visible on desktop",textPixels>=12);
        check("disable Dark camouflage restores Dark appearance",theme::ToggleCamouflage()&&theme::Mode()==0);
        theme::SetMode(1);
        check("Light restores an opaque background",Wait([&]{return screenPixel()==RGB(255,255,255);},3000));
        DestroyWindow(underlay);
        if(argc>1){theme::SetMode(2);overlay::SwitchApp("home");Wait([]{return false;},45000);}
    }
    theme::Cleanup(); Cleanup(); overlay::Cleanup(); CoUninitialize(); return failures?1:0;
}
