// White-box native WebView2 acceptance test. Including the implementation gives
// this executable access to controller state without shipping test IPC commands.
// The static archive supplies the other modules, not its browser.cpp object.
#include "../src/browser.cpp"
#include "ghost.h"
#include <cstdio>
#include <shlwapi.h>

using namespace cronq;
using namespace cronq::browser;

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

int main(int argc, char**) {
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    config::Init();
    overlay::Init(true);
    ghost::Init();
    // A normal taskbar window makes this isolated test host inspectable by UI tools.
    auto hwnd = overlay::Hwnd();
    SetWindowLongPtrW(hwnd, GWL_EXSTYLE,
        (GetWindowLongPtrW(hwnd, GWL_EXSTYLE) & ~WS_EX_TOOLWINDOW) | WS_EX_APPWINDOW);
    Init(hwnd);
    int failures = 0;
    auto check = [&](const char* label, bool passed) {
        std::printf("%s %s\n", passed ? "PASS" : "FAIL", label);
        std::fflush(stdout);
        if (!passed) ++failures;
    };
    check("all controllers and UI ready", Wait([] {
        return ui::IsReady() && g.uiLoaded && g.views[kBrowser].core &&
               g.views[kChat].core && g.views[kChat].scriptReady;
    }));
    if (!failures) {
        for (int n = 0; n < 6; ++n) {
            for (const char* app : {"chatgpt", "browser", "home"}) {
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
        check("browser process crash recreates every controller", Wait([generation] {
            return g.views[kUi].generation > generation && ui::IsReady() && g.uiLoaded &&
                   g.views[kBrowser].core && g.views[kChat].core && g.views[kChat].scriptReady;
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
        if (argc > 1) {
            Wait([] { return false; }, 15000); // live visual inspection
            overlay::SwitchApp("browser");
            LoadInBrowser("https://duckduckgo.com/");
            check("live browser document loads", Wait([] {
                return !g.views[kBrowser].loading && g.views[kBrowser].title.find("DuckDuckGo") != std::string::npos;
            }, 30000));
            Wait([] { return false; }, 45000);
        }
    }
    Cleanup();
    ghost::Cleanup();
    overlay::Cleanup();
    CoUninitialize();
    return failures ? 1 : 0;
}
