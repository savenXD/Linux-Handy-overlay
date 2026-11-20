// White-box native WebView2 acceptance test. Including the implementation gives
// this executable access to controller state without shipping test IPC commands.
// The static archive supplies the other modules, not its chat_view.cpp object.
#include "../src/chat_view.cpp"
#include "hotkey.h"
#include "focus_guard.h"
#include <cstdio>
#include <shlwapi.h>

using namespace cronq;
using namespace cronq::chat_view;

template<class F> bool Wait(F condition, DWORD timeout = 20000) {
    const auto end = GetTickCount64() + timeout;
    do {
        MSG msg;
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
        if (condition()) return true;
        Sleep(10);
    } while (GetTickCount64() < end);
    return false;
}

std::string EvalChat(const wchar_t* script) {
    struct Result { bool done=false; std::string value; };
    auto result=std::make_shared<Result>();
    auto callback=MakeHandler<ICoreWebView2ExecuteScriptCompletedHandler,HRESULT,LPCWSTR>(
        [result](HRESULT hr,LPCWSTR value)->HRESULT {
            result->value=SUCCEEDED(hr)&&value?Narrow(value):"ERROR"; result->done=true; return S_OK;
        });
    g.views[kChat].core->ExecuteScript(script,callback.Get());
    return Wait([&]{return result->done;}) ? result->value : "TIMEOUT";
}
void Key(WORD vk,bool down) {
    INPUT event{}; event.type=INPUT_KEYBOARD;event.ki.wVk=vk;
    event.ki.dwFlags=down?0:KEYEVENTF_KEYUP;
    SendInput(1,&event,sizeof(event));
    Wait([]{return false;},40);
}
void ToggleTyping() {
    Key(VK_LMENU,true);Key('M',true);Key('M',false);Key(VK_LMENU,false);
}

int main(int argc, char**) {
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    config::Init();
    overlay::Init(true);
    // A normal taskbar window makes this isolated test host inspectable by UI tools.
    auto hwnd = overlay::Hwnd();
    SetWindowLongPtrW(hwnd, GWL_EXSTYLE,
        (GetWindowLongPtrW(hwnd, GWL_EXSTYLE) & ~WS_EX_TOOLWINDOW) | WS_EX_APPWINDOW);
    input::Init(hwnd);
    const bool hooksReady=hotkey::Init(hwnd);
    focus_guard::Init(hwnd);
    Init(hwnd);
    int failures = 0;
    auto check = [&](const char* label, bool passed) {
        std::printf("%s %s\n", passed ? "PASS" : "FAIL", label);
        std::fflush(stdout);
        if (!passed) ++failures;
    };
    check("typing hook ready",hooksReady);
    check("UI and preloaded ChatGPT ready", Wait([] {
        return ui::IsReady() && g.uiLoaded && g.views[kChat].core && g.views[kChat].scriptReady;
    }));
    if (!failures) {
        for (int n = 0; n < 6; ++n) {
            for (const char* app : {"chatgpt", "home"}) {
                overlay::SwitchApp(app);
                if (std::string(app) == "home") continue;
                check("switch gives visible nonempty content", Wait([] {
                    BOOL visible = FALSE;
                    Active().controller->get_IsVisible(&visible);
                    RECT bounds{};
                    Active().controller->get_Bounds(&bounds);
                    bool aboveUi = false;
                    for (HWND sibling = GetWindow(Active().host, GW_HWNDNEXT); sibling;
                         sibling = GetWindow(sibling, GW_HWNDNEXT))
                        if (sibling == g.views[kUi].host) aboveUi = true;
                    return g.slotValid && visible && aboveUi && IsWindowVisible(Active().host) &&
                           bounds.right > bounds.left && bounds.bottom > bounds.top;
                }));
                overlay::Hide();
                overlay::Show();
            }
        }
        overlay::SwitchApp("chatgpt");
        for (int n = 0; n < 2; ++n) {
            g.views[kChat].core->NavigateToString(
                L"<html><head><title>Crash fixture</title></head><body>Renderer ready</body></html>");
            check("renderer is ready before crash", Wait([] {
                return !g.views[kChat].loading && g.views[kChat].title == "Crash fixture";
            }));
            auto generation = g.views[kChat].generation;
            auto done = MakeHandler<ICoreWebView2CallDevToolsProtocolMethodCompletedHandler, HRESULT, PCWSTR>(
                [](HRESULT, PCWSTR) -> HRESULT { return S_OK; });
            g.views[kChat].core->CallDevToolsProtocolMethod(L"Page.crash", L"{}", done.Get());
            check("renderer crash recreates ChatGPT repeatedly", Wait([generation] {
                return g.views[kChat].generation > generation && g.views[kChat].core &&
                       g.views[kChat].scriptReady;
            }));
        }
        auto generation = g.views[kUi].generation;
        auto done = MakeHandler<ICoreWebView2CallDevToolsProtocolMethodCompletedHandler, HRESULT, PCWSTR>(
            [](HRESULT, PCWSTR) -> HRESULT { return S_OK; });
        g.views[kChat].core->CallDevToolsProtocolMethod(L"Browser.crash", L"{}", done.Get());
        check("WebView2 process crash recreates every controller", Wait([generation] {
            return g.views[kUi].generation > generation && ui::IsReady() && g.uiLoaded &&
                   g.views[kChat].core && g.views[kChat].scriptReady;
        }, 30000));

        // A local document separates rendering/layout failures from network/auth failures.
        Navigate(g.views[kChat], "https://chatgpt.com");
        if (argc == 1) g.views[kChat].core->NavigateToString(
            L"<html><head><title>Rendering verified</title></head><body style='background:#fff;color:#000'>"
            L"<h1>WebView2 rendering verified</h1><p>ChatGPT view survived repeated process failures.</p></body></html>");
        check("active document finishes navigation", Wait([argc] {
            return !g.views[kChat].loading && (argc == 1 ? g.views[kChat].title == "Rendering verified"
                                                         : !g.views[kChat].title.empty());
        }, 30000));
        std::printf("ChatGPT title: %s\n", g.views[kChat].title.c_str());
        bool captured = false;
        ComPtr<IStream> stream;
        auto path = DataDir() + L"\\chatgpt-preview.png";
        SHCreateStreamOnFileEx(path.c_str(), STGM_CREATE | STGM_WRITE, FILE_ATTRIBUTE_NORMAL,
                              TRUE, nullptr, stream.ReleaseAndGetAddressOf());
        auto capture = MakeHandler<ICoreWebView2CapturePreviewCompletedHandler, HRESULT>(
            [&captured](HRESULT hr) -> HRESULT { captured = SUCCEEDED(hr); return S_OK; });
        g.views[kChat].core->CapturePreview(COREWEBVIEW2_CAPTURE_PREVIEW_IMAGE_FORMAT_PNG,
                                          stream.Get(), capture.Get());
        check("rendered preview captured", Wait([&captured] { return captured; }));
        stream.Reset();
        // Exercise real CDP routing and composer DOM focus with a local page only.
        g.views[kChat].core->NavigateToString(
            L"<html><head><title>Typing fixture</title></head><body><textarea id='prompt-textarea'></textarea></body></html>");
        check("typing fixture ready",Wait([]{return !g.views[kChat].loading&&g.views[kChat].title=="Typing fixture";}));
        // Model React appending a streamed subtree in a single mutation batch.
        // Count DOM traversals rather than asserting timing on a particular CPU.
        EvalChat(LR"JS((()=>{
          window.__perfQuery=Element.prototype.querySelectorAll;
          window.__perfScans=0;
          Element.prototype.querySelectorAll=function(...args){
            ++window.__perfScans;return window.__perfQuery.apply(this,args);
          };
          const batch=document.createElement('section');document.body.append(batch);
          for(let i=0;i<500;i++){
            const row=document.createElement('p');row.title='tooltip';batch.append(row);
            const span=document.createElement('span');span.title='tooltip';row.append(span);
          }
        })())JS");
        check("streamed tooltip batch scans only outer subtree",
              EvalChat(L"window.__perfScans===2 && !document.querySelector('[title]')")=="true");
        EvalChat(L"window.__perfScans=0;document.body.title='updated tooltip'");
        check("title attribute updates avoid subtree scans",
              EvalChat(L"window.__perfScans===0 && !document.body.hasAttribute('title')")=="true");
        EvalChat(L"Element.prototype.querySelectorAll=window.__perfQuery;document.querySelector('section').remove()");
        HWND external=CreateWindowW(L"STATIC",L"Cronq focus receiver",WS_OVERLAPPEDWINDOW|WS_VISIBLE,
                                    600,100,400,300,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);
        HWND editor=CreateWindowW(L"EDIT",L"",WS_CHILD|WS_VISIBLE,10,10,350,220,
                                  external,nullptr,GetModuleHandleW(nullptr),nullptr);
        input::ReleaseGrab(); SetForegroundWindow(external);SetFocus(editor);
        POINT originalCursor{};GetCursorPos(&originalCursor);
        SetCursorPos(1000,100);Wait([]{return false;},150);
        auto hoverPoint=EvalChat(L"(()=>{const r=document.getElementById('prompt-textarea').getBoundingClientRect();return [Math.round((r.x+r.width/2)*devicePixelRatio),Math.round((r.y+r.height/2)*devicePixelRatio)]})()");
        int hoverX=0,hoverY=0;sscanf(hoverPoint.c_str(),"[%d,%d]",&hoverX,&hoverY);
        POINT hoverScreen={hoverX,hoverY};ClientToScreen(g.views[kChat].host,&hoverScreen);
        SetCursorPos(hoverScreen.x,hoverScreen.y);
        check("hover enables overlay typing",Wait([]{return input::GetTypingTarget()==input::TypingTarget::Overlay;},3000));
        check("hover keeps normal app OS focus",GetForegroundWindow()==external&&GetFocus()==editor);
        check("hover selects composer",EvalChat(L"document.activeElement.id")=="\"prompt-textarea\"");
        input::ReleaseGrab();SetCursorPos(originalCursor.x,originalCursor.y);Wait([]{return false;},150);
        ToggleTyping();
        check("Alt+M selects overlay without changing OS focus",
              input::GetTypingTarget()==input::TypingTarget::Overlay&&GetForegroundWindow()==external&&GetFocus()==editor);
        check("composer receives DOM focus",EvalChat(L"document.activeElement.id")=="\"prompt-textarea\"");
        Key('A',true);Key('A',false);Key('B',true);Key('B',false);
        check("real ChatGPT view receives overlay-only text",EvalChat(L"document.getElementById('prompt-textarea').value")=="\"ab\"");
        wchar_t value[32]{};GetWindowTextW(editor,value,32);
        check("normal editor receives no mirrored text",value[0]==0&&GetForegroundWindow()==external&&GetFocus()==editor);
        overlay::Hide();
        check("hidden overlay retains typing",input::GetTypingTarget()==input::TypingTarget::Overlay);
        Key('H',true);Key('H',false);
        check("hidden WebView receives real keys",EvalChat(L"document.getElementById('prompt-textarea').value")=="\"abh\"");
        GetWindowTextW(editor,value,32);
        check("hidden typing stays exclusive",value[0]==0&&GetForegroundWindow()==external&&GetFocus()==editor);
        overlay::Show();
        check("show preserves the focused field",EvalChat(L"document.activeElement.id")=="\"prompt-textarea\"");
        ToggleTyping();Key('C',true);Key('C',false);GetWindowTextW(editor,value,32);
        check("second Alt+M restores original editor typing",std::wstring(value)==L"c"&&input::GetTypingTarget()==input::TypingTarget::None);
        check("ChatGPT stops receiving normal typing",EvalChat(L"document.getElementById('prompt-textarea').value")=="\"abh\"");
        DestroyWindow(external);
        if (argc > 1) {
            Navigate(g.views[kChat],"https://chatgpt.com");
            Wait([] { return false; }, 15000); // live visual inspection
        }
    }
    input::Cleanup();
    hotkey::Cleanup();
    focus_guard::Cleanup();
    Cleanup();
    overlay::Cleanup();
    CoUninitialize();
    return failures ? 1 : 0;
}
