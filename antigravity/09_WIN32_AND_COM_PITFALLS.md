# Win32 / COM / WebView2 pitfalls checklist

Because nobody can run the code on Windows while you write it, you must prevent bugs by discipline. **Re-read this before
starting each phase and again before you report.** Each item is something that has broken real projects.

## A. Compile-time and language

- [ ] Zero warnings with `-Wall -Wextra`. A warning about an unused parameter is fine to silence with `(void)x;` or an unnamed parameter; every other warning is a real bug until proven otherwise.
- [ ] Every function you call exists with the signature you think. If you are not 100 % sure, `grep` the header under `~/.local/llvm-mingw/x86_64-w64-mingw32/include/` or the WebView2 header. Never guess an API.
- [ ] Wide vs narrow: only `...W` functions; string literals for Win32 are `L"..."`; `%ls` in `swprintf`, `%s` in `snprintf` with UTF-8 `char*`. Convert with `Widen`/`Narrow`.
- [ ] `wchar_t` is 16-bit on Windows: UTF-16 code units, not code points. Emoji = two units. Count code points yourself (surrogate pairs) when the *number of characters* matters.
- [ ] No `long` for 64-bit values (`long` is 32-bit on Windows). Use `int64_t`, `LONGLONG`, `ULONG_PTR`.
- [ ] Pointer-sized message params: `WPARAM`/`LPARAM`/`ULONG_PTR`/`INT_PTR`. Casting a pointer to `int` truncates on x64.
- [ ] `std::string::operator[]` on an empty string, `.front()` on empty, `s.substr(pos)` with `pos > size()` (throws) — check sizes. No exceptions are allowed to escape callbacks.
- [ ] MinGW + MSVC both: no `__attribute__`, `__builtin_*`, VLAs, `#pragma comment(lib...)` (ignored by MinGW; put libraries in CMake), `ssize_t`, `strcasecmp` (use `EqNoCase`), `localtime_r` (use `GetLocalTime`), `usleep` (use `Sleep` only off the UI thread — or better, timers).
- [ ] `std::min/max` need `NOMINMAX` (already defined in `common.h`); do not include `<windows.h>` before `common.h` in a way that skips it — always include `common.h` first.
- [ ] Integer overflow in size computations (`width * height * 4`): use `size_t`/`int64_t`.
- [ ] Lambdas passed to Win32 callbacks (`SetTimer`, `SetWinEventHook`, `EnumWindows`) must be **captureless and have the right calling convention** → use a named `CALLBACK` function instead (this exact bug was caught in Phase 1).

## B. Threads and message loop

- [ ] **Only the UI thread** touches: the overlay window, WebView2, COM objects created on it, `g_` window state. Other threads communicate by `PostMessage` (or the `ipc::Request` ref-counted hand-off).
- [ ] `PostMessage` with a pointer: someone must own and free it exactly once. Prefer small integers in `wParam/lParam`. If you pass a heap object, document the owner (see `ipc::Request`).
- [ ] `SendMessage` to a window of another thread blocks until handled: never call it from the hook thread or while holding a lock the UI thread may need.
- [ ] Never block the UI thread: no `Sleep`, no `WaitForSingleObject` on anything slow, no synchronous file/network I/O beyond tiny local files. Use timers (`SetTimer`) and async callbacks.
- [ ] `SetTimer(nullptr, 0, ...)` thread timers fire only on the thread that created them and need a message loop (the UI thread and the hook thread have one). Kill every timer you create.
- [ ] A timer callback can fire after you "stopped" the thing it serves (message already queued): guard with a state flag at the top of the handler.
- [ ] Shutdown order matters: stop timers and hooks → close WebView2 controllers → release COM objects → `CoUninitialize` → destroy the window → stop threads (see `RunDaemon`). Do not touch COM after `CoUninitialize`.
- [ ] Do not hold a `CRITICAL_SECTION` while calling out (SendInput, COM, PostMessage is OK but SendMessage is not).

## C. Hooks (`WH_KEYBOARD_LL`, `WH_MOUSE_LL`)

- [ ] The callback must return in **well under 300 ms** (aim for microseconds). No logging with disk I/O in the hot path (log only on state *changes*), no COM, no `SendInput`, no `SendMessage`, no allocation if avoidable.
- [ ] Always call `CallNextHookEx` unless you deliberately swallow (`return 1`). Swallowing a key-down without its key-up (or vice versa) leaves apps with stuck keys — track swallowed keys (see `g_swallowed`).
- [ ] Events we inject must be tagged `dwExtraInfo = kInjectTag` and the hook must pass tagged events through. Never inject from inside the hook.
- [ ] `GetAsyncKeyState` reflects the *physical* state for keys other than the one in the current event; use it for Ctrl/Shift/Alt checks (already done in `AltOnly`).
- [ ] Hooks can be silently removed by Windows → the periodic re-install exists; keep it.
- [ ] A bug in Exclusive mode can leave the user with a dead keyboard. Every swallow-all path needs the safety releases listed in the Phase 3 doc and a log line when engaged/released.

## D. Window and focus (the product's #1 rule)

- [ ] Never `SetForegroundWindow/SetFocus/SetActiveWindow/BringWindowToTop/AttachThreadInput` for the overlay. Never `ShowWindow` with `SW_SHOW/SW_SHOWNORMAL/SW_RESTORE` on it.
- [ ] Z-order only via `SetWindowPos(hwnd, HWND_TOPMOST, ..., SWP_NOACTIVATE)`.
- [ ] After changing extended window styles call `SetWindowPos(... SWP_FRAMECHANGED | SWP_NOACTIVATE | SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER)` (see `SetGhost`).
- [ ] `SetWindowRgn` takes ownership of the region: never `DeleteObject` it afterwards.
- [ ] GDI objects: every `Create*` paired with `DeleteObject` (and `SelectObject` the old one back before deleting). `GetDC` with `ReleaseDC`, `CreateCompatibleDC` with `DeleteDC`.
- [ ] DPI: the process is per-monitor-v2 aware: all coordinates are **physical pixels**; scale design numbers with `S()`.

## E. WebView2 specifics

- [ ] `CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED)` on the UI thread **before** the first WebView2 call; the thread must pump messages.
- [ ] Every WebView2 call is **asynchronous**: you get results in callbacks, possibly after your object/state is gone. Check `g_shuttingDown` at the top of every callback and never capture raw pointers to short-lived objects.
- [ ] Check every `HRESULT` with `FAILED(hr)`; log it in hex (`0x%08lX`). A failed call must not crash.
- [ ] `ComPtr` handlers that hold a `ComPtr` to the object that owns the handler create reference cycles → leaks. Break cycles in `Cleanup` (remove event handlers with the saved `EventRegistrationToken`, `Close()` controllers, reset pointers).
- [ ] Strings returned as `LPWSTR` (e.g. `get_Uri`) must be freed with `CoTaskMemFree`. Wrap in a tiny RAII helper.
- [ ] `ICoreWebView2Controller::Close()` before releasing; after `Close()` do not use the controller.
- [ ] `put_Bounds` uses **client pixels of the parent window** (physical). After the parent moves nothing is needed; after it resizes call `put_Bounds`.
- [ ] Include only `webview2_include.h` / `com_support.h` (they fix MinGW `__uuidof`). Never include `WebView2EnvironmentOptions.h` (needs WRL).
- [ ] Event handler `Invoke` signatures must exactly match the SDK header; copy them from `WebView2.h`, not from memory. The template `MakeHandler<Interface, Args...>` fails to compile if they differ — good, that is your safety net.
- [ ] Scripts injected into pages run in the page's world: keep them small, wrapped in try/catch, idempotent (guard with a window flag like the Linux script does).
- [ ] `CallDevToolsProtocolMethod` parameters are a **JSON string**: build it with `json::Escape`; a malformed string returns an error HRESULT through the callback, it does not throw.
- [ ] Under Wine none of this runs. That is why `browser::Init()` failing must leave the app fully usable.

## F. Files, paths, registry

- [ ] Paths may contain spaces and non-ASCII characters: always quote when building command lines; use wide APIs.
- [ ] Use `DataDir()` / `ConfigDir()`; create directories with `CreateDirectoryW` one level at a time (or a helper that walks the path) and ignore `ERROR_ALREADY_EXISTS`.
- [ ] Config file may be saved by Notepad with a **UTF-8 BOM** and **CRLF** (the parser handles both; do the same in any writer: preserve line endings).
- [ ] Do not assume the working directory; resolve files relative to the exe directory (`GetModuleFileNameW`).
- [ ] Registry writes only under `HKCU`.
- [ ] A file may be locked by another process (antivirus, Explorer): retry a few times (20 ms apart) before failing, and never loop forever.

## G. Process and misc

- [ ] `CreateProcessW` needs a **mutable** command-line buffer (`&cmd[0]`), and `CloseHandle` on both returned handles.
- [ ] `GetModuleFileNameW` can truncate: use a `MAX_PATH*2` buffer and check the return value.
- [ ] `WaitNamedPipe`/`CreateFile` on a pipe returns `ERROR_PIPE_BUSY` when the single instance is busy: retry (already implemented in `ipc::Send`).
- [ ] `GetTickCount` wraps at 49 days; use `NowUs()` / `GetTickCount64` for intervals.
- [ ] Random numbers: seed once from `QueryPerformanceCounter`; never `rand()` for the typing model (use the `Rng` class).
- [ ] Keep `kVersion` and user-visible texts identical to the spec; typos in a toast are bugs (parity).

## H. First-try correctness techniques

1. **Make illegal states impossible**: one enum for state machines (Idle/Countdown/Typing/Done/Failed) and one function that changes state and updates UI.
2. **Small pure functions** for everything computable, each with a unit test run under Wine.
3. **Log every async step** (`Log("BROWSER", "env created hr=0x%08lX")`) so a failure on the owner's machine is diagnosable from `cronq.log` alone.
4. **Fail soft**: if anything unexpected happens, log it, show a toast if relevant, keep the app running.
5. **Diff review**: before reporting, read your whole diff top to bottom once, slowly, with this checklist open.
