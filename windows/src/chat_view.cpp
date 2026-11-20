#include "chat_view.h"

#include <algorithm>
#include <array>
#include <functional>
#include <memory>
#include <vector>

#include "chat_logic.h"
#include "recovery_logic.h"
#include <map>
#include "config.h"
#include "input.h"
#include "hotkey.h"
#include "jsonlite.h"
#include "key_translate.h"
#include "overlay.h"
#include "snap_logic.h"
#include "image_logic.h"
#include "image_codec.h"
#include "ui_bridge.h"
#include "theme.h"
#include <fstream>
#include <sstream>
#ifdef CRONQ_WEBVIEW2
#include "com_support.h"
#include <wincodec.h>
#endif

namespace cronq {
namespace chat_view {
namespace {
std::string g_failure;
[[maybe_unused]] bool g_shuttingDown = true;  // Used only by WebView2-enabled builds.
[[maybe_unused]] HWND g_parent = nullptr;
std::string g_app = "home";
#ifdef CRONQ_WEBVIEW2
constexpr int kUi = 0, kChat = 1, kPopup = 2, kEnvironment = 3;
struct View {
    HWND host = nullptr;
    ComPtr<ICoreWebView2Controller> controller;
    ComPtr<ICoreWebView2> core;
    std::vector<std::function<void()>> detach;
    unsigned generation = 0;
    bool creating = false, loading = false, scriptReady = true, hoverPending = false;
    bool layoutValid = false, visible = false;
    RECT placement{};
    std::string uri, title, pending;
};
struct PendingPopup {
    ComPtr<ICoreWebView2NewWindowRequestedEventArgs> args;
    ComPtr<ICoreWebView2Deferral> deferral;
    bool completed = false;
    void Complete() {
        if (!completed) {
            completed = true;
            if (deferral) {
                HRESULT hr = deferral->Complete();
                if (FAILED(hr)) LogErr("CHAT", "popup Complete hr=0x%08lX", hr);
            }
            args.Reset();
            deferral.Reset();
        }
    }
};
struct State {
    ComPtr<ICoreWebView2Environment> environment;
    std::array<View, 3> views;
    std::shared_ptr<PendingPopup> popup;
    HMODULE loader = nullptr;
    RECT slot{};
    bool slotValid = false, uiLoaded = false, contentStarted = false, muted = false;
    unsigned environmentGeneration = 0;
} g;

bool Check(HRESULT hr, const char* operation) {
    if (FAILED(hr)) {
        LogErr("CHAT", "%s hr=0x%08lX", operation, hr);
        return false;
    }
    return true;
}
struct ComString {
    LPWSTR p = nullptr;
    ~ComString() { CoTaskMemFree(p); }
    std::string Text() const { return p ? Narrow(p) : std::string(); }
};
bool Live(int i, unsigned gen) { return !g_shuttingDown && g.views[i].generation == gen && g.views[i].core; }
int ActiveIndex() { return kChat; }
View& Active() { return g.views[ActiveIndex()]; }
void UpdateLayout();
void CreateView(int i);
void CloseView(int i) {
    auto& v = g.views[i];
    ++v.generation;
    v.creating = false;
    v.hoverPending = false;
    for (auto& remove : v.detach) remove();
    v.detach.clear();
    if (v.controller) Check(v.controller->Close(), "controller Close");
    v.core.Reset();
    v.controller.Reset();
    if (v.host) DestroyWindow(v.host);
    v.host = nullptr;
    v.loading = false;
    v.layoutValid = false;
    v.uri.clear();
    v.title.clear();
}
// ---- recovery pacing: a crash loop (bad GPU driver, broken runtime) must not spin the CPU.
constexpr unsigned kRecoverNow = 0x100;  // wParam flag: a delayed recovery whose wait is over
recovery_logic::Backoff g_recoverBackoff[4] = {  // three views and the whole environment
    {0, 2000, 60000, 30000}, {0, 2000, 60000, 30000}, {0, 2000, 60000, 30000},
    {0, 2000, 60000, 30000}};
std::map<UINT_PTR, std::pair<unsigned, unsigned>> g_delayedRecover;  // timer -> (index, generation)
recovery_logic::Backoff g_initBackoff(5000, 10000, 120000, 600000);
UINT_PTR g_retryTimer = 0;
HWND g_retryParent = nullptr;

void CALLBACK DelayedRecoverProc(HWND, UINT, UINT_PTR id, DWORD) {
    KillTimer(nullptr, id);
    auto it = g_delayedRecover.find(id);
    if (it == g_delayedRecover.end()) return;
    const auto request = it->second;
    g_delayedRecover.erase(it);
    if (g_parent) PostMessageW(g_parent, WM_CRONQ_WEBVIEW_RECOVER, request.first | kRecoverNow, request.second);
}
void CancelDelayedRecoveries() {
    for (const auto& entry : g_delayedRecover) KillTimer(nullptr, entry.first);
    g_delayedRecover.clear();
}
void CALLBACK RetryInitProc(HWND, UINT, UINT_PTR id, DWORD) {
    KillTimer(nullptr, id);
    g_retryTimer = 0;
    HWND parent = g_retryParent;
    g_retryParent = nullptr;
    if (!parent || !g_shuttingDown) return;
    Log("CHAT", "Retrying WebView2 start-up.");
    Init(parent);
    overlay::QueueDraw();
}

void Fail(const char* operation, HRESULT hr) {
    g_shuttingDown = true;
    LogErr("CHAT", "WebView2 unavailable: %s hr=0x%08lX", operation, hr);
    g_failure =
        hr == HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND)
            ? "WebView2 runtime not installed. Get it at https://go.microsoft.com/fwlink/p/?LinkId=2124703"
            : "WebView2 unavailable. See cronq.log; install Microsoft Edge WebView2 Runtime.";
    ui::Reset();
    g.uiLoaded = false;
    if (g.popup) {
        g.popup->Complete();
        g.popup.reset();
    }
    for (int j = kPopup; j >= 0; --j) CloseView(j);
    g.environment.Reset();
    g_shuttingDown = true;
    // Transient failures (runtime updating, low memory, runtime installed later) recover on their own:
    // try again after 5 s, then 10, 20 ... 120 s, instead of staying broken until Cronq restarts.
    if (g_parent && !g_retryTimer) {
        const int64_t delay = std::max<int64_t>(1000, g_initBackoff.Next(NowUs() / 1000));
        g_retryParent = g_parent;
        g_retryTimer = SetTimer(nullptr, 0, static_cast<UINT>(delay), RetryInitProc);
        Log("CHAT", "Will retry WebView2 in %lld s.", (long long)(delay / 1000));
    }
    overlay::QueueDraw();
}
void Navigate(View& v, const std::string& uri) {
    if (!uri.empty()) {
        v.pending = uri;  // Keep a host-owned URL even if the renderer/browser dies.
        if (v.core && v.scriptReady)
            Check(v.core->Navigate(Widen(uri).c_str()), "Navigate");
    }
}
void Mute(View& v) {
    if (!v.core) return;
    ComPtr<ICoreWebView2_8> audio;
    if (Check(v.core.As(&audio), "Query audio"))
        Check(audio->put_IsMuted(g.muted ? TRUE : FALSE), "put_IsMuted");
}

template <typename Handler, typename Args, typename Add, typename Remove, typename F>
bool Bind(int i, Add add, Remove remove, F fn) {
    auto& v = g.views[i];
    unsigned gen = v.generation;
    EventRegistrationToken token{};
    auto handler =
        MakeHandler<Handler, ICoreWebView2*, Args>([i, gen, fn](ICoreWebView2*, Args args) -> HRESULT {
            if (Live(i, gen)) fn(args);
            return S_OK;
        });
    if (!Check((v.core.Get()->*add)(handler.Get(), &token), "add event handler")) return false;
    ICoreWebView2* core = v.core.Get();
    v.detach.emplace_back([core, remove, token] { Check((core->*remove)(token), "remove event handler"); });
    return true;
}
#define BIND(I, NAME, ARGS, ...)                                                 \
    Bind<ICoreWebView2##NAME##EventHandler, ARGS>(I, &ICoreWebView2::add_##NAME, \
                                                  &ICoreWebView2::remove_##NAME, __VA_ARGS__)

void PopupRequested(int source, ICoreWebView2NewWindowRequestedEventArgs* args) {
    ComString uri;
    BOOL user = FALSE;
    if (!Check(args->get_Uri(&uri.p), "popup get_Uri") ||
        !Check(args->get_IsUserInitiated(&user), "popup get_IsUserInitiated"))
        return;
    std::string text = uri.Text();
    // Hosts only: full URLs (conversation ids, search terms, tokens) never go into the log.
    Log("CHAT", "NewWindowRequested host='%s' isUserInitiated=%d", chat_logic::ExtractHost(text).c_str(), user);
    if (!chat_logic::IsAuthUri(text) && user && !text.empty() && text != "about:blank") {
        if (Check(args->put_Handled(TRUE), "popup handled")) Navigate(g.views[source], text);
        return;
    }
    if (!Check(args->put_Handled(TRUE), "popup handled")) return;
    ClosePopup();
    auto pending = std::make_shared<PendingPopup>();
    pending->args = args;
    if (!Check(args->GetDeferral(pending->deferral.ReleaseAndGetAddressOf()), "popup GetDeferral")) return;
    g.popup = pending;
    CreateView(kPopup);
    ui::PushState();
}

bool ConfigureView(int i) {
    auto& v = g.views[i];
    ComPtr<ICoreWebView2Settings> settings;
    if (!Check(v.core->get_Settings(settings.ReleaseAndGetAddressOf()), "get_Settings")) return false;
    if (!Check(settings->put_IsStatusBarEnabled(FALSE), "put_IsStatusBarEnabled")) return false;
    // The UI host is trusted; remote content never receives the native bridge.
    if (!Check(settings->put_IsWebMessageEnabled(i == kUi ? TRUE : FALSE), "put_IsWebMessageEnabled"))
        return false;
    // Native browser chrome (menus, DevTools, autofill/password bubbles, dialogs, prompts) is drawn
    // in separate msedgewebview2.exe windows. Windows only lets a process exclude its *own* windows
    // from capture, so those would show up in recordings and screen shares: turn them all off.
    Check(settings->put_AreDefaultContextMenusEnabled(FALSE), "put_AreDefaultContextMenusEnabled");
    Check(settings->put_AreDevToolsEnabled(FALSE), "put_AreDevToolsEnabled");
    Check(settings->put_AreDefaultScriptDialogsEnabled(FALSE), "put_AreDefaultScriptDialogsEnabled");
    {
        ComPtr<ICoreWebView2Settings3> settings3;
        if (SUCCEEDED(settings.As(&settings3)))  // Ctrl+F find bar, Ctrl+P print, F12 ... (editing keys stay)
            Check(settings3->put_AreBrowserAcceleratorKeysEnabled(FALSE), "put_AreBrowserAcceleratorKeysEnabled");
        ComPtr<ICoreWebView2Settings4> settings4;
        if (SUCCEEDED(settings.As(&settings4))) {
            Check(settings4->put_IsGeneralAutofillEnabled(FALSE), "put_IsGeneralAutofillEnabled");
            Check(settings4->put_IsPasswordAutosaveEnabled(FALSE), "put_IsPasswordAutosaveEnabled");
        }
    }
    ComPtr<ICoreWebView2Controller2> controller2;
    if (!Check(v.controller.As(&controller2), "Query controller2") ||
        !Check(controller2->put_DefaultBackgroundColor({255, 0, 0, 0}), "put_DefaultBackgroundColor"))
        return false;
    auto cdpFocusHandler =
        MakeHandler<ICoreWebView2CallDevToolsProtocolMethodCompletedHandler, HRESULT, PCWSTR>(
            [](HRESULT hr, PCWSTR) -> HRESULT {
                Check(hr, "CDP input completion");
                return S_OK;
            });
    v.core->CallDevToolsProtocolMethod(L"Emulation.setFocusEmulationEnabled", L"{\"enabled\":true}",
                                       cdpFocusHandler.Get());
    bool ok = true;

    ok &= BIND(i, NavigationStarting, ICoreWebView2NavigationStartingEventArgs*, [i](auto* a) {
        ComString uri;
        if (!Check(a->get_Uri(&uri.p), "navigation get_Uri")) return;
        const auto text = uri.Text();
        if (i == kUi && text != "https://cronq.ui/index.html") {
            Check(a->put_Cancel(TRUE), "block UI navigation");
            return;
        }
        if (i == kChat && !text.empty() && !chat_logic::IsAuthUri(text) &&
            !chat_logic::IsDomainAllowedForChatGpt(text, config::Get().chatgptAllowlist)) {
            Check(a->put_Cancel(TRUE), "block external navigation");
            Log("CHAT", "Blocked external ChatGPT navigation to host '%s'.",
                chat_logic::ExtractHost(text).c_str());
            overlay::ShowToast("External website navigation is unavailable");
            return;
        }
        if (i == kUi) ui::Reset();
        g.views[i].loading = true;
        ui::PushState();
    });
    ok &= BIND(i, NavigationCompleted, ICoreWebView2NavigationCompletedEventArgs*, [i](auto* a) {
        BOOL success = FALSE;
        Check(a->get_IsSuccess(&success), "navigation get_IsSuccess");
        g.views[i].loading = false;
        RefreshAppearance();
        if (!success) {
            COREWEBVIEW2_WEB_ERROR_STATUS status{};
            Check(a->get_WebErrorStatus(&status), "get_WebErrorStatus");
            LogErr("CHAT", "NavigationCompleted view=%d status=%d", i, int(status));
        }
        if (i == kUi) {
            if (!success) {
                Fail("UI NavigationCompleted", E_FAIL);
                return;
            }
            g.uiLoaded = true;
            if (!g.contentStarted) {
                g.contentStarted = true;
                CreateView(kChat);  // preloaded so ChatGPT opens instantly
            }
        }
        UpdateLayout();
        if ((i == kChat || i == kPopup) && input::GetTypingTarget() == input::TypingTarget::Overlay)
            PrepareTypingTarget();
        ui::PushState();
    });
    ok &= BIND(i, SourceChanged, ICoreWebView2SourceChangedEventArgs*, [i](auto*) {
        ComString uri;
        if (Check(g.views[i].core->get_Source(&uri.p), "get_Source")) g.views[i].uri = uri.Text();
        ui::PushState();
    });
    ok &= BIND(i, DocumentTitleChanged, IUnknown*, [i](auto*) {
        ComString title;
        if (Check(g.views[i].core->get_DocumentTitle(&title.p), "get_DocumentTitle"))
            g.views[i].title = title.Text();
        ui::PushState();
    });
    ok &= BIND(i, HistoryChanged, IUnknown*, [](auto*) { ui::PushState(); });
    // Default dialogs are off: an unhandled alert/confirm/prompt is dismissed. Leaving a page
    // (beforeunload) is allowed so navigation never gets stuck behind an invisible question.
    ok &= BIND(i, ScriptDialogOpening, ICoreWebView2ScriptDialogOpeningEventArgs*, [](auto* a) {
        COREWEBVIEW2_SCRIPT_DIALOG_KIND kind{};
        if (Check(a->get_Kind(&kind), "dialog get_Kind") && kind == COREWEBVIEW2_SCRIPT_DIALOG_KIND_BEFOREUNLOAD)
            Check(a->Accept(), "dialog Accept");
    });
    // Permission prompts are native bubbles too: decide silently instead of asking.
    ok &= BIND(i, PermissionRequested, ICoreWebView2PermissionRequestedEventArgs*, [i](auto* a) {
        COREWEBVIEW2_PERMISSION_KIND kind{};
        Check(a->get_PermissionKind(&kind), "permission get_PermissionKind");
        const bool allow = i == kChat && kind == COREWEBVIEW2_PERMISSION_KIND_CLIPBOARD_READ;
        Check(a->put_State(allow ? COREWEBVIEW2_PERMISSION_STATE_ALLOW : COREWEBVIEW2_PERMISSION_STATE_DENY),
              "permission put_State");
        if (!allow) {
            Log("CHAT", "Denied permission kind=%d in view=%d", int(kind), i);
            overlay::ShowToast("Blocked a site permission request");
        }
    });
    ok &= BIND(i, ProcessFailed, ICoreWebView2ProcessFailedEventArgs*, [i](auto* a) {
        COREWEBVIEW2_PROCESS_FAILED_KIND kind{};
        Check(a->get_ProcessFailedKind(&kind), "get_ProcessFailedKind");
        ComPtr<ICoreWebView2ProcessFailedEventArgs2> more;
        int exitCode = 0;
        if (Check(a->QueryInterface(__uuidof(ICoreWebView2ProcessFailedEventArgs2),
                                    reinterpret_cast<void**>(more.ReleaseAndGetAddressOf())),
                  "Query process failure args"))
            Check(more->get_ExitCode(&exitCode), "get_ExitCode");
        LogErr("CHAT", "ProcessFailed view=%d kind=%d exit=%d", i, int(kind), exitCode);
        // Recover outside this COM callback. Reload cannot revive a dead browser
        // process, and closing controllers inside an event is reentrant.
        if (kind == COREWEBVIEW2_PROCESS_FAILED_KIND_BROWSER_PROCESS_EXITED)
            PostMessageW(g_parent, WM_CRONQ_WEBVIEW_RECOVER, kEnvironment, g.environmentGeneration);
        else if (kind == COREWEBVIEW2_PROCESS_FAILED_KIND_RENDER_PROCESS_EXITED ||
                 kind == COREWEBVIEW2_PROCESS_FAILED_KIND_RENDER_PROCESS_UNRESPONSIVE)
            PostMessageW(g_parent, WM_CRONQ_WEBVIEW_RECOVER, i, g.views[i].generation);
    });
    if (i == kChat)
        ok &= BIND(i, NewWindowRequested, ICoreWebView2NewWindowRequestedEventArgs*,
                   [i](auto* a) { PopupRequested(i, a); });
    if (i == kPopup) ok &= BIND(i, WindowCloseRequested, IUnknown*, [](auto*) { ClosePopup(); });
    if (i == kUi) {
        ComPtr<ICoreWebView2_3> virtualHost;
        if (!Check(v.core.As(&virtualHost), "Query virtual host")) return false;
        wchar_t exe[32768];
        DWORD count = GetModuleFileNameW(nullptr, exe, 32768);
        if (!count || count >= 32768) return false;
        std::wstring folder(exe, count);
        folder = folder.substr(0, folder.find_last_of(L"\\/")) + L"\\ui";
        for (const auto* file : {L"index.html", L"cronq.css", L"cronq.js"}) {
            const auto path = folder + L"\\" + file;
            DWORD attributes = GetFileAttributesW(path.c_str());
            if (attributes == INVALID_FILE_ATTRIBUTES || (attributes & FILE_ATTRIBUTE_DIRECTORY)) {
                LogErr("CHAT", "Missing UI asset '%s'", Narrow(path).c_str());
                return false;
            }
        }
        if (!Check(virtualHost->SetVirtualHostNameToFolderMapping(
                       L"cronq.ui", folder.c_str(), COREWEBVIEW2_HOST_RESOURCE_ACCESS_KIND_ALLOW),
                   "SetVirtualHostNameToFolderMapping"))
            return false;
        ok &= BIND(i, WebMessageReceived, ICoreWebView2WebMessageReceivedEventArgs*, [](auto* a) {
            ComString source, text;
            if (!Check(a->get_Source(&source.p), "message get_Source") ||
                source.Text() != "https://cronq.ui/index.html")
                return;
            if (Check(a->get_WebMessageAsJson(&text.p), "get_WebMessageAsJson")) ui::OnMessage(text.Text());
        });
    }
    if (i != kUi) {
        ComPtr<ICoreWebView2_4> core4;
        if (SUCCEEDED(v.core.As(&core4))) {
            unsigned gen = v.generation;
            EventRegistrationToken token{};
            auto handler = MakeHandler<ICoreWebView2DownloadStartingEventHandler, ICoreWebView2*,
                                       ICoreWebView2DownloadStartingEventArgs*>(
                [i, gen](ICoreWebView2*, ICoreWebView2DownloadStartingEventArgs* a) -> HRESULT {
                    if (!Live(i, gen)) return S_OK;
                    Check(a->put_Handled(TRUE), "download put_Handled");  // no download bubble; file still saves
                    overlay::ShowToast("Download saved to your Downloads folder");
                    return S_OK;
                });
            if (Check(core4->add_DownloadStarting(handler.Get(), &token), "add_DownloadStarting")) {
                ICoreWebView2_4* raw = core4.Get();
                raw->AddRef();
                v.detach.emplace_back([raw, token] {
                    Check(raw->remove_DownloadStarting(token), "remove_DownloadStarting");
                    raw->Release();
                });
            }
        }
        // Chat gets this inside its own combined script (registered before its first navigation).
        if (i != kChat)
            Check(v.core->AddScriptToExecuteOnDocumentCreated(Widen(chat_logic::NoTooltipScript()).c_str(), nullptr),
                  "AddScript no-tooltips");
    }
    Mute(v);
    return ok;
}

LRESULT CALLBACK ViewHostProc(HWND hwnd,UINT msg,WPARAM wp,LPARAM lp) {
    if (msg==WM_ERASEBKGND) {
        RECT r; GetClientRect(hwnd,&r);
        auto bg=theme::Get().background;
        HBRUSH brush=CreateSolidBrush(RGB(bg.r,bg.g,bg.b));
        FillRect(reinterpret_cast<HDC>(wp),&r,brush);DeleteObject(brush);return 1;
    }
    return DefWindowProcW(hwnd,msg,wp,lp);
}
void CreateView(int i) {
    if (g_shuttingDown || !g.environment || g.views[i].creating || g.views[i].controller) return;
    auto& v = g.views[i];
    v.creating = true;
    unsigned gen = ++v.generation;
    // Give each controller its own native host. Sibling WebView2 controllers
    // sharing one parent can reorder their internal child HWNDs when resized,
    // leaving the full-window black UI surface above the remote page.
    WNDCLASSW hostClass{};hostClass.hInstance=GetModuleHandleW(nullptr);hostClass.lpszClassName=L"CronqWebViewHost";hostClass.lpfnWndProc=ViewHostProc;
    RegisterClassW(&hostClass);
    v.host = CreateWindowExW(WS_EX_NOACTIVATE, L"CronqWebViewHost", L"", WS_CHILD | WS_CLIPCHILDREN | WS_CLIPSIBLINGS,
                             0, 0, 1, 1, g_parent, nullptr, GetModuleHandleW(nullptr), nullptr);
    if (!v.host) { Fail("Create view host", HRESULT_FROM_WIN32(GetLastError())); return; }
    Log("CHAT", "Creating controller view=%d generation=%u", i, gen);
    auto handler = MakeHandler<ICoreWebView2CreateCoreWebView2ControllerCompletedHandler, HRESULT,
                               ICoreWebView2Controller*>([i, gen](
                                                             HRESULT hr,
                                                             ICoreWebView2Controller* controller) -> HRESULT {
        if (g_shuttingDown || g.views[i].generation != gen) {
            if (controller) Check(controller->Close(), "stale controller Close");
            return S_OK;
        }
        auto& view = g.views[i];
        view.creating = false;
        if (FAILED(hr) || !controller) {
            Fail("CreateCoreWebView2Controller", FAILED(hr) ? hr : E_POINTER);
            return S_OK;
        }
        view.controller = controller;
        view.scriptReady = i != kChat;
        if (!Check(controller->put_IsVisible(FALSE), "initial visibility") ||
            !Check(controller->get_CoreWebView2(view.core.ReleaseAndGetAddressOf()), "get_CoreWebView2") ||
            !ConfigureView(i)) {
            Fail("ConfigureView", E_FAIL);
            return S_OK;
        }
        Log("CHAT", "Controller created view=%d", i);
        RefreshAppearance();
        ResizeHost();
        if (i == kPopup) {
            auto pending = g.popup;
            if (pending && pending->args) {
                Check(pending->args->put_NewWindow(view.core.Get()), "popup put_NewWindow");
                pending->Complete();
            }
            g.popup.reset();
        } else if (i == kChat) {
            // Navigate only after script registration finishes, so the first document receives it.
            auto scriptDone = MakeHandler<ICoreWebView2AddScriptToExecuteOnDocumentCreatedCompletedHandler,
                                          HRESULT, LPCWSTR>([gen](HRESULT result, LPCWSTR) -> HRESULT {
                if (!Live(kChat, gen)) return S_OK;
                Check(result, "AddScript completed"); // optional Enter helper must not block the page
                auto& chat = g.views[kChat];
                chat.scriptReady = true;
                std::string url = chat.pending.empty() ? "https://chatgpt.com" : chat.pending;
                chat.pending.clear();
                Navigate(chat, url);
                return S_OK;
            });
            hr = view.core->AddScriptToExecuteOnDocumentCreated(
                Widen(chat_logic::ChatGptUserScript() + ";" + chat_logic::NoTooltipScript() + ";" +
                      theme::WebScript()).c_str(),
                scriptDone.Get());
            if (!Check(hr, "AddScriptToExecuteOnDocumentCreated")) {
                view.scriptReady = true;
                Navigate(view, view.pending.empty() ? "https://chatgpt.com" : view.pending);
            }
        } else {
            std::string url = "https://cronq.ui/index.html";
            view.pending.clear();
            Navigate(view, url);
        }
        UpdateLayout();
        ui::PushState();
        return S_OK;
    });
    HRESULT hr = g.environment->CreateCoreWebView2Controller(v.host, handler.Get());
    if (FAILED(hr)) Fail("CreateCoreWebView2Controller start", hr);
}
void UpdateLayout() {
    if (g_shuttingDown) return;
    RECT client{};
    GetClientRect(g_parent, &client);
    for (int i = 0; i < 3; ++i) {
        auto& v = g.views[i];
        if (!v.controller) continue;
        // Keep the selected controller ready for CDP typing behind the hidden parent.
        bool visible = (g.muted && input::GetTypingTarget() != input::TypingTarget::Overlay) ? false
                     : i == kUi ? g.uiLoaded
                                : (g.uiLoaded && g.slotValid &&
                                   (i == kPopup ? bool(v.core)
                                                : !HasPopup() && i == kChat && g_app == "chatgpt"));
        const RECT placement = i == kUi ? client : g.slot;
        const int width = placement.right - placement.left;
        const int height = placement.bottom - placement.top;
        const bool moved = !v.layoutValid || !EqualRect(&v.placement, &placement);
        const bool visibilityChanged = !v.layoutValid || v.visible != visible;
        if (!moved && !visibilityChanged) continue;
        const bool resized = !v.layoutValid ||
            v.placement.right - v.placement.left != width ||
            v.placement.bottom - v.placement.top != height;
        // State notifications often carry the same rectangle. Avoid triggering
        // native layout/compositor work unless placement or visibility changes.
        bool ok = SetWindowPos(v.host, i == kUi ? HWND_BOTTOM : HWND_TOP,
                               placement.left, placement.top, width, height,
                               SWP_NOACTIVATE | (visible ? SWP_SHOWWINDOW : SWP_HIDEWINDOW)) != FALSE;
        if (resized) {
            RECT bounds{0, 0, width, height};
            ok &= Check(v.controller->put_Bounds(bounds), "put_Bounds");
        }
        if (visibilityChanged)
            ok &= Check(v.controller->put_IsVisible(visible ? TRUE : FALSE), "put_IsVisible");
        v.layoutValid = ok;  // Retry failed updates instead of caching them.
        v.placement = placement;
        v.visible = visible;
    }
}
#endif
}  // namespace

void RefreshAppearance() {
#ifdef CRONQ_WEBVIEW2
    const auto& p=theme::Get();
    for (int i=0; i<3; ++i) {
        auto& v=g.views[i];
        if (!v.core || !v.controller) continue;
        ComPtr<ICoreWebView2Controller2> background;
        auto bg=p.background;
        if (SUCCEEDED(v.controller.As(&background)))
            background->put_DefaultBackgroundColor({static_cast<BYTE>(theme::IsCamouflage() ? 0 : 255),static_cast<BYTE>(bg.r),static_cast<BYTE>(bg.g),static_cast<BYTE>(bg.b)});
        InvalidateRect(v.host,nullptr,TRUE);
        ComPtr<ICoreWebView2_13> core13;
        ComPtr<ICoreWebView2Profile> profile;
        if (SUCCEEDED(v.core.As(&core13)) && SUCCEEDED(core13->get_Profile(profile.ReleaseAndGetAddressOf())))
            Check(profile->put_PreferredColorScheme(p.light ? COREWEBVIEW2_PREFERRED_COLOR_SCHEME_LIGHT : COREWEBVIEW2_PREFERRED_COLOR_SCHEME_DARK), "profile color scheme");
        if (i==kChat) Check(v.core->ExecuteScript(Widen(theme::WebScript()).c_str(),nullptr),"ChatGPT appearance");
    }
#endif
}
bool Init(HWND parent) {
    g_parent = parent;
    g_shuttingDown = false;
    g_app = overlay::CurrentApp();
    g_failure.clear();
#ifdef CRONQ_WEBVIEW2
    g.slotValid = g.uiLoaded = g.contentStarted = false;
    g.muted = !overlay::IsVisible();
    wchar_t exe[32768];
    DWORD count = GetModuleFileNameW(nullptr, exe, 32768);
    if (!count || count >= 32768) {
        Fail("GetModuleFileNameW", E_FAIL);
        return false;
    }
    std::wstring dll(exe, count);
    dll = dll.substr(0, dll.find_last_of(L"\\/")) + L"\\WebView2Loader.dll";
    if (!g.loader) g.loader = LoadLibraryW(dll.c_str());
    if (!g.loader) {
        Fail("WebView2Loader.dll not found", HRESULT_FROM_WIN32(GetLastError()));
        return false;
    }
    using CreateEnvFn = HRESULT(STDAPICALLTYPE*)(PCWSTR, PCWSTR, ICoreWebView2EnvironmentOptions*,
                                                 ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler*);
    auto create =
        reinterpret_cast<CreateEnvFn>(GetProcAddress(g.loader, "CreateCoreWebView2EnvironmentWithOptions"));
    if (!create) {
        Fail("loader export", HRESULT_FROM_WIN32(GetLastError()));
        return false;
    }
    std::wstring folder = DataDir() + L"\\webview";
    if (!CreateDirectoryW(folder.c_str(), nullptr) && GetLastError() != ERROR_ALREADY_EXISTS) {
        Fail("CreateDirectory webview", HRESULT_FROM_WIN32(GetLastError()));
        return false;
    }
    Log("CHAT", "Creating environment userDataFolder='%s'", Narrow(folder).c_str());
    const unsigned environmentGeneration = ++g.environmentGeneration;
    auto handler = MakeHandler<ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler, HRESULT,
                               ICoreWebView2Environment*>(
        [environmentGeneration](HRESULT hr, ICoreWebView2Environment* environment) -> HRESULT {
            if (g_shuttingDown || g.environmentGeneration != environmentGeneration) return S_OK;
            Log("CHAT", "Environment callback hr=0x%08lX", hr);
            if (FAILED(hr) || !environment) {
                Fail("environment callback", FAILED(hr) ? hr : E_POINTER);
                return S_OK;
            }
            g.environment = environment;
            CreateView(kUi);
            return S_OK;
        });
    HRESULT hr = create(nullptr, folder.c_str(), nullptr, handler.Get());
    if (FAILED(hr)) {
        Fail("CreateCoreWebView2EnvironmentWithOptions", hr);
        return false;
    }
    return true;
#else
    g_failure = "WebView2 unavailable: this build has WebView2 support disabled.";
    LogErr("CHAT", "%s", g_failure.c_str());
    return false;
#endif
}
void Cleanup() {
    g_shuttingDown = true;
    ui::Reset();
#ifdef CRONQ_WEBVIEW2
    CancelDelayedRecoveries();
    if (g_retryTimer) KillTimer(nullptr, g_retryTimer);
    g_retryTimer = 0;
    g_retryParent = nullptr;
    if (g.popup) {
        g.popup->Complete();
        g.popup.reset();
    }
    for (int i = kPopup; i >= 0; --i) CloseView(i);
    g.environment.Reset();
    // DECISION: keep the loader mapped until process exit: pending COM callbacks may still use its code.
#endif
    g_parent = nullptr;
}
bool IsAvailable() {
#ifdef CRONQ_WEBVIEW2
    return !g_shuttingDown && bool(g.environment);
#else
    return false;
#endif
}
bool IsUiVisible() {
#ifdef CRONQ_WEBVIEW2
    return !g_shuttingDown && g.uiLoaded;
#else
    return false;
#endif
}
const std::string& FailureReason() { return g_failure; }
void SetActiveApp(const std::string& name) {
    g_app = name;
    if (input::GetTypingTarget() == input::TypingTarget::Overlay) PrepareTypingTarget();
#ifdef CRONQ_WEBVIEW2
    g.slotValid = false;
    UpdateLayout();
#endif
}
void GoBack() {
#ifdef CRONQ_WEBVIEW2
    if (Active().core && CanGoBack()) Check(Active().core->GoBack(), "GoBack");
#endif
}
void Reload() {
#ifdef CRONQ_WEBVIEW2
    if (!Active().core || !Check(Active().core->Reload(), "Reload"))
        Recover(ActiveIndex(), Active().generation);
#endif
}
bool CanGoBack() {
#ifdef CRONQ_WEBVIEW2
    BOOL yes = FALSE;
    return Active().core && Check(Active().core->get_CanGoBack(&yes), "get_CanGoBack") && yes;
#else
    return false;
#endif
}
void Pause() {
#ifdef CRONQ_WEBVIEW2
    g.muted = true;
    for (auto& v : g.views) Mute(v);
    UpdateLayout();  // puts every controller to IsVisible(FALSE) while muted
#endif
}
void Resume() {
#ifdef CRONQ_WEBVIEW2
    g.muted = false;
    for (auto& v : g.views) Mute(v);
    ResizeHost();
    UpdateLayout();
#endif
}
void Recover(unsigned index, unsigned generation) {
#ifdef CRONQ_WEBVIEW2
    const bool waited = (index & kRecoverNow) != 0;
    index &= ~kRecoverNow;
    if (g_shuttingDown || index > kEnvironment) return;
    // Ignore stale notifications before they count towards the backoff.
    if (index == kEnvironment ? (!g.environment || g.environmentGeneration != generation)
                   : g.views[index].generation != generation)
        return;
    if (!waited) {
        const int64_t delay = g_recoverBackoff[index].Next(NowUs() / 1000);
        if (delay > 0) {
            UINT_PTR timer = SetTimer(nullptr, 0, static_cast<UINT>(delay), DelayedRecoverProc);
            if (timer) {
                g_delayedRecover[timer] = {index, generation};
                LogErr("CHAT", "Repeated failure of view %u; recovering in %lld s.", index, (long long)(delay / 1000));
                return;
            }
        }
    }
    if (index == kEnvironment) {
        // Coalesce process-failure notifications from all views in this environment.
        if (!g.environment || g.environmentGeneration != generation) return;
        auto& chat = g.views[kChat];
        if (!chat.uri.empty() && chat.uri != "about:blank") chat.pending = chat.uri;
        HWND parent = g_parent;
        Cleanup();
        Init(parent);  // same user-data folder: retain cookies and sign-in
        return;
    }
    if (index >= kEnvironment || g.views[index].generation != generation) return;
    auto& v = g.views[index];
    if (index == kPopup) { ClosePopup(); return; }
    if (!v.uri.empty() && v.uri != "about:blank") v.pending = v.uri;
    if (index == kUi) { ui::Reset(); g.uiLoaded = false; }
    CloseView(index);
    CreateView(index);
#else
    (void)index;
    (void)generation;
#endif
}
void UniversalReset() {
    ClosePopup();
#ifdef CRONQ_WEBVIEW2
    Navigate(g.views[kChat], "https://chatgpt.com");
#endif
    // Reset's visible behavior is wired in overlay::UniversalReset (Linux parity).
}
void ResizeContent(const RECT& rect) {
#ifdef CRONQ_WEBVIEW2
    RECT client{};
    GetClientRect(g_parent, &client);
    g.slot = {std::clamp(rect.left, 0L, client.right), std::clamp(rect.top, 0L, client.bottom),
              std::clamp(rect.right, 0L, client.right), std::clamp(rect.bottom, 0L, client.bottom)};
    g.slot.right = std::max(g.slot.left, g.slot.right);
    g.slot.bottom = std::max(g.slot.top, g.slot.bottom);
    g.slotValid = g.slot.right > g.slot.left && g.slot.bottom > g.slot.top;
    UpdateLayout();
#else
    (void)rect;
#endif
}
void ResizeHost() {
#ifdef CRONQ_WEBVIEW2
    if (g_shuttingDown) return;
    UpdateLayout();
    for (auto& v : g.views)
        if (v.controller)
            Check(v.controller->NotifyParentWindowPositionChanged(), "NotifyParentWindowPositionChanged");
#endif
}
bool HasPopup() {
#ifdef CRONQ_WEBVIEW2
    return !g_shuttingDown && (g.popup || g.views[kPopup].core);
#else
    return false;
#endif
}
void ClosePopup() {
#ifdef CRONQ_WEBVIEW2
    if (g.popup) {
        g.popup->Complete();
        g.popup.reset();
    }
    CloseView(kPopup);
    g.slotValid = false;
    UpdateLayout();
    ui::PushState();
#endif
}
bool PostUiMessage(const std::string& message) {
#ifdef CRONQ_WEBVIEW2
    return !g_shuttingDown && g.views[kUi].core &&
           Check(g.views[kUi].core->PostWebMessageAsJson(Widen(message).c_str()), "PostWebMessageAsJson");
#else
    (void)message;
    return false;
#endif
}

#ifdef CRONQ_WEBVIEW2
ICoreWebView2* GetCurrentKeyTarget() {
    if (HasPopup() && g.views[kPopup].core) return g.views[kPopup].core.Get();
    if (g_app == "chatgpt" && g.views[kChat].core) return g.views[kChat].core.Get();
    if (g.views[kUi].core) return g.views[kUi].core.Get();
    return nullptr;
}
#endif

#ifdef CRONQ_WEBVIEW2
static bool HoverAllowed(int index,POINT point) {
    if(g_shuttingDown||!overlay::IsVisible()||hotkey::IsLocked()||
       input::GetGhostMode()!=input::GhostMode::Off||input::GetTypingTarget()!=input::TypingTarget::None||
       (g_app!="chatgpt"&&!HasPopup())||index!=(HasPopup()?kPopup:kChat))return false;
    auto& view=g.views[index];
    if(!view.core||!view.host||!IsWindowVisible(view.host))return false;
    HWND hit=WindowFromPoint(point);
    if(!hit)return false;
    HWND root=GetAncestor(hit,GA_ROOT);
    // Transparent Camouflage pixels may hit the app behind us. Reject windows
    // above the overlay, while allowing its transparent editor rectangles.
    for(HWND above=GetWindow(overlay::Hwnd(),GW_HWNDPREV);above;above=GetWindow(above,GW_HWNDPREV))
        if(above==root)return false;
    RECT bounds{};GetClientRect(view.host,&bounds);ScreenToClient(view.host,&point);
    return PtInRect(&bounds,point)!=FALSE;
}
#endif

void CheckTypingHover() {
#ifdef CRONQ_WEBVIEW2
    static POINT previous={-2147483647,-2147483647};
    POINT point{};
    if(!GetCursorPos(&point))return;
    if(point.x==previous.x&&point.y==previous.y)return;
    if(g_shuttingDown||!overlay::IsVisible()||hotkey::IsLocked()||
       input::GetGhostMode()==input::GhostMode::On||input::GetTypingTarget()!=input::TypingTarget::None||
       (g_app!="chatgpt"&&!HasPopup()))return;
    int index=HasPopup()?kPopup:kChat;
    auto& view=g.views[index];
    if(view.hoverPending)return;
    previous=point;
    if(!HoverAllowed(index,point))return;
    RECT bounds{};GetClientRect(view.host,&bounds);
    POINT local=point;ScreenToClient(view.host,&local);
    if(!PtInRect(&bounds,local))return;
    static const char *script=
#include "../../shared/hover_focus_script.inc"
    ;
    std::string js=std::string(script)+"("+std::to_string(local.x)+","+std::to_string(local.y)+","+
        std::to_string(bounds.right)+","+std::to_string(bounds.bottom)+")";
    unsigned generation=view.generation;
    view.hoverPending=true;
    auto done=MakeHandler<ICoreWebView2ExecuteScriptCompletedHandler,HRESULT,LPCWSTR>(
        [index,generation,point](HRESULT hr,LPCWSTR result)->HRESULT {
            if(Live(index,generation))g.views[index].hoverPending=false;
            POINT now{};
            if(!Live(index,generation)||FAILED(hr)||!result||wcscmp(result,L"true")||
               !GetCursorPos(&now)||now.x!=point.x||now.y!=point.y||!overlay::IsVisible()||hotkey::IsLocked()||
               input::GetGhostMode()!=input::GhostMode::Off||input::GetTypingTarget()!=input::TypingTarget::None)return S_OK;
            if(HoverAllowed(index,now)) input::SetTypingTarget(input::TypingTarget::Overlay);
            return S_OK;
        });
    if(FAILED(view.core->ExecuteScript(Widen(js).c_str(),done.Get())))view.hoverPending=false;
#endif
}

void PrepareTypingTarget() {
#ifdef CRONQ_WEBVIEW2
    if (g_shuttingDown || (g_app != "chatgpt" && !HasPopup())) return;
    auto* target = GetCurrentKeyTarget();
    if (!target) return;
    // DOM focus selects an editor inside the existing controller. Never activate the
    // HWND or call MoveFocus: Windows foreground focus must stay in the user's app.
    const wchar_t* script = LR"JS((()=>{
      const editable=e=>e && !e.disabled && !e.readOnly && e.getClientRects().length &&
        (e.isContentEditable || e.tagName==='TEXTAREA' ||
         (e.tagName==='INPUT' && /^(text|search|email|url|tel|number|password)$/i.test(e.type)));
      if(editable(document.activeElement)) return;
      const preferred=document.querySelector('#prompt-textarea');
      const editor=editable(preferred)?preferred:
        [...document.querySelectorAll('textarea,input,[contenteditable]')].find(editable);
      if(editor) editor.focus({preventScroll:true});
    })())JS";
    auto done = MakeHandler<ICoreWebView2ExecuteScriptCompletedHandler,HRESULT,LPCWSTR>(
        [](HRESULT hr,LPCWSTR)->HRESULT { Check(hr,"prepare typing target"); return S_OK; });
    Check(target->ExecuteScript(script,done.Get()),"prepare typing target ExecuteScript");
#endif
}

void DispatchKeyEvent(WPARAM vk, LPARAM lParam) {
#ifdef CRONQ_WEBVIEW2
    if (g_shuttingDown) return;
    ICoreWebView2* target = GetCurrentKeyTarget();
    if (!target) return;

    bool isDown = (lParam & 1) != 0;
    bool autoRepeat = (lParam & (1 << 25)) != 0;
    int scanCode = static_cast<int>((lParam >> 16) & 0xFF);

    key_translate::KeyInfo ki = key_translate::TranslateVk(static_cast<int>(vk), (lParam & (8 << 1)) != 0);
    if (ki.isModifier) return;

    int modifiers = static_cast<int>((lParam >> 1) & 15);

    std::string typeStr = isDown ? "keyDown" : "keyUp";
    std::string keyStr = ki.key;
    std::string codeStr = ki.code;
    std::string textStr;
    std::string unmodTextStr;

    if (isDown) {
        BYTE keyState[256]{};
        if (modifiers & 1) keyState[VK_MENU] = keyState[VK_RMENU] = 0x80;
        if (modifiers & 2) keyState[VK_CONTROL] = keyState[VK_LCONTROL] = 0x80;
        if (modifiers & 8) keyState[VK_SHIFT] = keyState[VK_LSHIFT] = 0x80;
        keyState[VK_CAPITAL] = (lParam & (1 << 6)) ? 1 : 0;
        keyState[VK_NUMLOCK] = (lParam & (1 << 7)) ? 1 : 0;
        if (vk < 256) keyState[vk] |= 0x80;
        WCHAR buf[16] = {0};
        HKL hkl = GetKeyboardLayout(GetWindowThreadProcessId(GetForegroundWindow(), nullptr));
        int res = ToUnicodeEx(static_cast<UINT>(vk), static_cast<UINT>(scanCode), keyState, buf, 8, 0x4, hkl);

        if (ki.printable && res > 0 && (!(modifiers & 3) || (modifiers & 3) == 3)) {
            std::string utf8 = Narrow(std::wstring(buf, res));
            keyStr = utf8;
            textStr = utf8;
            unmodTextStr = utf8;
            typeStr = "keyDown";
        } else {
            typeStr = "rawKeyDown";
            if (vk == VK_RETURN) {
                textStr = "\r";
                unmodTextStr = "\r";
            }
        }
    }

    std::ostringstream ss;
    ss << "{\"type\":\"" << typeStr << "\","
       << "\"key\":\"" << json::Escape(keyStr) << "\","
       << "\"code\":\"" << json::Escape(codeStr) << "\","
       << "\"windowsVirtualKeyCode\":" << static_cast<int>(vk) << ","
       << "\"modifiers\":" << modifiers;
    if (!textStr.empty()) {
        ss << ",\"text\":\"" << json::Escape(textStr) << "\""
           << ",\"unmodifiedText\":\"" << json::Escape(unmodTextStr) << "\"";
    }
    if (autoRepeat) {
        ss << ",\"autoRepeat\":true";
    }
    ss << "}";

    std::wstring params = Widen(ss.str());
    auto handler = MakeHandler<ICoreWebView2CallDevToolsProtocolMethodCompletedHandler, HRESULT, PCWSTR>(
        [](HRESULT hr, PCWSTR) -> HRESULT {
            Check(hr, "CDP input completion");
            return S_OK;
        });
    Check(target->CallDevToolsProtocolMethod(L"Input.dispatchKeyEvent", params.c_str(), handler.Get()),
          "Input.dispatchKeyEvent");
#else
    (void)vk;
    (void)lParam;
#endif
}

void DispatchClipboardAction(WPARAM vk, LPARAM lParam) {
#ifdef CRONQ_WEBVIEW2
    if (g_shuttingDown) return;
    ICoreWebView2* target = GetCurrentKeyTarget();
    if (!target) return;

    bool isDown = (lParam & 1) != 0;
    key_translate::KeyInfo ki = key_translate::TranslateVk(static_cast<int>(vk));

    if (!isDown) {
        std::string json = "{\"type\":\"keyUp\",\"key\":\"" + json::Escape(ki.key) + "\",\"code\":\"" +
                           json::Escape(ki.code) +
                           "\",\"windowsVirtualKeyCode\":" + std::to_string(static_cast<int>(vk)) +
                           ",\"modifiers\":2}";
        auto handler = MakeHandler<ICoreWebView2CallDevToolsProtocolMethodCompletedHandler, HRESULT, PCWSTR>(
            [](HRESULT hr, PCWSTR) -> HRESULT {
                Check(hr, "CDP input completion");
                return S_OK;
            });
        Check(
            target->CallDevToolsProtocolMethod(L"Input.dispatchKeyEvent", Widen(json).c_str(), handler.Get()),
            "clipboard dispatch");
        return;
    }

    if (vk == 'V') {
        PasteImageOrText();
        return;
    }

    std::string cmd;
    if (vk == 'A')
        cmd = "selectAll";
    else if (vk == 'C')
        cmd = "copy";
    else if (vk == 'X')
        cmd = "cut";
    else
        return;

    std::string json = "{\"type\":\"rawKeyDown\",\"key\":\"" + json::Escape(ki.key) + "\",\"code\":\"" +
                       json::Escape(ki.code) +
                       "\",\"windowsVirtualKeyCode\":" + std::to_string(static_cast<int>(vk)) +
                       ",\"modifiers\":2,\"commands\":[\"" + cmd + "\"]}";
    auto handler = MakeHandler<ICoreWebView2CallDevToolsProtocolMethodCompletedHandler, HRESULT, PCWSTR>(
        [](HRESULT hr, PCWSTR) -> HRESULT {
            Check(hr, "CDP input completion");
            return S_OK;
        });
    Check(target->CallDevToolsProtocolMethod(L"Input.dispatchKeyEvent", Widen(json).c_str(), handler.Get()),
          "clipboard dispatch");
#else
    (void)vk;
    (void)lParam;
#endif
}

void PasteImageOrText() {
#ifdef CRONQ_WEBVIEW2
    if (g_shuttingDown) return;
    ICoreWebView2* target = GetCurrentKeyTarget();
    if (!target) return;

    std::string b64Png;
    UINT pngFmt = RegisterClipboardFormatW(L"PNG");
    bool hasImage = IsClipboardFormatAvailable(CF_DIB) || IsClipboardFormatAvailable(CF_DIBV5) ||
                    (pngFmt && IsClipboardFormatAvailable(pngFmt));

    if (OpenClipboard(overlay::Hwnd())) {
        if (pngFmt && IsClipboardFormatAvailable(pngFmt)) {
            HANDLE hData = GetClipboardData(pngFmt);
            if (hData) {
                size_t size = GlobalSize(hData);
                const uint8_t* p = reinterpret_cast<const uint8_t*>(GlobalLock(hData));
                if (p) {
                    if (size > 0 && size <= image_logic::kMaxImageBytes) {
                        b64Png = snap_logic::Base64Encode(p, size);
                    }
                    GlobalUnlock(hData);
                }
            }
        } else {
            UINT format = IsClipboardFormatAvailable(CF_DIBV5) ? CF_DIBV5 : CF_DIB;
            HANDLE data = GetClipboardData(format);
            if (data) {
                const size_t size = GlobalSize(data);
                const auto* bytes = static_cast<const uint8_t*>(GlobalLock(data));
                if (bytes) {
                    int width = 0, height = 0;
                    std::vector<uint8_t> pixels, png;
                    if (image_logic::DecodeDib(bytes, size, &width, &height, &pixels) &&
                        image_codec::EncodePng(width, height, pixels.data(), &png))
                        b64Png = snap_logic::Base64Encode(png.data(), png.size());
                    else
                        LogErr("CHAT", "Unsupported or invalid clipboard bitmap");
                    GlobalUnlock(data);
                }
            }
        }
        CloseClipboard();
    }

    // An image that could not be read is reported, never replaced by an older screenshot from disk.
    if (hasImage && b64Png.empty()) {
        LogErr("CHAT", "Clipboard image could not be read; nothing pasted");
        overlay::ShowToast("Could not read the copied image");
        return;
    }

    if (!b64Png.empty()) {
        Log("CHAT", "Injecting synthetic image paste event (b64 length=%zu)", b64Png.size());
        std::string js =
            "(function() {\n"
            "  try {\n"
            "    var b64 = \"" +
            b64Png +
            "\";\n"
            "    var byteChars = atob(b64);\n"
            "    var byteNums = new Array(byteChars.length);\n"
            "    for (var i = 0; i < byteChars.length; i++) {\n"
            "      byteNums[i] = byteChars.charCodeAt(i);\n"
            "    }\n"
            "    var byteArray = new Uint8Array(byteNums);\n"
            "    var blob = new Blob([byteArray], { type: 'image/png' });\n"
            "    var file = new File([blob], 'screenshot.png', { type: 'image/png', lastModified: Date.now() "
            "});\n"
            "    var dt = new DataTransfer();\n"
            "    dt.items.add(file);\n"
            "    var target = document.activeElement || document.body;\n"
            "    var pasteEvent = new ClipboardEvent('paste', {\n"
            "      clipboardData: dt,\n"
            "      bubbles: true,\n"
            "      cancelable: true\n"
            "    });\n"
            "    target.dispatchEvent(pasteEvent);\n"
            "  } catch(e) {\n"
            "    console.error('cronq image paste error:', e);\n"
            "  }\n"
            "})();";
        auto handler = MakeHandler<ICoreWebView2ExecuteScriptCompletedHandler, HRESULT, LPCWSTR>(
            [](HRESULT hr, LPCWSTR) -> HRESULT {
                Check(hr, "image paste completion");
                return S_OK;
            });
        Check(target->ExecuteScript(Widen(js).c_str(), handler.Get()), "image paste ExecuteScript");
    } else {
        std::string json =
            "{\"type\":\"rawKeyDown\",\"key\":\"v\",\"code\":\"KeyV\","
            "\"windowsVirtualKeyCode\":86,\"modifiers\":2,\"commands\":[\"paste\"]}";
        auto handler = MakeHandler<ICoreWebView2CallDevToolsProtocolMethodCompletedHandler, HRESULT, PCWSTR>(
            [](HRESULT hr, PCWSTR) -> HRESULT {
                Check(hr, "CDP input completion");
                return S_OK;
            });
        Check(
            target->CallDevToolsProtocolMethod(L"Input.dispatchKeyEvent", Widen(json).c_str(), handler.Get()),
            "clipboard dispatch");
    }
#endif
}
}  // namespace chat_view
}  // namespace cronq
