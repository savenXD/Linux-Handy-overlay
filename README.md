# 📱 Cronq

> A sleek, floating, non-focus-stealing companion overlay designed for Linux and Windows. It stays visible on top of all windows without ever stealing your typing focus.

```text
   +-------------------------------------------------------------+
   | (● Mirror) (● Ghost)    [ ≡ Drag Move ]        —   ▢   ✕   |  <- Header: Status Pills, Move Handle, Window Controls
   | [ 🔍 https://duckduckgo.com                               ] |  <- URL / Search Entry
   |-------------------------------------------------------------|
   |                                                             |
   |                     WebKitGTK 4.1 View                      |  <- Embedded Mobile Browser
   |                   (Android Mobile View)                     |     (ChatGPT, Web Apps, Docs)
   |                                                             |
   |   +-----------------------------------------------------+   |
   |   | 👻 Ghost Typer: [Type text to inject...           ] |   |  <- On-Demand Ghost Input (toggled via bottom nav)
   |   +-----------------------------------------------------+   |
   |-------------------------------------------------------------|
   |      🏠 Home       🌐 Chrome       🤖 ChatGPT       👻 Ghost       🔄 Reset          [⤡] |  <- Bottom Navigation & Resize Grip
   +-------------------------------------------------------------+
```

---

## ⚡ Quick Install

### 🐧 Linux (X11) — one command
```bash
git clone https://github.com/savenXD/Linux-Handy-overlay.git cronq && cd cronq && ./install.sh
```
Then run `cronq` (or find **Cronq** in your app menu). Remove it later with `./uninstall.sh`.

### 🪟 Windows 10/11 (64-bit) — PowerShell
> ⚠️ The Windows port is **beta**: it passes cross-build/Wine tests but hasn't been fully verified on real Windows yet. See [windows/PARITY.md](windows/PARITY.md).

**One command (no CMake, no Visual Studio, no admin rights):**
```powershell
irm https://github.com/savenXD/Linux-Handy-overlay/releases/latest/download/cronq-setup.exe -OutFile $env:TEMP\cronq-setup.exe; & $env:TEMP\cronq-setup.exe
```
Or download `cronq-setup.exe` from the [Releases page](https://github.com/savenXD/Linux-Handy-overlay/releases/latest) and double-click it. The build is unsigned, so if SmartScreen warns, click **More info → Run anyway**. The installer fetches the WebView2 runtime if missing.

<details><summary>Build from source instead</summary>

(needs Git, CMake and Visual Studio Build Tools with the C++ workload)
```powershell
git clone https://github.com/savenXD/Linux-Handy-overlay.git cronq; cd cronq
cmake -S windows -B windows/build -A x64
cmake --build windows/build --config Release
powershell -NoProfile -ExecutionPolicy Bypass -File windows/installer/make-dist.ps1
powershell -NoProfile -ExecutionPolicy Bypass -File windows/installer/install.ps1
```
</details>

Then open a new terminal and run `cronq`. Uninstall with `windows/installer/uninstall.ps1` (add `-Purge` to also delete settings). Full details: [windows/README.md](windows/README.md).


**What the installer does automatically:**
- 🔍 Detects your distribution (`apt` for Ubuntu/Mint/Debian, `dnf` for Fedora/RHEL, `pacman` for Arch/Manjaro, `zypper` for openSUSE).
- 📦 Installs any missing build tools and GTK3/WebKit libraries with your permission.
- ⚙️ Compiles Cronq with maximum optimizations.
- 🚀 Integrates into your application menu with a custom SVG icon and desktop autostart.
- 🔗 Ensures `~/.local/bin` is in your shell `$PATH`.
- 📱 Launches the `cronq-supervisor` daemon in the background immediately!

---

## 📑 Table of Contents
1. [What It Does (In Simple Words)](#-what-it-does-in-simple-words)
2. [Who Can Run It?](#-who-can-run-it)
3. [How to Control It](#-how-to-control-it)
4. [Keyboard Shortcuts](#-keyboard-shortcuts)
5. [What It Can Bypass vs What It Cannot](#-what-it-can-bypass-vs-what-it-cannot)
6. [Tech Stack Used](#-tech-stack-used)
7. [Installation & Setup](#-installation--setup)
8. [Uninstallation](#-uninstallation)
9. [Configuration & Customization](#-configuration--customization)

---

## 💡 What It Does (In Simple Words)

**Cronq** puts a mini companion window right on your desktop screen. It acts as a permanent, floating helper while you code, game, study, or work.

Here is what you can do with it:
- **Never Loses Focus**: You can click buttons on the overlay, browse mobile pages, or search without your main window (VS Code, terminal, browser) losing typing focus.
- **Center Drag-to-Move**: Click and hold the center pill in the top header `[ ≡ ]` to smoothly move the window anywhere on your screen.
- **Resize Freely**: Drag the bottom-right handle `[⤡]` to resize it to any width and height, or press `Alt + F` to go full screen.
- **Adjust Opacity**: Make it semi-transparent so you can see your code or work through it (`Alt + A` to decrease, `Alt + S` to increase).
- **Ghost Mode (Click-Through)**: Turn on Ghost mode (`Alt + G`) to make the overlay completely click-through. Mouse clicks pass straight through to whatever window is behind it.
- **Ghost Typer**: The Ghost footer button opens a page with a boxed editor that grows with your text (up to a cap). Type or paste text or code, pick **Slow**, **Moderate** or **Fast**, then click **Start**. A three-second countdown gives you time to click the destination field. **Stop** cancels immediately. Settings scroll above the pinned Start button. Footer clicks and Enter in the editor never start typing.
- **Human-like**: On by default, Slow (~40 WPM). Moderate is ~105 WPM (a good typist) and Fast ~140 WPM (a fast typist, with occasional quicker bursts). Pauses, hesitations and thinking delays stay the same length in every mode. Human mode uses log-normal gaps between keys, slower pauses after spaces, punctuation and newlines, slow speed drift, rare hesitations, a 40–170 ms key hold, a modifier lead-in for capitals and symbols, and a short pause every 3–6 lines. Turning Human-like off hides the speed buttons and types instantly in small batches (thousands of characters per second without freezing the window); that pattern is easy to detect. Settings save when changed.
- **Extra safe**: A switch under Human-like (off by default; hidden when Human-like is off). Works with Slow, Moderate and Fast: about 10% slower, occasional adjacent-key typos on lowercase letters that are noticed after 0–2 more letters, backspaced and retyped (about 3% of letters, never twice within 6 letters), and random thinking pauses after some words, sentences, commas and line breaks. The final text is always exactly what you entered.
- **Code and formatting**: Line breaks, blank lines, tabs, spaces, and Unicode text remain in the editor. Trailing line breaks are omitted from the typing output to prevent an accidental final Enter; internal line breaks are preserved. **Keep line breaks & tabs** is enabled by default; disable the destination editor’s automatic indentation when typing code to avoid extra indentation. Destination applications still interpret Return and Tab according to their own behavior. Typing stops if the destination loses focus. Timing variation does not guarantee that an application will treat the input as manual typing.
- **Universal Reset / Refresh**: Tap the reload button `🔄` in the bottom bar to instantly return the overlay to its pristine home state without logging out your accounts or clearing saved cookies.
- **Built-in ChatGPT & Mobile Web**: Switch instantly between your custom home screen, clean Chrome mobile web, and ChatGPT. All cookies, logins, and sessions are safely saved.
- **Instant Clean Screenshots**: Press `Alt + P` to take a screenshot. The overlay hides for a millisecond, captures the clean screen underneath, saves it to `~/Pictures/cronq/`, and copies it to your clipboard.

---

## 🖥️ Who Can Run It?

| Requirement | Supported | Notes |
| :--- | :--- | :--- |
| **Operating System** | **Linux** | Linux Mint (Cinnamon / XFCE / MATE), Ubuntu, Debian, Arch Linux, Fedora, Manjaro, openSUSE, etc. |
| **Display Server** | **X11 / Xorg** | **Required**. (Default on Linux Mint. On Ubuntu/Fedora, choose "Ubuntu on Xorg" at the login gear icon). |
| **Compositor** | **Yes** | Any compositor (Muffin, Mutter, Picom, Compiz, KWin) for smooth rounded corners and transparency. |
| **RAM / CPU** | **Very Low** | Written in lightweight C (<60 MB RAM). Runs smoothly even on older laptops. |

> [!NOTE]
> **Wayland users**: Cronq requires an X11 / XWayland session. Pure Wayland compositors isolate every window into a sandbox, blocking global click-through shaping and root keyboard monitoring.

---

## 🚀 How to Control It

Once installed, control everything with simple terminal commands or keyboard shortcuts:

### Bring It Up
To open the overlay (or launch it in the background if it is not running):
```bash
cronq
```

### Kill / Close It
To cleanly stop and close the overlay and its supervisor daemon:
```bash
cronq kill
```

### Other Helpful Commands
```bash
cronq toggle    # Show or hide the window
cronq status    # Check if it is running, opacity, and active mode
cronq snap      # Take a screenshot
cronq --help    # View all available CLI options
```

---

## ⌨️ Keyboard Shortcuts

All shortcuts use the **<kbd>Alt</kbd>** key globally from anywhere on your system:

| Shortcut | What It Does |
| :--- | :--- |
| **`Alt` + `X`** | **Toggle Overlay**: Instantly show or hide the companion window. |
| **`Alt` + `G`** | **Ghost Mode**: Toggle click-through mode on or off. When ON, clicks pass through to windows beneath. |
| **`Alt` + `A`** | **Decrease Opacity**: Make the window more transparent (-10% step). |
| **`Alt` + `S`** | **Increase Opacity**: Make the window more solid (+10% step). |
| **`Alt` + `F`** | **Fullscreen**: Toggle between full screen and your custom window size. |
| **`Alt` + `D`** | **Default Size**: Reset window back to default phone dimensions (360x640). |
| **`Alt` + `P`** | **Screenshot**: Capture the screen cleanly (excluding the overlay), copy to clipboard, and save to disk. |
| **`Alt` + `M`** | **Input Mode**: Switch between `MIRROR` (type to both overlay & background) and `EXCLUSIVE`. |
| **`Alt` + `W`** | **Ghost Writer**: Synthesize and type text into the active background application. |

### Mouse Controls Inside the Window
- **Move Window**: Click and drag the **center handle capsule `[ ≡ ]`** in the top navigation bar.
- **Resize Window**: Click and drag the **bottom-right grip `[⤡]`** to change width and height smoothly.
- **Window Controls (Top-Right)**:
  - `—` Hide / Minimize overlay
  - `▢` Toggle Fullscreen
  - `✕` Close / Hide
- **Bottom Navigation**:
  - `🏠` Home Page
  - `🌐` Chrome Mobile Browser
  - `🤖` ChatGPT Web App
  - `👻` Open Ghost Typer
  - `🔄` Universal Refresh / Reset to clean state

---

## 🛡️ What It Can Bypass vs What It Cannot

### ✅ What It CAN Bypass
1. **Window Stacking Rules**:
   - Uses an X11 **override-redirect** window combined with a proactive **restack watcher**.
   - Bypasses standard window manager layering. It stays above fullscreen games, fullscreen YouTube / VLC videos, and `_NET_WM_STATE_ABOVE` utility windows without getting buried.
2. **Focus Stealing**:
   - Standard windows steal keyboard and window focus when clicked.
   - Cronq **never steals `_NET_ACTIVE_WINDOW`**. You can click, scroll, and interact with the overlay while your code editor or terminal stays active and focused.
3. **Mouse Blocking (Ghost Mode)**:
   - Uses the **X11 Shape Extension** (`ShapeInput`) to puncture an empty input mask.
   - Allows your mouse clicks, drags, and scrolling to pass straight through the overlay to underlying software as if the overlay weren't there.
4. **Screenshot Occlusion**:
   - When taking a snapshot (`Alt + P`), it momentarily unmaps itself from the X server, takes the clean capture of the desktop underneath, and remaps instantly with a flash toast.
5. **Crashes & Disruptions**:
   - Includes a background watchdog supervisor (`cronq-supervisor`) that automatically restores the process if terminated abnormally.

---

### ❌ What It CANNOT Bypass
1. **Wayland Display Server Restrictions**:
   - Pure Wayland compositors isolate every window into a sandbox. Wayland will not allow global click-through shaping, raw global keyboard event snooping, or arbitrary top-level restacking. You must run an **X11 / Xorg** session.
2. **Root & Kernel-Level Protections**:
   - Cannot bypass kernel-level security tools, hardware-exclusive direct-rendering (DRM/KMS VT consoles outside X11), or games running with kernel anti-cheat drivers.
3. **Complex Asian Multi-Stage IMEs**:
   - Input Method Editors (like Japanese Mozc or Chinese Pinyin) that rely on external candidate popup windows require active window focus. In Mirror mode, standard keys are mirrored, but IME composition candidate menus may require switching to Exclusive mode (`Alt + M`).
4. **Hardware Anti-Bot Sensor Tests**:
   - The browser uses a modern Android/Chrome mobile user-agent with standard WebKit. While it logs into websites like Google and ChatGPT cleanly, sites with specialized native app bot-checkers checking for physical phone hardware (e.g. mobile accelerometer gyro chips) can tell it is running in an embedded browser environment.

---

## 🛠️ Tech Stack Used

| Component | Technology | Rationale |
| :--- | :--- | :--- |
| **Language** | **C (GNU11)** | Ultra-fast execution, zero runtime bloat, minimal memory footprint (<60 MB RAM). |
| **GUI Framework** | **GTK+ 3.0 & Cairo** | Native Linux GUI with RGBA visual support for transparent, anti-aliased phone borders. |
| **Web Engine** | **WebKitGTK 4.1 (libsoup-3.0)** | Modern web standards, fast hardware-accelerated rendering, persistent cookies/local storage. |
| **Window System** | **X11 / Xlib** | Direct interaction with X server for override-redirect windows and restacking dominance. |
| **Input Sniffing** | **XInput 2.2 (`libXi`)** | Raw root window keyboard listening without stealing focus or intercepting apps. |
| **Click-Through** | **XShape (`libXext`)** | Real-time shape input masking for 100% mouse pass-through in Ghost mode. |
| **Keystroke Injection** | **XTest 2.2 (`libXtst`)** | Ghost Writer simulated typing into active target windows. |
| **IPC Control** | **Unix Domain Sockets** | Instantaneous, single-instance communication between CLI terminal and background daemon. |

---

## 📦 Installation & Setup

### Method 1: Automated 1-Step Installer (Recommended)
```bash
git clone https://github.com/savenXD/Linux-Handy-overlay.git cronq
cd cronq
./install.sh
```

### Method 2: Manual Installation

**1. Install Dependencies**
- **Ubuntu / Debian / Linux Mint**:
  ```bash
  sudo apt update
  sudo apt install -y build-essential pkg-config libgtk-3-dev libwebkit2gtk-4.1-dev \
                      libx11-dev libxi-dev libxext-dev libxrandr-dev libxtst-dev xdotool xclip maim
  ```
- **Fedora / RHEL**:
  ```bash
  sudo dnf install -y gcc gcc-c++ make pkgconf-pkg-config gtk3-devel webkit2gtk4.1-devel \
                      libX11-devel libXi-devel libXtst-devel libXrandr-devel libXext-devel xdotool xclip maim
  ```
- **Arch Linux / Manjaro**:
  ```bash
  sudo pacman -S --needed base-devel gtk3 webkit2gtk-4.1 libx11 libxi libxtst libxrandr libxext xdotool xclip maim
  ```

**2. Build & Install**
```bash
make
make install
```

---

## 🗑️ Uninstallation

To completely remove Cronq from your system:

```bash
./uninstall.sh
```
Or with manual make:
```bash
make uninstall
```
To purge saved browser cookies, sessions, and cache as well, pass `--purge`:
```bash
./uninstall.sh --purge
```

---

## ⚙️ Configuration & Customization

The settings file is located at `~/.config/cronq/config.ini`:

```ini
[General]
width = 360
height = 640
margin = 12
# Options: bottom-left, bottom-right, top-left, top-right
corner = bottom-left
# Options: primary or monitor index (0, 1, ...)
monitor = primary
opacity = 0.98
start_visible = false
# Options: mirror, exclusive
default_key_mode = mirror

[Browser]
home_url = https://html5test.com
chatgpt_url = https://chatgpt.com
search_url_template = https://duckduckgo.com/?q=%s
user_agent = Mozilla/5.0 (Linux; Android 14; Pixel 8) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/124.0.0.0 Mobile Safari/537.36

[Hotkeys]
# Global hotkey letter bindings (combined with Alt)
toggle_key = x
ghost_key = g
opacity_dec_key = a
opacity_inc_key = s
fullscreen_key = f
default_size_key = d
snap_key = p
mode_key = m
ghost_writer_key = w
```

You can reload configuration changes on the fly without restarting:
```bash
cronq --reload-config
```


### Ghost Type page regression test

Run `bash tests/test_ghost_page.sh` in a dedicated X11 test display (for example, `DISPLAY=:91 bash tests/test_ghost_page.sh` inside Xephyr). It uses a temporary configuration and an isolated target window to check navigation, multiline text, slider/switch behavior, key hold and gap timing, phone layout, countdown, exact keystrokes, chunk pauses, cancellation, and focus changes. It does not start the normal app daemon.
