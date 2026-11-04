# Phase 2 — Browser, ChatGPT and the real UI (WebView2 + HTML)

**Implementation note (22 October 2026):** see `REPORT_PHASE_2.md` for evidence and Windows-only checks.
The owner confirmed Reset should match Linux's overlay behavior: Home + Mirror/Solid. `browser::UniversalReset`
only resets browser views; `overlay::UniversalReset` performs the visible reset. The owner delegated toast layout:
windowed content briefly reserves space for HTML toasts, then restores it after 2200 ms.

**Goal:** replace the placeholder (GDI-drawn) body of the overlay with the real Cronq UI and the real browser/ChatGPT,
exactly like Linux, using WebView2. Typing *into* the overlay and the Ghost Typer *function* come in Phase 3.

Before starting: read `03_NON_NEGOTIABLE_RULES.md`, `04_WHAT_IS_ALREADY_DONE_PHASE1.md`, `09_WIN32_AND_COM_PITFALLS.md`,
and the Linux code listed for Phase 2 in `10_LINUX_SOURCE_READING_MAP.md`.

**Verification reality:** WebView2 does not run under Wine. Almost everything in this phase is therefore
**COMPILED + UNIT-TESTED (pure logic) but UNVERIFIED on Windows**. Compensate by (1) writing all pure logic as unit-tested
functions, (2) checking every HRESULT, (3) making the app degrade gracefully when WebView2 fails, (4) writing the manual
checklist and the failure playbook at the end of this file into `windows/README.md`.

---

## 0. Architecture (decided — do not change)

```
Overlay HWND  (already exists: layered, topmost, no-activate, rounded region, click-through toggle)
 ├─ UI controller       WebView2 #1  full client area. Loads https://cronq.ui/index.html (files in <exe dir>\ui\)
 │                       draws: header (pills, drag handle, 2 buttons), URL bar, Home page, Ghost page,
 │                       bottom nav, toasts, popup bar, resize edges
 ├─ Browser controller  WebView2 #2  content rectangle; visible only when app == browser
 ├─ ChatGPT controller  WebView2 #3  content rectangle; visible only when app == chatgpt
 └─ Popup controller    WebView2 #4  created on demand (OAuth / login popups); shown in a modal layer
```

- All four share **one** `ICoreWebView2Environment` with user-data folder `DataDir() + L"\\webview"`
  → one cookie/login store, like Linux's shared `WebKitWebContext`. Logins persist across restarts automatically.
- **Windowed hosting** (`CreateCoreWebView2Controller(hwnd, ...)`). Each controller is a child window of the overlay.
- The UI page is served from disk via a virtual host: `SetVirtualHostNameToFolderMapping(L"cronq.ui", <exe dir>\ui, ALLOW)`
  on `ICoreWebView2_3`. No resource embedding.
- Native ⟷ UI messages are small flat JSON objects (§3).
- Content controllers are **inset 6 px (scaled) from the left, right and bottom** so the UI controller still shows the
  phone "bezel" around them. The bezel strips receive the mouse for resize handles.

**Why windowed, not composition hosting:** simplest, least code, most likely to work first time. The risk (documented in
§9 and the failure playbook) is how WebView2 behaves inside a `WS_EX_LAYERED` + `WS_EX_NOACTIVATE` window. We cannot test
that here, so we add a **focus-guard safety net** (§6) and the playbook.

---

## 1. Step-by-step task list

Do these in order. After each step: build with zero warnings, run `wine-smoke.sh` (must stay 22/22), then continue.

### Step 2.1 — Switch the build to WebView2 by default

1. In `windows/CMakeLists.txt` change `option(CRONQ_WEBVIEW2 ... OFF)` to `ON`.
2. Add a post-build step copying the whole `windows/ui/` folder next to `cronq.exe`/`cronqw.exe`:
   `add_custom_command(TARGET cronqw POST_BUILD COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_CURRENT_SOURCE_DIR}/ui $<TARGET_FILE_DIR:cronqw>/ui)`
   (create `windows/ui/` with a stub `index.html` first so the command works).
3. Update `windows/scripts/build-mingw.sh` default: `CRONQ_WEBVIEW2="${CRONQ_WEBVIEW2:-ON}"`.
4. Add `ole32 oleaut32` to link libraries if not already (the CMake block for the option does this).
5. Rebuild; confirm `webview2_compile_check.exe` and `WebView2Loader.dll` appear. Run the smoke test: still 22/22.

### Step 2.2 — Pure logic first (unit-tested)

Create these **without any Win32 calls**, with a unit test each (`windows/tests/browser_logic_test.cpp`, `jsonlite_test.cpp`),
run under Wine, add to the smoke script:

**`src/browser_logic.h/.cpp`** (namespace `cronq::browser_logic`)

1. `std::string NormalizeInput(const std::string& input, const std::string& searchTemplate)` — exact port of
   `load_uri_internal` in `src/browser.c`:
   - Trim spaces/tabs/newlines. Empty → return empty (caller does nothing).
   - Starts with `http://`, `https://`, `file://` or `about:` → return as is.
   - Else if it contains **no space** and (contains `.` or starts with `localhost`) → `"https://" + trimmed`.
   - Else it is a search: percent-encode with `g_uri_escape_string(trimmed, NULL, TRUE)` semantics: every byte is
     escaped as `%XX` (upper-case hex) **except** unreserved ASCII `A-Z a-z 0-9 - . _ ~` and **except bytes ≥ 0x80**
     (UTF-8 is left as is). Then if the template contains `%s`, replace the **first** `%s` with the encoded text;
     otherwise use `https://duckduckgo.com/?q=` + encoded.
   - Test vectors: `"hello world"` → `https://duckduckgo.com/?q=hello%20world`; `"example.com"` → `https://example.com`;
     `"localhost:3000"` → `https://localhost:3000`; `"cats"` → search; `"a b.com"` → search (has a space);
     `"https://x.y/z?q=1"` unchanged; `"  rust  "` → search `rust`; `"a&b=c/d"` → `a%26b%3Dc%2Fd`; `"naïve"` → `naïve` unchanged.
2. `bool IsAuthUri(const std::string& uri)` — true if uri contains any of `oauth`, `auth0`, `accounts.google`, `signin`,
   `challenge`, `login`, `SetSID` (case-sensitive substring, from `on_decide_policy` in browser.c).
3. `std::string ExtractHost(const std::string& uri)` — text between `://` and the next `/`, `?` or `#`; drop `user:pass@`;
   drop `:port` (handle `[ipv6]`); empty if no `://`.
4. `bool IsDomainAllowedForChatGpt(const std::string& uri, const std::string& allowlistCsv)` — exact port of
   `browser_is_domain_allowed_for_chatgpt` (read it line by line):
   empty uri → true; starts with `about:`, `data:`, `blob:`, `javascript:` or `file:` → true; empty host → true;
   fast-path true if host contains `google.` or ends with any of `.google.com gstatic.com googleusercontent.com
   googleapis.com recaptcha.net cloudflare.com openai.com oaistatic.com oaiusercontent.com auth0.com microsoft.com
   microsoftonline.com live.com msftauth.net apple.com` (these suffix checks are plain `ends_with`, **no** dot required);
   otherwise split `allowlistCsv` on any of `, ; space`; for each non-empty token: allowed if host equals token
   (ASCII case-insensitive), or host ends with `"." + token` (case-insensitive), or host **contains** the token (this
   third rule is in the Linux code; copy it).
   Tests: `https://chatgpt.com/` allowed with the default list; `https://evil.example/` not allowed; `https://accounts.google.com/x` allowed;
   `https://sub.openai.com` allowed; `https://example.org/` not allowed; `about:blank` allowed.
5. `std::string ChatGptUserScript()` returns the JS in §4.

**`src/jsonlite.h/.cpp`** (namespace `cronq::json`) — flat JSON only:
`bool ParseFlat(const std::string& text, std::map<std::string,std::string>* out)` for objects whose values are strings,
numbers, `true`/`false`/`null` (store numbers/bools as their text; strings unescaped: `\" \\ \/ \b \f \n \r \t \uXXXX`
incl. surrogate pairs → UTF-8). Reject nested objects/arrays by returning false. And `std::string Escape(const std::string&)`
producing a JSON string body (escape `" \ \b \f \n \r \t`, control chars < 0x20 as `\u00XX`; leave UTF-8 as is).
Test with: empty object, nested (must fail), escapes, unicode, whitespace, numbers like `-1.5e3`, trailing garbage (must fail).

### Step 2.3 — COM, environment and the `browser` module

Create `src/browser.h/.cpp` (namespace `cronq::browser`). Public API mirrors Linux `browser.h` (same names, PascalCase):

```cpp
bool Init(HWND parent);                 // false => caller shows the fallback UI
void Cleanup();
bool IsAvailable();                     // true once the environment exists
void SetActiveApp(const std::string&);  // "home","browser","chatgpt","ghost" -> shows/hides controllers
void Load(const std::string& input);            // active view
void LoadInBrowser(const std::string& input);
void LoadInChatGpt(const std::string& input);
void GoBack(); void GoForward(); void Reload(); void Stop(); void GoHome();
bool CanGoBack(); bool IsLoading(); double Progress();
std::string CurrentUri(); std::string CurrentTitle();
void Pause(); void Resume();            // mute / unmute
void UniversalReset();
void ResizeContent(const RECT& contentRect);   // called from overlay on every size/position change
bool HasPopup(); void ClosePopup();
```

Implementation rules:

1. **COM apartment.** At the very start of `RunDaemon()` (main.cpp, before `overlay::Init`): `CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED)`;
   `CoUninitialize()` at the very end. The message loop must keep running (it already does).
2. **Load the loader dynamically** (works for MSVC and MinGW, no import library needed):
   `LoadLibraryW(<exe dir>\WebView2Loader.dll)`, then `GetProcAddress(h, "CreateCoreWebView2EnvironmentWithOptions")`
   with the typedef shown in `tests/webview2_compile_check.cpp`. Missing DLL or export → `Init` returns false, log
   `LogErr("BROWSER", "WebView2Loader.dll not found ...")`.
3. Create the environment with `userDataFolder = DataDir() + L"\\webview"`, `browserExecutableFolder = nullptr`,
   options = `nullptr`. Use `MakeHandler<ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler, HRESULT, ICoreWebView2Environment*>`.
   If the callback gets a failed HRESULT (runtime not installed: `0x80070002`) log the HRESULT in hex, set a
   `g_failureReason` string ("WebView2 runtime not installed. Get it at https://go.microsoft.com/fwlink/p/?LinkId=2124703"),
   and tell the overlay to show it in the fallback body.
4. **Creation chain (async, one after another, never nested more than needed):**
   environment → UI controller (navigate to `https://cronq.ui/index.html`) → on its first `NavigationCompleted`: browser
   controller (navigate to `cfg.homeUrl` if non-empty) → ChatGPT controller (navigate `https://chatgpt.com`; Linux preloads it too).
   Keep a single `struct State` (file-scope) with the ComPtrs and flags; every callback first checks `g_shuttingDown`.
5. **Per-controller setup** (write one helper `ConfigureView(ICoreWebView2*, ICoreWebView2Controller*)`):
   - `QueryInterface` to `ICoreWebView2Settings2` → `put_UserAgent(Widen(cfg.userAgent))`.
   - `ICoreWebView2Settings`: `put_IsStatusBarEnabled(FALSE)`; leave dev tools/context menus at defaults (Linux has dev extras on).
   - `ICoreWebView2Controller2::put_DefaultBackgroundColor({255,0,0,0})` (opaque black, like Linux).
   - Register event handlers (store the `EventRegistrationToken`s and remove them in `Cleanup`): `NavigationStarting`,
     `NavigationCompleted`, `SourceChanged`, `DocumentTitleChanged`, `HistoryChanged`, `NewWindowRequested`
     (browser & ChatGPT only), `WindowCloseRequested` (popup only), `WebMessageReceived` (UI only), `ProcessFailed` (all).
   - Do **not** use device emulation. Window sizes are DPI-scaled so the page sees ~360 CSS px; the user-agent is enough.
6. **Hosting:** `put_Bounds` with RECTs in **client pixels of the overlay window**. Provide `ResizeContent(RECT)`; overlay
   calls it from `ApplyGeometry()` and `WM_SIZE`. UI controller bounds = whole client rect. Content rect = client rect
   minus the top status bar (and, for the browser app, the URL row + progress bar), minus the bottom nav, inset 6 px
   left/right/bottom; all values `S()`-scaled. **Take the exact heights from the Linux CSS** (`min-height`, `padding`,
   `margin` of the status bar, `.url-bar-container`, `.bottom-nav` in `s_phone_css`; Linux's own click-routing code treats
   roughly 28 px as the status bar, ~70 px as status bar + URL row and ~40 px as the bottom nav, which is a sanity check,
   not the source of truth). Better still: make the UI page report the pixel rectangle of its "content slot" element
   (`getBoundingClientRect()` × devicePixelRatio) with a `{"type":"slot","x":..,"y":..,"w":..,"h":..}` message and let native
   use that rectangle for the content controllers, so HTML layout and native bounds can never disagree.
   Hide inactive controllers with `put_IsVisible(FALSE)`.
7. **Navigation policy.** In `NavigationStarting` of the **ChatGPT** controller only:
   `if (uri non-empty && !IsAuthUri(uri) && !IsDomainAllowedForChatGpt(uri, cfg.chatgptAllowlist))` → `put_Cancel(TRUE)`, log
   `"ChatGPT navigating to external domain '%s' -> Diverting to Browser app."`, then `overlay::SwitchApp("browser")` and `LoadInBrowser(uri)`.
8. **New windows** (`NewWindowRequested`, browser and ChatGPT) — DECISION (cannot reproduce Linux's `nav_type` exactly):
   - If `IsAuthUri(uri)` **or** the event is *not* user-initiated → create the popup controller (§5) and hand it back with
     `args->put_NewWindow(popupCore)` using a **deferral** (`args->GetDeferral`, `Complete()` in the creation callback);
     this keeps `window.opener`, which OAuth sign-in needs.
   - Else (a normal `target=_blank` link click, uri non-empty and not `about:blank`) → `put_Handled(TRUE)` and `Navigate(uri)` in the same view.
9. **Mute** when hidden: `ICoreWebView2_8::put_IsMuted(TRUE)` on all views from `Pause()`; `FALSE` in `Resume()`.
   `overlay::Hide()` calls `browser::Pause()`, `overlay::Show()` calls `browser::Resume()` (as Linux does).
10. **UniversalReset()** (Linux `browser_universal_reset`): close any popup; close and recreate the browser controller
    and navigate it to `homeUrl`; ChatGPT: `Navigate("https://chatgpt.com")` (keeps login); reset the browser's active view to browser.
    The overlay reset restores Mirror/Solid, clears editors, switches Home, and shows `Cronq reset to new state` (owner-confirmed Linux behavior).
11. **Shutdown (`Cleanup`)**: set `g_shuttingDown`; remove event handlers with the saved tokens; call `controller->Close()` on
    each; `Reset()` all `ComPtr`s; then return. Do this **before** `CoUninitialize` and before the window is destroyed.
12. **Process failure** (`ProcessFailed`): log kind + exit code; for a renderer/browser crash call `Reload()` once; never loop.
13. **Every callback** is wrapped: `try { ... } catch (...) { LogErr(...); }` and returns `S_OK`.

### Step 2.4 — Native ⟷ UI bridge

Create `src/ui_bridge.h/.cpp` (namespace `cronq::ui`):

- `PostToUi(const std::string& jsonObject)` → `ICoreWebView2::PostWebMessageAsJson(Widen(json))` on the UI controller
  (queue messages until the UI page has sent `ready`).
- `PushState()` builds the state object (§3) and posts it. Call it from every place that changes state
  (`input::SetKeyMode`, `SetGhostMode`, `overlay::SwitchApp`, opacity, size, browser callbacks, popup open/close).
  `overlay::QueueDraw()` already is called in those places: add `ui::PushState()` there.
- `OnMessage(const std::string& json)` parses with `json::ParseFlat` and dispatches (§3). Unknown types: log and ignore.
- `overlay::ShowToast(msg)` → if the UI is ready: post `{"type":"toast","text":...}` (the JS shows it for 2200 ms);
  otherwise keep the GDI toast fallback that exists today.

### Step 2.5 — The HTML UI (`windows/ui/`)

Files: `index.html`, `cronq.css`, `cronq.js`, `chatgpt_enter.js` (copied from Linux, §4). No frameworks, no npm, no CDNs,
no external fonts (system UI font stack: `"Segoe UI", system-ui, sans-serif`). Everything must work offline.

**Replicate the Linux look exactly.** Source of truth is `src/window.c`:
- Colours, radii, paddings, font sizes: the GTK CSS string `s_phone_css` (starts at line ~249). Translate property by
  property to normal CSS (`border-radius`, `padding`, `background-color`, `color`, `font-size`, `min-height`...). Class
  names in the CSS become class names in your HTML (`.phone-root`, `.top-ctrl-btn`, `.move-capsule`, `.pill-*`,
  `.home-search-bar`, `.nav-button`, `.url-entry`, `.toast-box` ...).
- Icons: the 19 SVG strings at the top of `window.c` (`s_svg_*`, lines ~27–145). Copy them **verbatim** inline in
  `index.html` (as `<svg>` elements). Do not redraw or approximate them.
- Structure (read `window_init`, lines ~1740–2110): root column = **status bar** (left: Mirror/Exclusive pill, Solid/Ghost pill,
  grab-alert pill hidden unless grab failed; centre: move-handle capsule; right: **two** buttons — fullscreen and close; close
  only hides) → **central viewport** (pages: home, chatgpt, browser, ghost; toast box bottom-centre with 14 px margin) →
  **bottom nav** with five equal buttons in this order: **Back, Home, Reset, Typer(Ghost), Snap** (icon above a text label;
  the Ghost and Snap buttons switch icons by state, see below).
- Pages:
  - **Home** (`create_home_page`): 8 px spacer; search-bar launcher "Search or type URL..." (click → app browser + focus the URL
    input); section label `APPLICATIONS`; three app icons: ChatGPT, Chrome (= browser), Ghost Typer; flexible spacer.
  - **Browser**: URL row (search icon, input with placeholder `Search or enter website...`, reload/stop button that swaps icon
    while loading), progress bar under it (hidden when not loading), then the browser content (the native WebView sits here).
  - **ChatGPT**: no URL bar; native content fills the page.
  - **Ghost** (`create_ghost_page`): title "Ghost Typer"; auto-growing monospace editor (min 72 px, max 180 px, placeholder
    "Type here"); counter text exactly `"%d chars · %d line%s"` (plural `s` unless 1 line; the dot is `·`); "Human-like" switch;
    a block that hides when Human-like is off containing the "Extra safe" switch and three equal mode buttons
    `Slow` `Moderate` `Fast` (radio behaviour); a status row (`Ready`, and an ETA label on the right, hidden when empty);
    a full-width `Start` button (text becomes `Cancel` while running). In Phase 2 the page is **visual only**: Start shows
    the toast `Ghost Writer arrives in Phase 3`. Phase 3 wires it. Everything above the status row scrolls; the status row
    and Start button are pinned at the bottom.
- **The UI never gets keyboard focus** (the overlay is never the active window). Do not rely on `:focus` styles for core
  UX; the caret/typing is delivered in Phase 3. But clicks on buttons/inputs must work (they are mouse events).
- Toasts: a pill at the bottom centre; fade in/out; visible 2200 ms; replaces the previous toast.
- **Window dragging and resizing from HTML**: on `mousedown` over the capsule post `{"type":"drag","kind":"move"}`; on
  `mousedown` over an invisible 8 px strip along the left/right/bottom/top edges or the bottom-right grip post
  `{"type":"drag","kind":"resize","edges":N}` with `edges` = bitmask L=1 R=2 T=4 B=8. Set the CSS cursor yourself
  (`nwse-resize`, `ew-resize`, ...). Double-click on the capsule posts `{"type":"dblclick-handle"}`. Do not implement
  the movement in JS; native does it (next bullet).
- **Native drag support in `overlay.cpp`** (add, do not rewrite): `void BeginExternalDrag(bool move, int edges)` stores
  start cursor/window rect (reuse `g_drag*` variables), sets `g_drag`, and starts `SetTimer(g_hwnd, kDragTimerId, 8, nullptr)`.
  In `WM_TIMER` for that id: if `GetAsyncKeyState(VK_LBUTTON) & 0x8000` is clear → stop the timer and clear `g_drag`; else call
  `OnMouseMove()` (it already uses `GetCursorPos`). This works even though the mouse button went down in another process's window.
- The old GDI drawing (`Paint`, hit-testing) stays as the **fallback** used only when WebView2 is unavailable. Keep it
  compiling and working. Fix the fallback header to show **two** buttons (fullscreen, close) like Linux (the Phase 1
  placeholder had three).

### Step 2.6 — Wire the IPC commands (in `app.cpp`)

| Command | Behaviour |
| --- | --- |
| `LOAD <url>` | `overlay::SwitchApp("browser")`; `browser::LoadInBrowser(url)` (note: Linux compares the prefix case-sensitively) |
| `APP <name>` | `overlay::SwitchApp(name)` |
| `BACK` | `overlay::Back()`: see Linux `overlay_back` (go back in the active view; at the start of history go Home) |
| `HOME` | `overlay::SwitchApp("home")` |
| `RELOAD` | `browser::Reload()` |
| `RESET` / `UNIVERSAL_RESET` | `overlay::UniversalReset()` (calls browser reset, returns Home + Mirror/Solid) |
| `HAS_MODAL` | `TRUE` if a popup is open else `FALSE` |
| `CLOSE_MODAL` | `browser::ClosePopup()` |
| `STATUS` | unchanged (APP now reflects reality) |

Also make `overlay::SwitchApp` call `browser::SetActiveApp`, compact-mode logic stays as it is (home = 280 px tall, others 640).

### Step 2.7 — Focus-guard safety net

New `src/focus_guard.h/.cpp`: `SetWinEventHook(EVENT_SYSTEM_FOREGROUND, ..., WINEVENT_OUTOFCONTEXT)` (always on while the app runs).
Keep `g_lastExternal` = the most recent foreground window that is **not** the overlay or a child of it. When the new foreground
window **is the overlay HWND or `IsChild(overlay, hwnd)`**, call `SetForegroundWindow(g_lastExternal)` if `IsWindow(g_lastExternal)`,
and `LogErr("FOCUS_GUARD", "Overlay received focus; restored 0x%p")`. Do **not** react to other top-level windows of our process
(file pickers, WebView2 permission prompts) — they may legitimately take focus. Rate-limit to 10 restores per second. This is
the safety net for the biggest unknown: whether clicking inside a WebView2 child of a `WS_EX_NOACTIVATE` window activates it.

### Step 2.8 — Graceful fallback

If `browser::Init()` returns false or the environment callback fails: keep running with the GDI placeholder UI, draw the failure
reason in the body, keep IPC and hotkeys working. **This is exactly what happens under Wine, so the smoke test covers it.**
Add a smoke check: daemon starts, `status` works, the log contains a `BROWSER` line explaining WebView2 is unavailable, and no crash.

### Step 2.9 — Docs and checklist

Add `## Phase 2 manual checklist` to `windows/README.md` (§8 below) and the failure playbook (§9).

---

## 2. Linux behaviours to match (details you must not forget)

- ChatGPT and Browser are **two views, one login store**. Both are preloaded at startup. ChatGPT home = `https://chatgpt.com`.
- Closing/hiding the overlay mutes web audio; showing unmutes.
- The browser's URL bar shows the current address; the page title is not shown (Linux has no title bar for it).
- Back button: sensitive only when not on Home. Home screen = app `home`.
- Popups are shown inside the overlay as a **modal layer**: a bar with the title `Popup / Sign In` and a close (X) button, the
  popup content below it. Linux treats `y < 65` px as the bar. Read `overlay_show_popup_modal` (window.c ~1595–1680) and the
  `.popup-modal-*` CSS. When the popup closes itself (`WindowCloseRequested`) the modal disappears. Only one popup at a time.
- When ChatGPT is active the compact/expanded behaviour is: expanded (640). Browser/ghost: expanded. Home: compact (280).
- `--load` while hidden still switches app and loads.
- Toast texts you will emit: `Cronq reset to new state`, `Ghost Writer arrives in Phase 3` (temporary), plus the Phase 1 ones.

## 3. Native ⟷ UI message protocol

All messages are **flat JSON objects** with a `"type"` field.

UI → native:

| type | fields | native action |
| --- | --- | --- |
| `ready` | — | mark UI ready, flush queued messages, `PushState()` |
| `slot` | `x`,`y`,`w`,`h` (client pixels, already multiplied by devicePixelRatio) | `browser::ResizeContent(rect)`; send on load, on resize and whenever the app/page changes |
| `nav` | `app` = home/browser/chatgpt/ghost | `overlay::SwitchApp(app)` |
| `back` | — | `overlay::Back()` |
| `reset` | — | `overlay::UniversalReset()` |
| `snap` | — | Phase 3 (toast "Snap arrives in Phase 3" for now) |
| `reload` / `stop` | — | `browser::Reload()` / `Stop()` |
| `url` | `value` | `browser::LoadInBrowser(value)` (and switch to browser app) |
| `search-home` | — | switch to browser, focus URL input (UI does the focus; native switches app) |
| `window` | `action` = fullscreen / hide | `overlay::SetFullscreen()` / `overlay::Hide()` |
| `pill` | `which` = mode / ghost | `input::CycleKeyMode()` / `ToggleGhostMode()` |
| `drag` | `kind` = move / resize, `edges` (int) | `overlay::BeginExternalDrag(...)` |
| `dblclick-handle` | — | maximise-height toggle (existing `ToggleMaximizedHeight`, make it callable) |
| `popup-close` | — | `browser::ClosePopup()` |

Native → UI:

| type | fields |
| --- | --- |
| `state` | `mode` ("mirror"/"exclusive"), `ghost` (bool), `grab` ("none"/"active"/"failed"), `app`, `opacity` (0–1), `width`, `height`, `canBack` (bool), `loading` (bool), `progress` (0–1), `url`, `popup` (bool), `scale` (DPI scale, 1.0 = 96 dpi) |
| `toast` | `text` |

Rules: never trust incoming values (validate `app`, clamp numbers); ignore unknown types; never block.
Build outgoing JSON with `json::Escape`. Keep messages small; `state` is sent whole every time (it is cheap).

## 4. The ChatGPT "Enter sends" script

Copy the JavaScript in `src/browser.c` (`s_chatgpt_enter_script`, lines ~41–124) **verbatim** into `windows/ui/chatgpt_enter.js`
and embed the same text in `ChatGptUserScript()`. Register it on the ChatGPT controller with
`ICoreWebView2::AddScriptToExecuteOnDocumentCreated`. **Wrap it** so it is safe at document-creation time (Linux injects at
document end; on Windows `document.documentElement` may not exist yet and the original would throw):

```js
(function () {
  function run() { /* ORIGINAL SCRIPT BODY, unchanged */ }
  if (document.readyState === 'loading') document.addEventListener('DOMContentLoaded', run);
  else run();
})();
```

(The original contains its own IIFE; paste it inside `run` or call it from `run` — keep its logic untouched.)

## 5. The popup / login modal

- Created inside the `NewWindowRequested` deferral: `env->CreateCoreWebView2Controller(overlayHwnd, handler)`; apply
  `ConfigureView`; `args->put_NewWindow(popupCore)`; `deferral->Complete()`.
- Bounds: the modal region below the 65 px bar (scaled), inset like content. Show the modal bar in the **UI page** (HTML)
  when `state.popup` is true; the bar's close button posts `popup-close`.
- `WindowCloseRequested` on the popup → close controller, release, push state. `ClosePopup()` does the same.
- Popup uses the same environment, so cookies/logins are shared.

## 6. Mouse and focus facts you must keep in mind

- A click in a WebView2 child window never reaches our `WndProc`; `WM_MOUSEACTIVATE → MA_NOACTIVATE` still protects the
  parent, but Chromium's own window may behave differently. That is why §Step 2.7 exists.
- Click-through (Ghost mode, `WS_EX_TRANSPARENT` on the top-level) is expected to make child windows click-through too
  (they are skipped together with the parent in hit-testing). This is UNVERIFIED; it is in the manual checklist.
- Opacity (`SetLayeredWindowAttributes`) applies to the whole window tree on Windows 8+.
- The rounded `SetWindowRgn` clips children too (corners stay rounded). Call it after every size change (already done in `WM_SIZE`).

## 7. Deliverables (what "Phase 2 done" means)

1. Builds with **zero warnings** (llvm-mingw) and the CMake file is still valid for MSVC.
2. `wine-smoke.sh`: the original 22 checks still pass **plus** the new fallback check and unit tests
   (`browser_logic_test`, `jsonlite_test`).
3. All new pure logic has unit tests that pass under Wine.
4. `windows/ui/` complete and visually matching Linux (compare against Linux screenshots if the owner provides them; otherwise
   against `s_phone_css`).
5. `windows/README.md` has the Phase 2 manual checklist and the failure playbook.
6. `git diff` shows no Linux file changed.
7. A report following `11_HOW_TO_REPORT_BACK.md`.

## 8. Phase 2 manual checklist (copy into `windows/README.md`, then the owner runs it on Windows)

1. Install/confirm the *Microsoft Edge WebView2 Runtime* (present on Windows 11 and updated Windows 10). Run `cronq.exe --daemon --show`.
2. The overlay shows the **real UI** (dark phone with pills, handle, two top buttons, Home screen with search bar and three
   app icons, bottom nav with 5 buttons) — not the plain text placeholder. Log shows `BROWSER` lines with no errors.
3. Open Notepad, type a few letters, then **click every button and the home icons in the overlay**. The Notepad caret/focus must never
   change (keep typing in Notepad between clicks: text must still appear in Notepad). If focus is stolen, note which click did it.
4. Click Chrome → the browser page loads `https://html5test.com`. Type a URL into the CLI: `cronq.exe --load example.com` → loads.
5. Click ChatGPT → chatgpt.com loads. Log in (including "Continue with Google": a popup should appear inside the overlay with the
   title *Popup / Sign In*; finishing sign-in closes it). Quit with `cronq.exe kill`, start again: still logged in.
6. In ChatGPT click a link to an unrelated site → it opens in the Browser app instead.
7. Drag the centre handle: window moves. Drag edges and the bottom-right grip: window resizes. Double-click the handle: height maximises/restores.
8. Alt+G: overlay becomes click-through **including the web content** (clicks reach windows behind it). Alt+G again restores.
9. Alt+A / Alt+S change opacity of the **whole** overlay including web content. Alt+X hides/shows; audio is muted while hidden.
10. Reset button: Browser returns to home page, ChatGPT stays logged in.
11. Set Windows display scaling to 150%: the overlay is bigger (540x960 px) and text is sharp; content still lays out as a ~360 px phone.

## 9. Failure playbook (for the follow-up after the owner's first Windows run)

| Symptom | Most likely cause | What to try (in this order) |
| --- | --- | --- |
| Overlay shows UI but web content areas are blank/black | WebView2 child inside layered window not composing | (1) call `SetLayeredWindowAttributes` again after each controller is created; (2) call `controller->put_Bounds` again after `put_IsVisible(TRUE)`; (3) test with `LWA_ALPHA` alpha 255 only; (4) if still blank, switch to **composition hosting** (`CreateCoreWebView2CompositionController`, DirectComposition visual tree, `WS_EX_NOREDIRECTIONBITMAP`, opacity via a DComp effect) — larger change, ask the owner first |
| Clicking the overlay changes the foreground app | Chromium child activates parent | confirm `FOCUS_GUARD` log lines; if the guard restores focus the user still sees flicker → investigate `WM_MOUSEACTIVATE` via a `SetWindowsHookEx(WH_CBT/WH_GETMESSAGE)` or move to composition hosting |
| Ghost mode clicks still hit the web content | child windows not skipped | add `EnableWindow(childHwnd, FALSE)` for the controllers' HWNDs while ghost mode is on (find them with `EnumChildWindows`) |
| `Init` fails with `0x80070002` | runtime missing | expected message in the overlay; install the runtime |
| `Init` fails with `0x8007139F` / `0x80070057` | bad user-data folder path or permissions | log the path; try `%TEMP%`; ensure the folder exists |
| ChatGPT login popup never appears | `NewWindowRequested` handled as in-place navigation | log every `NewWindowRequested` (uri, isUserInitiated); adjust the heuristic in §Step 2.3-8 |
| Text in web pages never shows a caret | expected until Phase 3 | n/a |
