# What we are building

## The product: Cronq

Cronq is a floating companion window shaped like a phone (default 360x640 pixels, rounded corners, dark theme).
It is meant for people who need a browser / ChatGPT / typing helper visible **while using other software**,
including software that normally forbids overlays.

The owner built it for Linux (X11) in C with GTK3 + WebKitGTK. It works completely. The owner now wants **the same
product on Windows**: same features, same look, same keyboard shortcuts, same CLI commands, same config file keys.

## The five core features (the owner's own list — these are what "done" means)

1. **Overlay above everything.** The window stays visible on top of other software, including fullscreen apps and
   software that does not allow overlays. No matter what, the overlay is on top.
2. **ChatGPT inside the overlay.** You can log in (including Google/Microsoft/Apple sign-in popups) and the login
   survives restarts.
3. **Ghost Typer.** You paste text into the overlay's editor; it types that text into whatever other app is focused,
   with human-like timing (slow / moderate / fast, "Extra safe" mode with typos and pauses). All features, same as Linux.
4. **Snap.** One key / button takes a screenshot of the screen **without the overlay in it** and without changing
   which window has focus. It copies it to the clipboard and saves a PNG.
5. **Typing without losing focus.** You can click inside the overlay and type into it, and the app underneath keeps
   its focus (Mirror mode = keys go to both; Exclusive mode = keys go only to the overlay).

Everything else (opacity, fullscreen, click-through "Ghost mode", reset, CLI, supervisor) supports these five.

## Keyboard shortcuts (identical on both platforms)

All are **Alt + key**, global (work from any app). Ctrl and Shift must NOT be held.

| Keys | Action |
| --- | --- |
| Alt+X | Show / hide overlay |
| Alt+G | Ghost mode (click-through) on/off |
| Alt+A / Alt+S | Opacity -10% / +10% |
| Alt+F | Fullscreen toggle |
| Alt+D | Default size |
| Alt+P | Snap (screenshot) |
| Alt+M | Mirror / Exclusive typing mode |
| Alt+W | Ghost Writer start / cancel |

## Who the owner is and how they work

- They are the product owner, not necessarily a Windows developer. They run Linux day to day and will boot Windows
  to test your work.
- They want a result that "works first time" on Windows. They are paying for correctness, not speed.
- They asked for the Linux code to stay completely isolated so it can never break.
- Messages from them may contain typos. Interpret the intent.

## What "done" looks like for the whole project

- `cronq.exe` / `cronqw.exe` + installer work on a clean Windows 10 (22H2) and Windows 11 machine.
- Every row of `shared/spec/behavior.md` and every feature in `02_LINUX_FEATURES_AND_WINDOWS_EQUIVALENTS.md`
  is implemented and either verified or listed with an exact manual test step for the owner.
- Same hotkeys, same CLI commands, same config keys, same toast texts as Linux.
- Zero compiler warnings, all automated checks green, no known bugs.
- Linux files unchanged (`git diff -- src Makefile scripts install.sh uninstall.sh tests` is empty).

## Honest limits (these are expected, not bugs — document them, do not try to "fix" them)

- True exclusive-fullscreen DirectX games cannot be drawn over by any normal window.
- Apps running as Administrator cannot be typed into or reliably overlaid by a normal-privilege app.
- The UAC prompt, the Ctrl+Alt+Del screen and kernel-level anti-cheat are untouchable.
- Keys sent by Ghost Typer are marked "injected" by Windows; other software can detect that.
