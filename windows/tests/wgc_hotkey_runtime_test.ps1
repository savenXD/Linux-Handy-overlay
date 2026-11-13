# Explicit interactive test: starts a WGC capture and may replace the clipboard/save a screenshot.
# Use a disposable Windows 11 desktop with a running test daemon, never a sensitive desktop.
param([string]$BinDir = (Join-Path $PSScriptRoot '../build/Release'))
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$cli = Join-Path $BinDir 'cronq.exe'
$keys = Join-Path $BinDir 'sendkeys.exe'
$log = Join-Path $env:LOCALAPPDATA 'cronq/cronq.log'
function Attempts([switch]$Window) {
    if (!(Test-Path $log)) { return 0 }
    $pattern = if ($Window) { 'WGC window experimental capture requested' } else { 'WGC experimental capture requested' }
    return ([regex]::Matches((Get-Content $log -Raw), $pattern)).Count
}
$initial = & $cli status
if ($LASTEXITCODE -ne 0) { throw 'Start the test daemon first' }
$wasLocked = $initial -match 'LOCKED=TRUE'
$wasVisible = $initial -match 'VISIBLE=TRUE'
try {
    if ($wasLocked) { & $keys alt+k+x }
    & $cli hide
    $before = Attempts
    $beforeWindow = Attempts -Window
    & $keys ctrl+alt+o ctrl+alt+shift+o
    Start-Sleep -Milliseconds 200
    if ((Attempts) -ne $before) { throw 'Ctrl+Alt+O unexpectedly captured' }
    if ((Attempts -Window) -ne $beforeWindow) { throw 'Ctrl+Alt+Shift+O unexpectedly captured' }
    & $keys alt+k alt+o alt+shift+o
    Start-Sleep -Milliseconds 200
    if ((Attempts) -ne $before) { throw 'Alt+O bypassed the temporary lock' }
    if ((Attempts -Window) -ne $beforeWindow) { throw 'Alt+Shift+O bypassed the temporary lock' }
    & $keys alt+k+x
    & $cli hide
    & $keys alt+o
    $deadline = (Get-Date).AddSeconds(15)
    do {
        $text = Get-Content $log -Raw
        $last = $text.LastIndexOf('WGC experimental capture requested')
        $completed = (Attempts) -eq ($before + 1) -and $last -ge 0 -and
            $text.Substring($last).Contains('WGC test:')
        if (!$completed) { Start-Sleep -Milliseconds 100 }
    } while (!$completed -and (Get-Date) -lt $deadline)
    if (!$completed) { throw 'Alt+O did not dispatch and complete one experimental capture' }
    if ((& $cli status) -notmatch 'VISIBLE=FALSE') { throw 'Alt+O changed hidden overlay visibility' }
    & $keys alt+shift+o
    $deadline = (Get-Date).AddSeconds(15)
    do {
        $text = Get-Content $log -Raw
        $last = $text.LastIndexOf('WGC window experimental capture requested')
        $completed = (Attempts -Window) -eq ($beforeWindow + 1) -and $last -ge 0 -and
            $text.Substring($last).Contains('WGC window test:')
        if (!$completed) { Start-Sleep -Milliseconds 100 }
    } while (!$completed -and (Get-Date) -lt $deadline)
    if (!$completed) { throw 'Alt+Shift+O did not dispatch and complete one window capture' }
    if ((& $cli status) -notmatch 'VISIBLE=FALSE') { throw 'Alt+Shift+O changed hidden overlay visibility' }
    Write-Output 'wgc_hotkey_runtime_test: routing, modifiers, lock and hidden-state checks passed'
    Write-Output 'Capture success is a separate check: run capture_runtime_test.exe --require-wgc'
} finally {
    $status = & $cli status
    if ($status -match 'LOCKED=TRUE') { & $keys alt+k+x }
    if ($wasLocked) { & $keys alt+k }
    elseif ($wasVisible) { & $cli show }
    else { & $cli hide }
}
