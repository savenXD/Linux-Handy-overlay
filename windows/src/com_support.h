// Tiny COM helpers for the WebView2 code, so it needs neither WRL nor WIL:
//   ComPtr<T>                         smart pointer (AddRef/Release)
//   MakeHandler<Interface, Args...>() turns a lambda into a WebView2 "...Handler" COM object
//
// Example:
//   env->CreateCoreWebView2Controller(hwnd,
//       MakeHandler<ICoreWebView2CreateCoreWebView2ControllerCompletedHandler, HRESULT,
//                   ICoreWebView2Controller*>([](HRESULT hr, ICoreWebView2Controller* c) -> HRESULT {
//           ...
//           return S_OK;
//       }).Get());
#pragma once

#include "com_ptr.h"
#include "webview2_include.h"  // also provides the __uuidof fix for MinGW

#include <functional>
#include <utility>

namespace cronq {

// COM object implementing `Interface` (a WebView2 callback interface with a single Invoke method) by
// calling a std::function. `Args` must list the Invoke parameter types exactly as the SDK declares them.
template <typename Interface, typename... Args>
class ComHandler final : public Interface {
   public:
    using Fn = std::function<HRESULT(Args...)>;
    explicit ComHandler(Fn fn) : fn_(std::move(fn)) {}

    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** out) override {
        if (!out) return E_POINTER;
        if (IsEqualIID(riid, __uuidof(Interface)) || IsEqualIID(riid, __uuidof(IUnknown))) {
            *out = static_cast<Interface*>(this);
            AddRef();
            return S_OK;
        }
        *out = nullptr;
        return E_NOINTERFACE;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return (ULONG)InterlockedIncrement(&refs_); }
    ULONG STDMETHODCALLTYPE Release() override {
        ULONG r = (ULONG)InterlockedDecrement(&refs_);
        if (r == 0) delete this;
        return r;
    }
    HRESULT STDMETHODCALLTYPE Invoke(Args... args) override {
        try {
            return fn_ ? fn_(args...) : S_OK;
        } catch (...) {
            LogErr("BROWSER", "Exception contained in COM callback");
            return S_OK;
        }
    }

   private:
    ~ComHandler() = default;  // only Release() deletes
    volatile LONG refs_ = 1;
    Fn fn_;
};

// Returns a ComPtr holding the handler (refcount 1). Pass `.Get()` to the WebView2 call; it AddRefs what it
// keeps.
template <typename Interface, typename... Args, typename F>
ComPtr<Interface> MakeHandler(F&& fn) {
    ComPtr<Interface> p;
    p.Attach(
        new ComHandler<Interface, Args...>(typename ComHandler<Interface, Args...>::Fn(std::forward<F>(fn))));
    return p;
}

}  // namespace cronq
