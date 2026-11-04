# Cronq for Windows

Cronq is a floating companion window shaped like a phone (default 360x640 pixels, rounded corners, dark theme) that stays visible on top of other software—including borderless-fullscreen apps and games—without ever stealing keyboard focus. Inside it: a mobile browser, ChatGPT with persistent login sessions, Ghost Typer for human-like automated typing, clean screen captures (Snap) excluding the overlay, and the ability to type into the overlay without losing focus on whatever app you are using underneath.

The Windows port is a completely native, dedicated C++17 implementation built using Win32 and Microsoft Edge WebView2, intended to match the Linux version. Cross-compilation and Wine checks pass; native WebView2 and Windows desktop acceptance remain required. See [PARITY.md](PARITY.md) for evidence and limits.

---

## System Requirements

- **Operating System:** Windows 10 (version 2004 / build 19041 or later) or Windows 11 (64-bit x64).
- **Runtime:** Microsoft Edge WebView2 Evergreen Runtime (pre-installed on Windows 11 and recent Windows 10; automatically detected and installed by the setup installer if missing).
- **Privileges:** Standard user privileges (no Administrator rights required for normal installation and operation).

---

## Installation & Setup

### Option 1: Graphical Setup Installer (Recommended)
Download and run `cronq-setup-<version>.exe`:
- Installs per-user to `%LOCALAPPDATA%\Programs\Cronq` (no UAC prompt required).
- Creates a Start Menu shortcut ("Cronq").
- Automatically detects and bootstraps the WebView2 runtime if missing.
- Option to autostart Cronq on user sign-in (`HKCU\Software\Microsoft\Windows\CurrentVersion\Run`).
- Option to add Cronq to the user's `PATH`.

### Option 2: PowerShell Developer Install
From a PowerShell terminal in the repository root:
```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File windows/installer/install.ps1
```

### Option 3: Portable / Developer Build
```powershell
cmake -S windows -B windows/build -A x64
cmake --build windows/build --config Release
ctest --test-dir windows/build -C Release --output-on-failure
```
Executables are placed in `windows/build/Release/`:
- `cronq.exe`: Command-line interface and background launcher.
- `cronqw.exe`: Windowless overlay executable (runs silently in the background or supervised).
Keep `cronq.exe`, `cronqw.exe`, `WebView2Loader.dll`, and the `ui/` folder together in the same directory.

---

## Quick Start & Usage

```powershell
cronq                  # Launch and show the overlay (or bring to front if already running)
cronq show             # Ensure overlay is visible
cronq hide             # Hide the overlay
cronq toggle           # Toggle visibility
cronq status           # Query current state (mode, visibility, target, grab)
cronq --load <url>     # Load a webpage in the browser view
cronq --app chatgpt    # Switch to ChatGPT view
cronq --app ghost      # Switch to Ghost Typer view
cronq snap             # Capture screenshot (excluding overlay) to clipboard and disk
cronq kill             # Terminate running background overlay and supervisor
```

---

## Global Keyboard Shortcuts

All global shortcuts use the **Alt + key** chord. The focused application does not lose focus, and hotkeys are swallowed cleanly without opening menu bars.

| Shortcut | Action | Configurable Key | Default |
| --- | --- | --- | --- |
| **Alt+X** | Toggle overlay visibility (Show / Hide) | `toggle_key` | `x` |
| **Alt+G** | Toggle Ghost Mode (click-through transparency) | `ghost_key` | `g` |
| **Alt+A** | Decrease overlay opacity by 10% (minimum 20%) | *fixed* | `a` |
| **Alt+S** | Increase overlay opacity by 10% (maximum 100%) | *fixed* | `s` |
| **Alt+F** | Toggle Fullscreen mode | *fixed* | `f` |
| **Alt+D** | Reset to default size (360x640) and corner position | *fixed* | `d` |
| **Alt+P** | Snap screenshot (saved to `Pictures\cronq` & clipboard) | `snap_key` | `p` |
| **Alt+M** | Cycle typing mode: Mirror ⇄ Exclusive | `mode_key` | `m` |
| **Alt+W** | Ghost Writer start / cancel typing | `ghost_writer_key` | `w` |

---

## Mouse Controls

- **Top Drag Handle:** Click and drag to move the overlay anywhere on screen.
- **Double-Click Drag Handle:** Maximise height (expands overlay from screen top to bottom margin); double-click again to restore previous dimensions.
- **Edges & Corners:** Click and drag any outer edge or corner to resize (minimum size: 260x180 px).
- **Top Buttons:**
  - `▢`: Fullscreen toggle.
  - `✕`: Hide overlay (does not quit the application).
- **Status Pills:** Click the mode or ghost pill in the header to cycle key mode or toggle click-through.

---

## Configuration

Configuration is stored in INI format at:
`%APPDATA%\cronq\config.ini`

If no file exists, Cronq automatically generates one with defaults:
```ini
[General]
width = 360
height = 640
margin = 12
corner = bottom-left        # bottom-left, bottom-right, top-left, top-right
monitor = primary          # primary or monitor index (0, 1, ...)
opacity = 1.0              # 0.1 to 1.0
start_visible = false
default_key_mode = mirror  # mirror, exclusive

[Browser]
home_url = https://html5test.com
search_url_template = https://duckduckgo.com/?q=%s
user_agent = Mozilla/5.0 (Linux; Android 14; Pixel 8) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/124.0.0.0 Mobile Safari/537.36
chatgpt_allowlist = chatgpt.com,openai.com,oaistatic.com,oaiusercontent.com,auth0.com,google.com,google.co.in,gstatic.com,googleusercontent.com,googleapis.com,recaptcha.net,cloudflare.com,challenges.cloudflare.com,microsoft.com,microsoftonline.com,live.com,msftauth.net,apple.com

[Hotkeys]
toggle_key = x
mode_key = m
ghost_key = g
snap_key = p
ghost_writer_key = w
shield_clipboard_keys = true

[Snap]
snap_scope = monitor       # monitor (active) or all (virtual desktop)
snap_delay_ms = 80
snap_save = true
snap_keep_count = 20

[GhostWriter]
ghost_delay_ms = 0
ghost_newline = space      # space, return, skip
ghost_tab = space          # space, tab, skip
ghost_max_chars = 100000

[GhostTiming]
human = 1                  # 1 = human-like timing model, 0 = constant delay
speed = 0                  # 0 = slow (40 WPM), 1 = moderate (104 WPM), 2 = fast (141 WPM)
safe = 0                   # 1 = extra safe mode (includes occasional typos and pauses)
```

To reload configuration changes without restarting:
```powershell
cronq --reload-config
```

---

## Uninstallation & Purge

### Via Windows Settings
Go to **Settings → Apps → Installed apps**, select **Cronq**, and click **Uninstall**.
- Standard uninstall removes the application and autostart entries while preserving your settings, logins, and screenshot history in `%APPDATA%\cronq`.
- Check the **"Also delete my settings, logins and saved screenshots index (purge)"** checkbox to completely delete all local configuration and session state.

### Via Developer Script
```powershell
# Standard uninstall
powershell -NoProfile -ExecutionPolicy Bypass -File windows/installer/uninstall.ps1

# Full purge
powershell -NoProfile -ExecutionPolicy Bypass -File windows/installer/uninstall.ps1 -Purge
```

---

## Capabilities & Limitations on Windows

### What Cronq CAN do on Windows:
- **Stay on top:** Uses a guarded topmost watcher for borderless-fullscreen apps and other topmost windows. Test your games on Windows; exclusive fullscreen is unsupported.
- **Focus preservation:** Uses non-activating windows and restores external focus if a child view activates. Native WebView2 interactions still need Windows verification.
- **Clean screenshot captures:** Uses Windows Display Affinity (`WDA_EXCLUDEFROMCAPTURE`) so screenshots captured via Alt+P or `--snap` do not contain the overlay window.
- **Crash supervision:** `cronqw.exe --supervise` automatically restarts the overlay within 2 seconds if an unexpected fault occurs and records restart history in `crash.log`. An unhandled fault also writes `crash-<timestamp>-<pid>.log` with exception, module offset, and x64 registers.
- **DPI scaling:** Configuration sizes scale with monitor DPI. Moving between monitors keeps the current physical rectangle and rescales controls; Default Size uses the new DPI. Verify mixed-DPI behavior on Windows.

### What Cronq CANNOT bypass (Architectural & Security Limits):
- **Exclusive-Fullscreen DirectX/Vulkan Games:** Traditional exclusive-fullscreen applications seize direct hardware presentation buffers. Run games in **Borderless Windowed** mode to use overlays.
- **Elevated (Administrator) Windows without UIAccess:** Standard Windows User Interface Privilege Isolation (UIPI) forbids standard-user processes from injecting input into or staying above windows running as Administrator. (See [windows/UIACCESS.md](UIACCESS.md) for how to build and sign an optional UIAccess binary).
- **Secure System Desktops:** The Windows UAC consent prompt, the lock screen (`Ctrl+Alt+Del`), and credential manager screens run on isolated system desktops and cannot be overlaid.
- **Kernel-Level Anti-Cheat Systems:** Software utilizing kernel drivers (Vanguard, BattlEye, Easy Anti-Cheat) may monitor or block synthetic input injected by Ghost Typer.

---

## Troubleshooting

| Issue | Cause | Solution |
| --- | --- | --- |
| Overlay does not appear | Background mode or port conflict | Run `cronq show` or inspect live output with `cronq.exe --daemon` |
| `WebView2 unavailable` error in log | Microsoft Edge WebView2 runtime missing | Run the installer setup or download runtime from Microsoft: `https://go.microsoft.com/fwlink/p/?LinkId=2124703` |
| Text not typed into target app | Target app is running as Administrator | Run target as normal user or follow `windows/UIACCESS.md` to enable UIAccess build |
| Ghost mode clicks hitting overlay | Click-through disabled | Press **Alt+G** to enable transparent click-through mode |
| Antivirus false positive warning | Unsigned executable heuristic | Check the detection and build provenance; release builds should be signed with a trusted code-signing certificate |

---

## Automated verification

From Linux, with llvm-mingw, Wine, Xephyr, Python 3 and X11 tools installed:

```bash
windows/scripts/build-mingw.sh
windows/scripts/wine-smoke.sh
windows/scripts/wine-soak.sh             # full 30-minute run
```

`CRONQ_BIN_DIR=/absolute/build/path` selects a different build for the Wine scripts.
The soak uses actual process memory; Wine's zero handle count is reported as unavailable.
It never substitutes a fake baseline. Native Windows handle stability still needs checking.
CTest also runs a read-only Python audit of all Linux config section/key pairs when Python is available.
The cross-build fixture path uses Wine's Z: drive; a native Windows build uses native paths.

The installer was compiled with Inno Setup 6.7.3 and exercised in a disposable Wine prefix.
Runtime detection was seeded for that test; actual Evergreen installation is a Windows acceptance step.
See [the integration report](../antigravity/REPORT_INTEGRATION.md) for exact results.

## Windows acceptance checklist

Use Windows 10 22H2 and Windows 11 as a standard user. These steps cover paths Wine cannot establish.

1. **Install and upgrade.** Run setup without elevation; verify the Start Menu shortcut and all four payloads
   (`cronq.exe`, `cronqw.exe`, `WebView2Loader.dll`, `ui/`). Upgrade while Cronq is running. Select/deselect
   autostart on an upgrade and verify the Run entry follows the selection. With WebView2 absent, verify
   Evergreen installation and useful fallback instructions if the download fails.
2. **Autostart and recovery.** Sign out/in: Cronq starts hidden; Alt+X shows it. Under `--supervise`, terminate
   the daemon and verify restart plus `crash.log`. `cronq kill` must stop it without a restart loop.
3. **Focus.** Keep Notepad focused while clicking all overlay controls, links, inputs, drag/resize borders,
   and an OAuth popup. Notepad retains OS focus. Repeat over a browser, IDE and borderless game.
4. **Hotkeys.** Exercise Alt+X/G/A/S/F/D/P/M/W, configurable letters, held-key repeats, and Ctrl+Alt chords.
   Verify fullscreen restores the previous rectangle and the underlying app's Alt menu stays closed.
5. **Geometry and DPI.** Test all corners, taskbars on each edge, 100/150/200% scaling, mixed-DPI dragging,
   negative monitor coordinates, unplug/reconnect and an out-of-range monitor index (falls back to primary).
6. **Browser.** Load a bare hostname, URL and search; navigate Back, reload and stop. Verify progress,
   compact Home, expansion, persistent cookies, HTTPS errors and unavailable-runtime fallback.
7. **ChatGPT and OAuth.** Sign in using Google/Microsoft; complete internal popups, type in them, close them,
   navigate between providers, restart Cronq and verify the login persists. Check redirects against the allowlist.
8. **Mirror input.** Click the URL bar and web content while Notepad stays focused. Type letters, shifted
   punctuation, Caps Lock, arrows, Enter, Tab, delete, repeats, numpad and your keyboard layout. Both inputs
   receive keys in Mirror. Move from the URL bar to web content and verify typing follows the click.
9. **Exclusive input.** Repeat in Exclusive; Notepad receives no routed keystrokes. Hide, Reset, switch mode,
   click outside and wait 60 seconds idle: `TARGET=NONE`, with no stuck keys or modifier state.
10. **Clipboard shield.** Ctrl+A/C/X/V act on the overlay while routed. After a Snap, copy ordinary text:
    Ctrl+V must paste that text rather than an old screenshot. Test clipboard images from Paint and Snipping Tool.
11. **Screenshot.** Capture visible/hidden and solid/click-through overlays, primary/all screens, and rapid
    consecutive captures. The overlay is excluded and OS focus stays external. Paste into Paint and inspect
    both saved PNG and `latest_snap.png`. With default retention, 25 captures leave the newest 20 files.
12. **Writer settings.** Toggle Human-like, Extra safe and all speeds, restart, and verify settings persist.
    Existing INI comments and unrelated sections survive. Type in the editor then immediately click Start;
    the newest text is used. Verify countdown cancel and Alt+W cancel during typing.
13. **Writer content.** Test multiple lines, indentation, tabs, accents, CJK and emoji. Human mode shows live
    progress/ETA and correct final text; Instant mode sends batches quickly. A trailing newline never submits.
    Extra safe mode repairs its simulated typos. Test Caps Lock restoration and unsupported applications.
14. **Writer interruption.** Switch focus away during a long pause and back: typing must stay cancelled.
    Reset, sleep or lock during typing; no held letters remain and Exclusive routing releases. Holding a
    modifier when starting should wait briefly, then cancel with an instruction to release it.
15. **Privileges.** Normal build rejects an elevated target with a useful toast. If needed, build, sign and
    install the optional UIAccess variant following [UIACCESS.md](UIACCESS.md), then test elevated targets.
    Secure desktops and protected exclusive-fullscreen applications remain unsupported.
16. **Desktop recovery.** Sleep/resume, lock/unlock and restart Explorer. Hooks and topmost behavior recover;
    external focus remains external. Recheck input and snaps afterward.
17. **Stability.** Run the native app with WebView2 for 30 minutes of browsing, popups, writing and snaps.
    Inspect working set and handle/GDI counts in Process Explorer; repeatedly opening/closing views must
    not produce sustained growth. Inspect `%LOCALAPPDATA%\cronq` logs and test log rotation.
18. **Uninstall.** Normal uninstall retains settings/logins, removes program files, shortcut, Run entry and
    PATH entry added by setup. Purge is unchecked initially; selecting it removes roaming/local Cronq data.
    `Pictures\cronq` screenshots are preserved. Reinstall and verify the retained profile.
