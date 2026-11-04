# Run explicitly against a running Cronq on an interactive Windows desktop.
# Exercises only the temporary lock; never sends the destructive kill chord.
param([string]$BinDir = (Join-Path $PSScriptRoot '../build/Release'))
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$cli = Join-Path $BinDir 'cronq.exe'
$keys = Join-Path $BinDir 'sendkeys.exe'
function Assert-Status([string]$Expected) {
    $status = & $cli status
    if ($status -notmatch $Expected) { throw "Expected $Expected, got $status" }
}
try {
    & $keys alt+k+x # Start unlocked, regardless of initial lock state.
    & $cli show
    & $keys alt+k
    Assert-Status 'VISIBLE=FALSE'
    Assert-Status 'LOCKED=TRUE'
    $lockedStatus = & $cli status
    & $keys alt+x
    Assert-Status 'VISIBLE=FALSE' # Regression: swallowed X used to dispatch Toggle.
    & $keys alt+x alt+x alt+g alt+m
    Assert-Status 'VISIBLE=FALSE'
    if ((& $cli status) -ne $lockedStatus) { throw 'A disabled shortcut changed the locked state' }
    & $keys alt+k+x
    Assert-Status 'VISIBLE=TRUE'
    Assert-Status 'LOCKED=FALSE'
    & $keys alt+k
    Assert-Status 'VISIBLE=FALSE'
    & $keys alt+x+k # Reverse unlock order must work too.
    Assert-Status 'VISIBLE=TRUE'
    & $keys alt+x
    Assert-Status 'VISIBLE=FALSE' # Normal toggle works after unlock.
    & $keys alt+x
    Assert-Status 'VISIBLE=TRUE'
    Write-Output 'lock_hotkey_runtime_test: all checks passed'
} finally {
    & $keys alt+k+x
    & $cli show
}
