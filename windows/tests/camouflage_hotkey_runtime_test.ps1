# Run explicitly on an interactive desktop with Cronq already running.
param([string]$BinDir = (Join-Path $PSScriptRoot '../build/Release'))
$ErrorActionPreference='Stop'
$cli=Join-Path $BinDir 'cronq.exe'
$keys=Join-Path $BinDir 'sendkeys.exe'
function Mode {
    $status=& $cli status
    if ($status -notmatch 'APPEARANCE=(\d)') { throw "Missing appearance status: $status" }
    return [int]$Matches[1]
}
& $keys alt+k+x
$initial=Mode
try {
    if ($initial -ge 2) { & $keys alt+c }
    $normal=Mode
    & $keys alt+c
    $camouflage=if($normal -eq 0){3}else{2}
    if ((Mode) -ne $camouflage) { throw 'Alt+C did not enable camouflage' }
    & $keys alt+t
    if ((Mode) -ne (5-$camouflage)) { throw 'Alt+T did not invert transparent text color' }
    & $keys alt+t
    if ((Mode) -ne $camouflage) { throw 'Alt+T did not restore transparent text color' }
    & $keys alt+c
    if ((Mode) -ne $normal) { throw 'Alt+C did not restore previous appearance' }
    & $keys alt+k alt+c alt+t
    if ((Mode) -ne $normal) { throw 'Alt+C bypassed the temporary lock' }
    & $keys alt+k+x
    & $keys alt+c
    if ((Mode) -ne $camouflage) { throw 'Alt+C did not work after unlock' }
    & $keys alt+c
    Write-Output 'camouflage_hotkey_runtime_test: all checks passed'
} finally {
    & $keys alt+k+x
    if ((Mode) -ne $initial) { & $keys alt+c }
    & $cli show
}
