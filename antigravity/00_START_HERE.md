# START HERE

You are taking over a half-finished project: **a Windows version of an app called Cronq**.
This folder is your instruction manual. Read the files **in the order below**, completely, before writing any code.
Do not skim. Every file exists because a mistake is easy to make in that area.

## The one-paragraph summary

Cronq is a small floating "phone-shaped" window that stays on top of everything (even fullscreen apps), without
ever stealing keyboard focus. Inside it: a mobile browser, ChatGPT (logins saved), a "Ghost Typer" that types text
into any other app like a human, a screenshot button that does not disturb focus, and the ability to type into the
overlay without losing focus on the app underneath. **It is finished and working on Linux.** We are building the
same thing for Windows, with identical features and identical keyboard shortcuts. The Linux code must never be touched.

## What is done and what is yours

| Phase | Scope | Status | Who |
| --- | --- | --- | --- |
| 1 | Overlay window, hotkeys, stay-on-top, IPC + CLI, supervisor, config | **DONE and tested** (22/22 automated checks pass under Wine) | already done |
| 2 | Browser + ChatGPT (WebView2), UI built in HTML | **COMPILED; Wine checks passed; native WebView2 UNVERIFIED (needs Windows)** — see `REPORT_PHASE_2.md` | implemented |
| 3 | Typing into the overlay without focus, Snap, Ghost Writer | **IMPLEMENTED; integration fixes and Wine checks passed; native WebView2 acceptance pending** — see `REPORT_INTEGRATION.md` | implemented |
| 4 | Installer, autostart, polish, final parity check | **IMPLEMENTED; installer compiled and exercised in Wine; native Windows acceptance pending** — see `REPORT_INTEGRATION.md` | you (Phase 4) |

The owner has now authorized a combined integration review after all agents finished. `REPORT_INTEGRATION.md` supersedes older verification claims.

For future phase work, do the phases **in order, one at a time**. When a phase is finished, **stop and report** (see `11_HOW_TO_REPORT_BACK.md`).
Never start the next phase on your own.

## Reading order

| # | File | Read it to learn |
| --- | --- | --- |
| 00 | `00_START_HERE.md` | this page |
| 01 | `01_WHAT_WE_ARE_BUILDING.md` | what the product is, who the owner is, what "done" means |
| 02 | `02_LINUX_FEATURES_AND_WINDOWS_EQUIVALENTS.md` | every Linux feature and the exact Windows replacement |
| 03 | `03_NON_NEGOTIABLE_RULES.md` | the rules. **Breaking these ruins the project.** |
| 04 | `04_WHAT_IS_ALREADY_DONE_PHASE1.md` | exactly what exists in `windows/` and how it behaves |
| 05 | `05_BUILD_AND_TEST_GUIDE.md` | how to compile and test here on Linux (this works!) and what cannot be tested |
| 06 | `06_PHASE2_BROWSER_AND_UI.md` | your Phase 2 task list |
| 07 | `07_PHASE3_TYPING_SNAP_GHOST_WRITER.md` | your Phase 3 task list |
| 08 | `08_PHASE4_INSTALLER_AND_PARITY.md` | your Phase 4 task list |
| 09 | `09_WIN32_AND_COM_PITFALLS.md` | checklist of classic Windows mistakes. Re-read before each phase |
| 10 | `10_LINUX_SOURCE_READING_MAP.md` | which Linux function to read for each feature you port |
| 11 | `11_HOW_TO_REPORT_BACK.md` | the report you must write after each phase |
| 12 | `12_DECISIONS_ALREADY_MADE.md` | design decisions that are settled. Do not re-decide them |

## The most important facts (memorise these)

1. **Nobody can run this on real Windows right now.** The owner is on Linux and will test on Windows later.
   Your code must work **the first time they run it**. That means: write slowly, re-read your code, keep functions
   small, and follow `09_WIN32_AND_COM_PITFALLS.md`. See `03_NON_NEGOTIABLE_RULES.md` §2.
2. **You can still compile and partly test on this Linux machine.** A Windows cross-compiler and Wine are installed.
   Phase 1 was verified this way. Use it for everything it can check (`05_BUILD_AND_TEST_GUIDE.md`).
3. **Never edit Linux files.** Allowed locations: `windows/`, `shared/`, `.github/workflows/windows.yml`, and this
   `antigravity/` folder. Everything else (`src/`, `Makefile`, `scripts/`, `install.sh`, `uninstall.sh`, `tests/`,
   `cronq.desktop`, `README.md`, `assets/`) is Linux and is off limits.
4. **Linux behaviour is the specification.** When in doubt, read the Linux C code (`10_LINUX_SOURCE_READING_MAP.md`)
   and copy its behaviour, numbers and texts exactly.
5. **Do not claim something works unless you ran it.** Say "compiled", "passed under Wine" or "UNVERIFIED (needs
   Windows)". Honest reports matter more than impressive ones.

## Where things live

```
god-window/
  src/ Makefile scripts/ install.sh ...   LINUX. Do not touch.
  windows/                                 THE WINDOWS APP. Your workspace.
    src/            C++17 source (Phase 1 already here)
    tests/          unit tests + dev tools
    scripts/        build-mingw.sh, wine-smoke.sh
    cmake/ compat/ tools/                  build helpers
    README.md                              user-facing Windows readme + manual test checklist
  shared/
    spec/behavior.md                       rules both platforms must obey (hotkeys, sizes, IPC)
    fixtures/config_defaults.ini           sample config both platforms parse
  .github/workflows/windows.yml            CI (Windows build + Linux-untouched guard)
  antigravity/                             this manual
```

If anything in these files contradicts what you see in the code, **stop and report the contradiction** instead of guessing.
