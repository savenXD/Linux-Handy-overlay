# Windows 11 adversarial capture review

Scope: Windows 11 x64. The goal is reliable application screenshots, with accurate
reporting of capture restrictions and failures. **The user reports that the
implemented approaches still fail against Deepak's custom focus/Pomodoro app.**
This review does not establish that app's implementation: no copy or controlled
native reproduction was available. The mechanism table below lists hypotheses.
See [the investigation record](SCREENSHOT_INVESTIGATION.md) for attempts, evidence,
research and the next experiment.

## Defender perspective (person A)

| Blocking mechanism | Visible result | Consequence for capture |
| --- | --- | --- |
| `WDA_MONITOR` on an application's own fullscreen window | The user sees normal content | Compatible capture APIs receive a blank window; fullscreen coverage can make the whole screenshot black |
| `WDA_EXCLUDEFROMCAPTURE` on an application's own window | The user sees normal content | Compatible captures omit that window; the result can show the desktop behind it, possibly black |
| Display-only or hardware-protected graphics resources | Normal local presentation when the hardware supports it | Some surfaces are unavailable to desktop capture; protected content can be masked |
| A separate input desktop or a secure desktop transition | The active display changes desktop | A process attached to another desktop cannot assume access to the active desktop image |
| API-specific rendering incompatibility or interception | Normal local presentation | One capture backend may fail or return black even when another supported backend works |

The first two mechanisms are distinct: blanking and omission are not equivalent.
Fullscreen and administrator privileges alone do not prove capture protection.
Black pixels alone also do not prove deliberate blocking.

## Capture engineer perspective (person B)

| Situation | Reliable engineering response |
| --- | --- |
| GDI cannot read a normal GPU-rendered app | Use DXGI Desktop Duplication, already implemented |
| Duplication is unsupported, busy, lost or returns an ambiguous black output | Recreate a lost duplication session once; use the compatibility fallback and bounded waits, already implemented |
| Another supported capture backend can access the image | Temporary Alt+O monitor and Alt+Shift+O foreground-window WGC-only experiments, with runtime checks and Windows' capture border enabled; not demonstrated to fix the focus app |
| Explicit Windows protection or inaccessible desktop | Report the restriction; changing screenshot API does not guarantee recovery of withheld pixels |
| A broken encoder, file write or clipboard publication | Test pixels independently of encoding, compare saved and clipboard PNGs, and report publication failures separately; implemented and exercised under Wine |
| Need to claim Windows 11 compatibility | Run the native GPU/protection tests on real Windows 11; Wine success is insufficient |

DXGI is already GPU-based capture. It is not a read of a universal physical
framebuffer. A displayed image can include separately composed planes, a hardware
cursor, protected surfaces and display processing. Being able to see a pixel does
not imply that Windows exposes that pixel to an application.

A new kernel driver is not a universal hardware screenshot feature. Production
kernel drivers have signing requirements and introduce deployment and maintenance
costs. An indirect display driver creates an indirect/virtual display device; it
is not a general reader for every existing physical monitor. Neither approach
meets a requirement of no behavioral, deployment or compatibility downside.

External capture hardware is a separate integration and does not provide an
unconditional answer either: it requires additional equipment and a capturable
output signal, with output protection and display configuration as constraints.

## Experimental confound and target-specific research

Current v1.3.0 source checks foreground display affinity **before** attempting any
capture backend. Nonzero affinity returns `ProtectedContent`; direct-window WGC
has the same early exit. The user failure report alone therefore does not prove
that all three APIs ran and returned black. Removing this experimental confound
was proposed, but no such change or raw-frame diagnostic mode is implemented.

Published [IOActive research](https://www.ioactive.com/signal-windows-desktop-contentprotection-bypass/)
reports removing Signal Desktop's standard affinity protection by changing it
inside the owning process. This is a concrete research lead, not an implemented
Cronq feature or a verified result for Deepak's app. Standard capture exclusion
must not be conflated with protected-video/DRM resources or a secure desktop.

Direct rendering capture and compositor modifications were also researched;
neither was implemented or tested here. A VM-host capture proposal was rejected
by the user, who has Windows in dual boot. The
[research and tooling record](SCREENSHOT_INVESTIGATION.md#research-findings-candidates-not-implemented-fixes)
preserves sources, limitations, REA readiness and the target information still
needed. An explanation of an OS restriction is not successful focus-app acceptance.

## Adversarial tests

`capture_runtime_test` uses two windows belonging only to the test process: a
red/blue foreground fixture and a green backing fixture. On native Windows 11 it
checks that `WDA_MONITOR` produces blank pixels through GDI and that exclusion
reveals the green fixture. It verifies that Cronq reports protected foreground
content and that normal capture recovers after the test removes its own
protection. It does not alter another application's protections.

These fixtures verify current restriction reporting; they do not test a
target-specific bypass. They would need review alongside a future change to the
experimental precheck behavior.

Run on a disposable, unlocked Windows 11 test desktop:

```powershell
& windows/build/Release/capture_runtime_test.exe --require-dxgi --require-protection
```

The required flags make missing native support a failure, rather than a skip.
Run `--daemon-snap` with a test daemon to exercise capture, saving, clipboard
formats and a busy clipboard. These tests replace the clipboard and take
screenshots of the test desktop.

The older Windows 10 compatibility investigation was dropped when the requested
scope changed to Windows 11. No claim of support for every Windows 10 build is
made by this review.

## Primary references

- [Window display affinity and Windows 10 2004 semantics](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-setwindowdisplayaffinity)
- [Display-only and protected swap-chain resources](https://learn.microsoft.com/en-us/windows/win32/api/dxgi/ne-dxgi-dxgi_swap_chain_flag)
- [Desktop duplication access errors and output ownership](https://learn.microsoft.com/en-us/windows/win32/api/dxgi1_2/nf-dxgi1_2-idxgioutput1-duplicateoutput)
- [Windows Graphics Capture](https://learn.microsoft.com/en-us/windows/apps/develop/media-authoring-processing/screen-capture)
- [Capture border and consent behavior](https://learn.microsoft.com/en-us/uwp/api/windows.graphics.capture.graphicscapturesession.isborderrequired)
- [Indirect display driver model](https://learn.microsoft.com/en-us/windows-hardware/drivers/display/indirect-display-driver-model-overview)
- [Driver signing requirements](https://learn.microsoft.com/en-us/windows-hardware/drivers/install/kernel-mode-code-signing-policy--windows-vista-and-later-)
