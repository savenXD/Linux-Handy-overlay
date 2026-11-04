# Explicit interactive test of the running overlay; no destructive shortcuts.
param([string]$BinDir=(Join-Path $PSScriptRoot '../build/Release'))
$ErrorActionPreference='Stop'
$cli=Join-Path $BinDir 'cronq.exe';$keys=Join-Path $BinDir 'sendkeys.exe'
function Geometry {
    $value=& $cli --get-geometry
    if($value -notmatch 'X=(-?\d+) Y=(-?\d+) W=(\d+) H=(\d+)'){throw "Unexpected geometry: $value"}
    return @{X=[int]$Matches[1];Y=[int]$Matches[2];W=[int]$Matches[3];H=[int]$Matches[4]}
}
function Assert-Same($a,$b) {foreach($k in 'X','Y','W','H'){if($a[$k] -ne $b[$k]){throw "Geometry changed unexpectedly at $k"}}}
& $keys alt+k+x
& $keys alt+left
$left=Geometry
& $keys alt+right
$right=Geometry
if($right.X -le $left.X -or $right.Y -ne $left.Y -or $right.W -ne $left.W -or $right.H -ne $left.H){throw 'Corner movement did not preserve size and bottom alignment'}
& $cli --reload-config
Assert-Same $right (Geometry)
& $cli hide
& $cli show
Assert-Same $right (Geometry)
& $keys alt+k alt+left
Assert-Same $right (Geometry)
& $keys alt+k+x
& $keys alt+left
Assert-Same $left (Geometry)
Write-Output 'corner_hotkey_runtime_test: all checks passed'
