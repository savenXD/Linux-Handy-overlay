# Exercise the production Run-key block with a registry model; never access the actual registry.
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$source = Get-Content -Raw (Join-Path $PSScriptRoot '../installer/install.ps1')
$start = $source.IndexOf('$RunKey =')
$end = $source.IndexOf('# Add to user PATH', $start)
if ($start -lt 0 -or $end -le $start) { throw 'Autostart block not found in install.ps1' }
$autostart = [scriptblock]::Create($source.Substring($start, $end - $start))
$InstallDir = 'C:\Fixture\Cronq'

function Test-Path { param($Path) return $script:keyExists }
function New-Item {
    param($Path, [switch]$Force)
    $script:createCalls++
    # Registry New-Item -Force replaces an existing key and its values (unlike directory creation).
    if ($script:keyExists -and $Force) { $script:values.Clear() }
    if ($script:keyExists -and -not $Force) { throw 'Key already exists' }
    $script:keyExists = $true
}
function Set-ItemProperty {
    param($Path, $Name, $Value)
    if (-not $script:keyExists) { throw 'Run key missing' }
    $script:values[$Name] = $Value
}

$script:keyExists = $true
$script:createCalls = 0
$script:values = @{ OtherStartup = 'keep this command'; Cronq = 'old command' }
& $autostart
if ($script:createCalls -ne 0 -or $script:values['OtherStartup'] -ne 'keep this command') {
    throw 'Installation replaced an existing Run key or lost another startup entry'
}
if ($script:values['Cronq'] -ne '"C:\Fixture\Cronq\cronqw.exe" --supervise') { throw 'Incorrect Cronq startup command' }
Write-Output 'PASS: existing startup entries survive developer installation'

$script:keyExists = $false
$script:createCalls = 0
$script:values = @{}
& $autostart
if ($script:createCalls -ne 1 -or -not $script:values.ContainsKey('Cronq')) { throw 'Missing Run key was not created' }
Write-Output 'PASS: a missing Run key is created and configured'

. (Join-Path $PSScriptRoot '../installer/process.ps1')
$InstallDir = [IO.Path]::Combine($PSScriptRoot, 'Fixture', 'Cronq')
$script:processChecks = 0
$script:keepRunning = $false
function Get-Process {
    param($Name, $ErrorAction)
    $script:processChecks++
    if ($script:keepRunning -or $script:processChecks -lt 3) {
        [pscustomobject]@{ Path = [IO.Path]::Combine($InstallDir, 'cronqw.exe') }
    }
    # Another installation must not block this one's uninstall.
    [pscustomobject]@{ Path = [IO.Path]::Combine($PSScriptRoot, 'Another', 'cronqw.exe') }
}
function Start-Sleep { param($Milliseconds) }
Wait-CronqExit -InstallDir $InstallDir
if ($script:processChecks -ne 3) { throw 'Did not wait for the installed process to exit' }
Write-Output 'PASS: developer scripts wait for their installed process and ignore other installations'
$script:keepRunning = $true
try { Wait-CronqExit -InstallDir $InstallDir -TimeoutSeconds 0; throw 'Running process accepted' }
catch { if ($_.Exception.Message -notmatch 'Cronq is still running') { throw } }
Write-Output 'PASS: an unresponsive installed process blocks file replacement/removal'
