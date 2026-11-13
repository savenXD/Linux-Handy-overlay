# God-copy: Windows selection reader and retained paste

Status: experimental implementation, Windows 11 target. Native CodeTantra acceptance is **not established**. This feature does not satisfy the requested universal guarantee of copying and pasting in every application. Do not describe it as a verified CodeTantra bypass.

## Using it

1. Select text in another application and press **Ctrl+Left Alt+C**. Release the keys. Right Alt is excluded to preserve AltGr typing.
2. A successful read replaces the overlay's memory buffer and attempts to publish `CF_UNICODETEXT` to the Windows clipboard. The toast distinguishes a successful clipboard publication from a busy clipboard.
3. Focus an editable destination and press **Ctrl+Left Alt+V**. Release the modifiers within two seconds. This sends the retained text as Unicode keyboard input without reading the clipboard or sending Ctrl+V/WM_PASTE.
4. Escape cancels an active paste. Changing the foreground window or focused native control also stops it. **Alt+K**, universal reset, and daemon shutdown cancel work and erase the retained text.

Both shortcuts work while the overlay is hidden. They are disabled while locked. A new copy/paste request is rejected while another god-copy operation or Ghost Writer operation is active. Failed copies preserve the prior buffer and explicitly report that fact; do not mistake earlier retained text for a newly copied selection. The buffer does not survive process exit, a crash, or a client terminating the overlay. No copied text is intentionally saved to files or logged. Text published to the system clipboard is subject to Windows clipboard history, synchronization, and other applications; clearing the overlay buffer does not erase that history.

## How copying works

`src/god_copy.cpp` launches the same executable with the internal `--god-copy-worker` entry point. A single explicitly inherited, unnamed, pagefile-backed mapping transfers the request and result. There is no text file, network transfer, injected code, target process patch, or clipboard-based selection acquisition.

The helper is created suspended, assigned to a job with kill-on-close, then resumed. If the overlay dies, the job prevents a helper from remaining indefinitely. The UI thread polls completion instead of calling accessibility providers itself; a four-second deadline terminates an unresponsive helper. The helper owns its COM MTA and interfaces. No COM objects cross back into the UI thread.

Selection readers, in order:

- Standard Win32 `EDIT`: bounded `EM_GETSEL`, `WM_GETTEXTLENGTH`, and `WM_GETTEXT`, followed by selection/focus revalidation. These read the selected substring independently of the control's copy command. Password style is excluded. Document allocation is bounded to 262,144 UTF-16 units; larger documents fall through to accessibility.
- UI Automation TextPattern: read `GetSelection()` ranges and their `GetText()` content. The focused accessibility element must be inside the target window's accessibility tree. Ancestors are searched only up to that window, with a bounded traversal. Renderer process IDs may differ from the native top-level window's process; ancestry, rather than PID equality, establishes containment. Focus is rechecked afterward. Password elements and unknown password status are excluded. An empty range is a caret, not permission to copy the whole document.

Noncontiguous selected ranges are joined with LF separators. The overall retained selection is limited to 262,144 UTF-16 units. Oversized, truncated, embedded-NUL, and malformed surrogate results are rejected. There is no document-range or entire-window text dump fallback when no selection is exposed.

The previous memory buffer is replaced only after successful process completion, target checks, and protocol validation. Clipboard publication retries for one second without blocking the UI. A successful `SetClipboardData` means Windows accepted the text at that instant; it does not prove another application cannot immediately clear it.

## How retained paste works

Paste waits for modifiers to be released, checks target focus on each timer tick, and submits one Unicode scalar's down/up events together. Surrogate pairs stay together. CRLF is normalized to one CR input; LF is sent as CR. Tabs and other characters use Unicode text packets rather than virtual Tab/Backspace navigation commands. A partial `SendInput` result stops the operation without retrying already submitted text. Any incomplete injected key press gets a best-effort release.

The success toast deliberately says **input sent; verify the text in the destination**. Windows accepting injected input does not prove the target inserted it. Controls can filter Unicode packets, interpret line breaks, impose length limits, format/indent code, or reject synthetic input. Switching logical editors inside one native HWND may not be detectable by the native focus check. Focus checks and injection are separate OS operations: an unavoidable race remains. This is not a secure transactional paste mechanism. Long selections are sent conservatively at approximately one scalar per 15 ms timer tick, and can take substantial time.

## Why the universal requirement remains open

The user's observed failure is: text can be selected, but normal Ctrl+C never produces usable clipboard text. The mechanism in CodeTantra has not been measured. Cancellation of copy events, keyboard interception, and immediate clipboard clearing remain distinguishable possibilities.

Accessibility can avoid an application's normal copy event **when that application exposes its selection**. A custom editor, canvas, inaccessible renderer, protected desktop, higher integrity target, disabled provider, or app that terminates the overlay may prevent this method. It does not recover text from black capture output or promise to bypass intentional content protection. Screenshot APIs and Windows clipboard APIs do not supply a universal fallback for text the application does not expose.

Microsoft documents [TextPattern selection behavior](https://learn.microsoft.com/en-us/windows/win32/api/uiautomationclient/nf-uiautomationclient-iuiautomationtextpattern-getselection), [UI Automation threading requirements](https://learn.microsoft.com/en-us/windows/win32/winauto/uiauto-threading), and [SendInput integrity restrictions and modifier behavior](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-sendinput). These constraints cannot be removed by calling the feature “god-copy.” No driver, privileged input interception, renderer patch, or hardware capture path has been implemented or validated.

## Validation and Windows continuation

Build with the ordinary Windows CMake workflow. `god_copy_protocol_test` is part of CTest. `god_copy_runtime_test.exe` requires an unlocked interactive desktop and a running daemon; it creates only its own disposable text controls. On Linux, `bash windows/scripts/god-copy-wine-test.sh` runs both in a fresh Wine prefix and nested Xephyr desktop. Wine is not evidence about CodeTantra, Chromium UIA exposure, elevated targets, or native Windows 11 accessibility behavior.

Validation on Linux/Wine (2026-11-14): cross-build succeeded without new compiler warnings, all **22 CTest cases** passed, and all **16 interactive fixture checks** passed. The fixture independently blocks ordinary Ctrl+C/WM_COPY, verifies exact selected Unicode/CRLF text, retained paste after clipboard clearing, no-selection/password failure retention, clipboard contention, focus cancellation, modifier/AltGr exclusion, and lock erasure. Successful selection reads in this fixture used **native EDIT**. UI Automation selection, the four-second hung-provider watchdog, CodeTantra behavior, and native MSVC/runtime behavior still require their own validation; the green fixture does not establish them.

On native Windows 11:

1. Build in a separate directory, run CTest, start the new daemon, and run `god_copy_runtime_test.exe`. Do not test against a real document.
2. Use an ordinary Electron/browser selection to check the UI Automation path. Logs identify the reader and UTF-16 length, without the text. A standard EDIT fixture only proves the native EDIT path.
3. With the user's authorized practice session, log in manually, select a harmless known phrase in CodeTantra, press Ctrl+Left Alt+C, and record the exact toast and `GODCOPY` log entries. Do not give credentials to the agent.
4. Exit the protected session, paste into a disposable editor with Ctrl+Left Alt+V, and compare exact text. Repeat after deliberately clearing the clipboard. Separately record whether the overlay/helper was terminated.
5. If accessibility says unsupported, collect evidence of the focused control/provider and stop describing this route as working. Do not silently substitute older retained text or normal clipboard content.

Read [WINDOWS_AGENT_HANDOFF.md](../WINDOWS_AGENT_HANDOFF.md) and the [CodeTantra evidence index](docs/codetantra-sea/README.md) for the prior screenshot investigation and native testing constraints. This addition does not fix the unresolved black screenshot issue. Packaged for v1.4.1; consult the GitHub release and its Actions run for publication/build status. CodeTantra acceptance remains unverified.
