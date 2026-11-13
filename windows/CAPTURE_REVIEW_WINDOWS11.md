# Windows 11 adversarial capture review

Scope: Windows 11 x64. The goal is reliable capture of the permitted display
image, with explicit reporting when the OS withholds pixels. This review does
not establish how the reported custom focus app is implemented; no copy or
native reproduction of that app was available.

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
| Another supported capture backend can access the permitted image | Temporary Alt+O WGC-only experiment, with runtime support checks and Windows' capture border left enabled; Alt+P unchanged |
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

## Adversarial tests

`capture_runtime_test` uses two windows belonging only to the test process: a
red/blue foreground fixture and a green backing fixture. On native Windows 11 it
checks that `WDA_MONITOR` produces blank pixels through GDI and that exclusion
reveals the green fixture. It verifies that Cronq reports protected foreground
content and that normal capture recovers after the test removes its own
protection. It does not alter another application's protections.

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
