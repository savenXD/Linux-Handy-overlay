# Experimental renderer capture probe

This standalone Windows test uses Chromium's `Page.captureScreenshot` through an
application's existing local debugging endpoint. It does not use GDI, DXGI or WGC.
It captures the selected renderer page, not necessarily every native window or
separate webview in the application.

**Status:** protocol and PNG output verified against an isolated Chromium page
on Linux using PowerShell 7. Five integration checks pass. Windows PowerShell
5.1 compatibility is intended but has not been run natively here. Neither Windows
capture-exclusion behavior nor Deepak's app has been validated. This is not an
integrated Cronq screenshot backend or a proven protection bypass.

## Windows trial

Keep the script from this repository or extract `renderer-capture-probe.zip` into
a folder. Run the commands from that folder (or use the full script path).

1. The intended Electron app must expose a loopback renderer debugging endpoint.
   If the challenge build accepts Electron's documented launch switches, close it
   normally first, then launch its actual executable with:

   ```powershell
   & 'C:\Path\To\DeepakApp.exe' --remote-debugging-address=127.0.0.1 --remote-debugging-port=9222
   ```

   This is an optional launch route, not a guarantee. An application may remove
   or reject the switches. The probe cannot create a debugging connection from
   outside an application that does not expose one. It does not patch the app,
   inject a helper, alter content protection or terminate another process.

2. Open the content you intend to capture. In PowerShell, list available pages:

   ```powershell
   powershell.exe -NoProfile -File .\Capture-Renderer.ps1 -ListTargets
   ```

   If the app prevents switching to PowerShell while fullscreen, run the command
   beforehand with `Start-Sleep -Seconds 15;` prefixed, then return to the app
   during the delay. The probe takes no window focus. This scheduling helps run
   the trial; it does not overcome capture exclusion or guarantee the app leaves
   PowerShell running.

3. Select the intended page ID from that output, then capture it:

   ```powershell
   powershell.exe -NoProfile -File .\Capture-Renderer.ps1 -TargetId 'PASTE-TARGET-ID'
   ```

   If there are several IDs, select the intended page with the app's developer
   or use an isolated test session; the tool deliberately does not capture every
   page automatically. IDs can change after app relaunch or page replacement.

4. For a delayed capture while the app is fullscreen:

   ```powershell
   Start-Sleep -Seconds 15
   powershell.exe -NoProfile -File .\Capture-Renderer.ps1 -TargetId 'PASTE-TARGET-ID'
   ```

   Return to the app during the delay. Open the newly returned PNG afterwards.

## Results

- `CapturedUnverified`: a PNG was returned by the selected renderer. Check its
  visible content; this status does not establish that the right page or any
  protected content was captured. Black PNGs are retained for inspection.
- `Unavailable` at `discovery`: no usable local debugging endpoint was reached.
  This means the renderer access route is unavailable, not that a capture API
  returned black pixels.
- `Unavailable` at `selection`: the ID is missing/ambiguous or discovery returned
  an endpoint outside the selected local port. Nothing is captured.
- `Unavailable` at `renderer-capture`: the renderer refused, disconnected, timed
  out or returned invalid data. No desktop fallback runs.

By default, each result has a new file under
`%LOCALAPPDATA%\cronq\renderer-probe`. The JSON output contains its path, dimensions
and SHA-256. Existing screenshots, latest PNG aliases and clipboard contents are
not replaced. The deadline defaults to 10 seconds, configurable up to 30 seconds.
The PNG response is size-bounded. App URLs, page titles and credentials are not
written to a manifest. Leave a debugging endpoint enabled only for the trial;
relaunch the app without the switches afterwards.

## Development verification

```bash
python3 windows/tests/renderer_capture_probe_test.py --pwsh /path/to/pwsh --chrome /path/to/chrome
```

These checks exercise real renderer image capture, unique outputs and hashes,
explicit page selection, unavailable endpoints and refusal of nonlocal
WebSocket endpoints. They use their own browser process and disposable profile.
They do not validate Windows protection or the target application.

References:
- [Chromium Page.captureScreenshot](https://chromedevtools.github.io/devtools-protocol/tot/Page/#method-captureScreenshot)
- [Electron supported debugging switch](https://www.electronjs.org/docs/latest/api/command-line-switches#--remote-debugging-portport)
