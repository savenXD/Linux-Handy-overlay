#include "god_copy.h"
#include "god_copy_protocol.h"
#include <objbase.h>
#include <oleauto.h>
#include <uiautomation.h>
#include <wrl/client.h>
#include <algorithm>
#include <cwchar>
#include <vector>

namespace cronq::god_copy {
namespace {
using Microsoft::WRL::ComPtr;
HWND Window(const Packet& p) { return reinterpret_cast<HWND>(static_cast<uintptr_t>(p.window)); }
bool SameTarget(const Packet& p) {
    DWORD pid = 0;
    HWND h = Window(p);
    if (!IsWindow(h) || GetForegroundWindow() != h) return false;
    DWORD thread = GetWindowThreadProcessId(h, &pid);
    GUITHREADINFO info{};
    info.cbSize = sizeof(info);
    return pid == p.process && GetGUIThreadInfo(thread, &info) &&
           reinterpret_cast<uintptr_t>(info.hwndFocus) == p.focus;
}
Result NativeSelection(Packet& p) {
    HWND focus = reinterpret_cast<HWND>(static_cast<uintptr_t>(p.focus));
    wchar_t name[64]{};
    GetClassNameW(focus, name, 64);
    // Only the standard EDIT protocol: never send arbitrary private messages to custom controls.
    if (_wcsicmp(name, L"Edit") != 0) return Result::Unsupported;
    if (GetWindowLongPtrW(focus, GWL_STYLE) & ES_PASSWORD) return Result::Password;
    auto send = [&](UINT msg, WPARAM wp, LPARAM lp, DWORD_PTR& result) {
        return SendMessageTimeoutW(focus, msg, wp, lp, SMTO_ABORTIFHUNG | SMTO_BLOCK, 250, &result) != 0;
    };
    DWORD start = 0, end = 0;
    DWORD_PTR ignored = 0, length = 0;
    if (!send(EM_GETSEL, reinterpret_cast<WPARAM>(&start), reinterpret_cast<LPARAM>(&end), ignored))
        return Result::Failed;
    if (end <= start) return Result::NoSelection;
    if (end - start > kMaxText) return Result::TooLarge;
    if (!send(WM_GETTEXTLENGTH, 0, 0, length)) return Result::Failed;
    // Bound temporary document allocation; do not silently truncate a selection.
    if (length > kMaxText || end > length) return Result::TooLarge;
    std::vector<wchar_t> text(static_cast<size_t>(length) + 1);
    struct Eraser {
        std::vector<wchar_t>& text;
        ~Eraser() { SecureZeroMemory(text.data(), text.size() * sizeof(wchar_t)); }
    } eraser{text};
    DWORD_PTR copied = 0;
    if (!send(WM_GETTEXT, text.size(), reinterpret_cast<LPARAM>(text.data()), copied) || copied != length)
        return Result::Changed;
    DWORD startAfter = 0, endAfter = 0;
    if (!send(EM_GETSEL, reinterpret_cast<WPARAM>(&startAfter), reinterpret_cast<LPARAM>(&endAfter), ignored) ||
        startAfter != start || endAfter != end) {
        return Result::Changed;
    }
    p.length = end - start;
    std::copy_n(text.data() + start, p.length, p.text);
    p.text[p.length] = 0;
    return Result::Ok;
}
Result AccessibleSelection(Packet& p) {
    ComPtr<IUIAutomation> automation;
    HRESULT hr = CoCreateInstance(__uuidof(CUIAutomation), nullptr, CLSCTX_INPROC_SERVER,
                                  IID_PPV_ARGS(&automation));
    if (FAILED(hr)) return Result::Unsupported;
    ComPtr<IUIAutomationElement> focused;
    if (FAILED(automation->GetFocusedElement(&focused)) || !focused) return Result::Unsupported;
    BOOL password = TRUE;
    if (FAILED(focused->get_CurrentIsPassword(&password)) || password) return Result::Password;
    ComPtr<IUIAutomationTreeWalker> walker;
    ComPtr<IUIAutomationElement> root;
    if (FAILED(automation->get_RawViewWalker(&walker)) || !walker ||
        FAILED(automation->ElementFromHandle(Window(p), &root)) || !root) return Result::Unsupported;
    // Browser accessibility providers can live in a renderer process. Verify ancestry in the
    // target window rather than requiring every provider to share the top-level window's PID.
    bool contained = false;
    ComPtr<IUIAutomationElement> ancestor = focused;
    for (int depth = 0; ancestor && depth < 64; ++depth) {
        BOOL same = FALSE;
        if (FAILED(automation->CompareElements(ancestor.Get(), root.Get(), &same))) return Result::Failed;
        if (same) { contained = true; break; }
        ComPtr<IUIAutomationElement> parent;
        if (FAILED(walker->GetParentElement(ancestor.Get(), &parent))) return Result::Failed;
        ancestor = parent;
    }
    if (!contained) return Result::Changed;
    ComPtr<IUIAutomationElement> element = focused;
    for (int depth = 0; element && depth < 64; ++depth) {
        if (FAILED(element->get_CurrentIsPassword(&password)) || password) return Result::Password;
        ComPtr<IUIAutomationTextPattern> pattern;
        hr = element->GetCurrentPatternAs(UIA_TextPatternId, IID_PPV_ARGS(&pattern));
        if (SUCCEEDED(hr) && pattern) {
            ComPtr<IUIAutomationTextRangeArray> ranges;
            if (FAILED(pattern->GetSelection(&ranges))) return Result::Failed;
            if (!ranges) return Result::NoSelection;
            int count = 0;
            if (FAILED(ranges->get_Length(&count)) || count < 0 || count > 256) return Result::Failed;
            p.length = 0;
            for (int i = 0; i < count; ++i) {
                ComPtr<IUIAutomationTextRange> range;
                if (FAILED(ranges->GetElement(i, &range)) || !range) return Result::Failed;
                BSTR text = nullptr;
                // Request one extra unit so oversized text is rejected, never treated as complete.
                hr = range->GetText(static_cast<int>(kMaxText + 1), &text);
                if (FAILED(hr)) { SysFreeString(text); return Result::Failed; }
                UINT size = SysStringLen(text);
                bool separator = p.length && size;
                if (size > kMaxText - p.length || (separator && size == kMaxText - p.length)) {
                    if (text) SecureZeroMemory(text, size * sizeof(wchar_t));
                    SysFreeString(text);
                    return Result::TooLarge;
                }
                if (separator) p.text[p.length++] = L'\n';
                if (size) std::copy_n(text, size, p.text + p.length);
                p.length += size;
                if (text) SecureZeroMemory(text, size * sizeof(wchar_t));
                SysFreeString(text);
            }
            ComPtr<IUIAutomationElement> after;
            BOOL same = FALSE;
            if (FAILED(automation->GetFocusedElement(&after)) || !after ||
                FAILED(automation->CompareElements(focused.Get(), after.Get(), &same)) || !same)
                return Result::Changed;
            p.text[p.length] = 0;
            return p.length ? Result::Ok : Result::NoSelection;
        }
        BOOL atRoot = FALSE;
        if (FAILED(automation->CompareElements(element.Get(), root.Get(), &atRoot)) || atRoot) break;
        ComPtr<IUIAutomationElement> parent;
        if (FAILED(walker->GetParentElement(element.Get(), &parent))) break;
        element = parent;
    }
    return Result::Unsupported;
}
}

int Worker(uintptr_t mapping) {
    HANDLE handle = reinterpret_cast<HANDLE>(mapping);
    auto* p = static_cast<Packet*>(MapViewOfFile(handle, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(Packet)));
    if (!p) return 2;
    if (p->magic != kMagic || !SameTarget(*p)) {
        p->result = Result::Changed;
    } else {
        p->backend = 1;
        Result result = NativeSelection(*p);
        if (result == Result::Unsupported || result == Result::TooLarge) {
            p->backend = 2;
            HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
            if (SUCCEEDED(hr)) {
                result = AccessibleSelection(*p);
                CoUninitialize();
            } else result = Result::Failed;
        }
        p->result = SameTarget(*p) ? result : Result::Changed;
        if (p->result != Result::Ok) SecureZeroMemory(p->text, sizeof(p->text));
    }
    UnmapViewOfFile(p);
    CloseHandle(handle);
    return 0;
}
}
