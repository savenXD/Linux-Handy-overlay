// Compile-and-link check for the WebView2 helpers (com_support.h). It is not a functional test and is never
// run; it exists so a build proves the SDK headers, the MinGW UUID declarations and the handler template agree.
#include "com_support.h"
using namespace cronq;

typedef HRESULT(STDAPICALLTYPE* CreateEnvFn)(PCWSTR, PCWSTR, ICoreWebView2EnvironmentOptions*,
                                             ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler*);

static ComPtr<ICoreWebView2Controller> g_controller;
static ComPtr<ICoreWebView2> g_view;

int main() {
    HMODULE loader = LoadLibraryW(L"WebView2Loader.dll");
    CreateEnvFn create = loader ? (CreateEnvFn)GetProcAddress(loader, "CreateCoreWebView2EnvironmentWithOptions") : nullptr;
    if (!create) return 1;
    HWND hwnd = nullptr;
    auto envDone = MakeHandler<ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler, HRESULT, ICoreWebView2Environment*>(
        [hwnd](HRESULT hr, ICoreWebView2Environment* env) -> HRESULT {
            if (FAILED(hr) || !env) return hr;
            env->CreateCoreWebView2Controller(
                hwnd, MakeHandler<ICoreWebView2CreateCoreWebView2ControllerCompletedHandler, HRESULT, ICoreWebView2Controller*>(
                          [](HRESULT hr2, ICoreWebView2Controller* c) -> HRESULT {
                              if (FAILED(hr2) || !c) return hr2;
                              g_controller = c;
                              g_controller->get_CoreWebView2(g_view.ReleaseAndGetAddressOf());
                              EventRegistrationToken tok;
                              g_view->add_NavigationStarting(
                                  MakeHandler<ICoreWebView2NavigationStartingEventHandler, ICoreWebView2*,
                                              ICoreWebView2NavigationStartingEventArgs*>(
                                      [](ICoreWebView2*, ICoreWebView2NavigationStartingEventArgs* a) -> HRESULT {
                                          LPWSTR uri = nullptr;
                                          a->get_Uri(&uri);
                                          CoTaskMemFree(uri);
                                          a->put_Cancel(TRUE);
                                          return S_OK;
                                      }).Get(),
                                  &tok);
                              return S_OK;
                          }).Get());
            return S_OK;
        });
    create(nullptr, nullptr, nullptr, envDone.Get());
    return 0;
}
