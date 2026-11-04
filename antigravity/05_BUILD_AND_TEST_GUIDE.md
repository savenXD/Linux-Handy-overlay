# Build and test guide (Linux host, Windows target)

## The situation

- The real target is Windows 10/11 x64. Nobody can run on real Windows during your work.
- But this Linux machine has **llvm-mingw** (Windows cross-compiler) and **Wine**, so you can compile for Windows and run
  non-WebView2 parts of the program. This catches typos, type errors, link errors, logic errors in pure code, and
  regressions in the window / IPC / hotkey / supervisor layer.

## Tools already installed (verify with `ls`)

| Tool | Location |
| --- | --- |
| llvm-mingw cross compiler | `~/.local/llvm-mingw/bin/x86_64-w64-mingw32-clang++` (override with `LLVM_MINGW=/path`) |
| CMake | `~/.local/bin/cmake` |
| Wine | `/usr/bin/wine`, `wineserver` |
| Xephyr (nested X display for Wine) | `/usr/bin/Xephyr` |
| xdpyinfo, ImageMagick `import`, xwd | for screenshots of the test display |

## Compile

```bash
windows/scripts/build-mingw.sh                 # Phase 1 style build, output in windows/build-mingw/
```

With WebView2 (needed from Phase 2; downloads a ~9 MB SDK once, needs internet):

```bash
CRONQ_WEBVIEW2=ON windows/scripts/build-mingw.sh
```

That also builds `webview2_compile_check.exe` and copies `WebView2Loader.dll` next to the executables.
Switch back to a plain build with `CRONQ_WEBVIEW2=OFF windows/scripts/build-mingw.sh`.
**Rule: zero warnings.** If the build prints any `warning:`, fix it.

## Automated runtime checks under Wine

```bash
windows/scripts/wine-smoke.sh
```

It starts a nested X display (a Xephyr window appears briefly), creates a private Wine prefix in
`~/.cache/cronq-wine-test`, runs the unit test and then the daemon, and prints `PASS`/`FAIL` lines plus
`Result: N passed, M failed`. **Currently 22 passed, 0 failed. It must stay that way.**
The first run takes a minute (Wine prefix creation).

After testing, free disk space: `rm -rf ~/.cache/cronq-wine-test` (a prefix is a few hundred MB).
**The disk on this machine is ~96% full (about 8 GB free). Do not leave big files around. Never create files in `/tmp`
outside the scratchpad directory you were given; use `~/.cache/...` or the scratchpad.**

### What Wine CAN verify

Window creation and styles, geometry maths, opacity/size commands, IPC protocol and CLI output, the keyboard hook and
hotkeys (`sendkeys.exe` injects keys that the hook sees), supervisor restart, config parsing, any pure logic you unit test,
file output (snap PNG files, config writes), `SendInput` plumbing (partially).

### What Wine CANNOT verify (needs the owner on real Windows) → mark UNVERIFIED

- **WebView2 / ChatGPT / login / popups / mobile emulation** (the Edge runtime does not exist under Wine).
- Real focus behaviour of other applications (does Notepad keep its caret when we click?).
- DPI scaling on real displays, multi-monitor, taskbar behaviour, Windows 11 specifics.
- Display affinity capture exclusion on real DWM (`WDA_EXCLUDEFROMCAPTURE`).
- Elevated (administrator) windows, UAC, games, borderless-fullscreen video.
- Anything involving the real clipboard from other apps.

## How to add a runtime check to the smoke script

Open `windows/scripts/wine-smoke.sh`. Use the existing helpers:

```bash
cli --some-command            # runs cronq.exe <args>, returns its stdout
keys alt+p                    # injects a hotkey chord
expect "name" "substring" cli status       # polls ~6 s until the output contains the substring
check  "name" "actual" "substring"         # one-shot check
```

Rules for scripts: quote every path (the project path contains a space: `Codding Projects`), never use `pkill -f`,
always `timeout` every `wine` call, and always poll (`expect`) instead of sleeping a fixed time.

## Adding a unit test

1. Put the logic in `windows/src/<thing>.h/.cpp` with no Win32 calls (or only trivial ones).
2. Add `windows/tests/<thing>_test.cpp` following `config_test.cpp` (a `CHECK` macro, returns non-zero on failure).
3. Register it in `windows/CMakeLists.txt` next to `config_test` (`add_executable`, `target_link_libraries(... cronq_core)`,
   `add_test`, and add it to the static-link loop at the bottom).
4. Add a section to `wine-smoke.sh` that runs it (copy the `config_parser unit test` block).

## Screenshots of the overlay (to look at your UI)

Inside a smoke-style session you can capture the nested display:

```bash
DISPLAY=:97 import -window root /home/saven/.cache/cronq-wine-test/shot.png
```

then open the PNG with the Read tool. (Under Wine the web content will be blank because WebView2 is unavailable.)

## CI

`.github/workflows/windows.yml` builds with MSVC on a Windows runner and runs `ctest`. The owner may run it; you
cannot. Keep the CMake files valid for **both** MSVC and llvm-mingw: no GCC-only flags without `if(NOT MSVC)`.

## Windows manual checklists

For every feature that Wine cannot verify, add numbered steps to `windows/README.md` under a heading
`## Phase N manual checklist`. Each step: precondition, exact action, expected result. The owner follows them literally.
Phase 1's checklist is already there; copy its style.

## Troubleshooting you will probably hit

| Symptom | Cause / fix |
| --- | --- |
| `wine: failed to open "/home/saven/Codding"` | an unquoted path with a space |
| `rc=53` / `status c0000135` | an exe is missing a DLL → it is not statically linked; fix CMake |
| `no driver could be loaded` / CreateWindow fails | Xephyr is not running or `DISPLAY` is wrong; Xephyr itself needs the real display (`DISPLAY=:0 Xephyr :97 ...`) |
| `No space left on device` | delete Wine prefixes and build dirs; check `df -h /` |
| everything times out on first run | Wine prefix still initialising; run `wine wineboot --init` first |
| hotkey tests all "pass" but nothing changes | your check string already equals the initial state; make the check require a *change* |
