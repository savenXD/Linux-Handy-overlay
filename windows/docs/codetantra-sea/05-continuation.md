# Continuation guidance and unresolved work

## Established starting point

Do not redo a strings-only investigation and call it the final result. We already recovered the actual capture-policy branch and obtained successful runtime reads of `0x11` during the user's protected coding session. The remaining practical objective is a reliable screenshot method for this particular build.

No tested solution currently exists in this investigation. Do not claim that an external PowerShell call can simply clear another process's affinity. The setter requires process ownership, while reading it cross-process succeeded here. Do not claim an archive edit, debugger flag, change of capture shortcut, administrator rights, or switching capture API will necessarily work.

## Questions that remain open

1. Which exact policy message/configuration is active for ADS, and which component sends it? No page/network/IPC trace was collected.
2. Did the described restart happen outside the observed interval, in another attempt, or under different persisted configuration? The current log does not show PID replacement.
3. What changed relative to the last release? Only version 5.2.0 was analyzed. An older installer or matched prior evidence is needed for comparison.
4. How does `sea_capture_integrity` interact with enforcement, and is it actually executed in this session? Static existence is insufficient.
5. Can the protection be disabled or another capture route work reliably on the user's Windows build? No application modifications or capture experiment validated this.

## If continuing implementation work

Use the existing decoded bytecode and pseudocode for narrow follow-up questions. Reestablish original artifact hashes before trusting a different copy. Runtime experiments require Windows; the current analysis host is Linux with no mounted Windows drive. User credentials have not been needed so far.

Any proposed workaround should be described as experimental until an actual coding-question image is captured and verified. Account for the reapplication handlers, relaunch/configuration persistence, archive integrity, and disabled debug fuses. Do not disable unrelated protections or alter the user's course data as an incidental change.

Do not assume the user can modify instructor/admin settings: they explicitly have student access only. A course-owner policy change is a separate possible route, but no administrative UI or exact procedure was inspected.

Do not invent details of their prior attempts: the precise list of screenshot tools/APIs is unknown. Avoid repeatedly asking for facts already answered in `01-user-context.md`.

## Project integration boundaries

This handoff does not establish that the existing God Window project's Windows capture paths were audited against this target. The project already had modified documentation and untracked work when this folder was created. Existing `windows/SCREENSHOT_INVESTIGATION.md`, `CAPTURE_REVIEW_WINDOWS11.md`, `SCREENSHOTS.md`, and source code may provide separate context, but were not validated by this CodeTantra investigation. Read them independently before integrating a change.

REA exports were saved and the active binary was closed. Investigation memory was reset after export, so a future agent must reopen/import as needed. No target was left running by this analysis.
