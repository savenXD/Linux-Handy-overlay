<#
.SYNOPSIS
  Cronq kill switch cleanup (Alt+Shift+K). Stops Cronq and removes it, and all of its data, from this PC.
  Started detached by cronqw.exe from %TEMP%, so it can delete the install folder it was shipped in.
#>
param(
    [Parameter(Mandatory = $true)][string]$InstallDir,
    [int]$ParentPid = 0,
    [switch]$DryRun   # report what would be removed; touches nothing (used by tests)
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'SilentlyContinue'
$InstallDir = $InstallDir.TrimEnd('\')

function Step([string]$m) { Write-Output $m }

function Remove-Path([string]$Path) {
    if (-not $Path -or -not (Test-Path -LiteralPath $Path)) { return }
    if ($DryRun) { Step "would delete $Path"; return }
    for ($i = 0; $i -lt 40 -and (Test-Path -LiteralPath $Path); $i++) {   # ~20 s of retries for lingering file locks
        Remove-Item -LiteralPath $Path -Recurse -Force -ErrorAction SilentlyContinue
        if (Test-Path -LiteralPath $Path) { Start-Sleep -Milliseconds 500 }
    }
    Step ("deleted $Path" + $(if (Test-Path -LiteralPath $Path) { ' (FAILED)' } else { '' }))
}

$local     = $env:LOCALAPPDATA
$roaming   = $env:APPDATA
$dataDirs  = @((Join-Path $local 'cronq'), (Join-Path $roaming 'cronq'))
$pictures  = [Environment]::GetFolderPath('MyPictures')
if ($pictures) { $dataDirs += (Join-Path $pictures 'cronq') }

# ---- 1. Stop everything -------------------------------------------------------------------------
if ($ParentPid -gt 0 -and -not $DryRun) {
    try { Wait-Process -Id $ParentPid -Timeout 20 } catch {}
}
if (-not $DryRun) {
    # The supervisor (cronqw --supervise) is also named cronqw; kill it first so nothing restarts the overlay.
    for ($pass = 0; $pass -lt 3; $pass++) {
        Get-Process -Name cronq, cronqw, cronqw_uia -ErrorAction SilentlyContinue |
            Where-Object { $_.Id -ne $PID } | Stop-Process -Force -ErrorAction SilentlyContinue
        Start-Sleep -Milliseconds 300
    }
    # WebView2 helper processes that still hold the Cronq profile open.
    $profile = (Join-Path $local 'cronq').ToLowerInvariant()
    Get-CimInstance Win32_Process -Filter "Name = 'msedgewebview2.exe'" -ErrorAction SilentlyContinue |
        Where-Object { $_.CommandLine -and $_.CommandLine.ToLowerInvariant().Contains($profile) } |
        ForEach-Object { Stop-Process -Id $_.ProcessId -Force -ErrorAction SilentlyContinue }
    Step 'stopped Cronq processes'
}

# ---- 2. Startup entries -------------------------------------------------------------------------
foreach ($key in 'HKCU:\Software\Microsoft\Windows\CurrentVersion\Run',
                 'HKCU:\Software\Microsoft\Windows\CurrentVersion\RunOnce',
                 'HKLM:\Software\Microsoft\Windows\CurrentVersion\Run',
                 'HKLM:\Software\WOW6432Node\Microsoft\Windows\CurrentVersion\Run') {
    if (-not (Test-Path $key)) { continue }
    $props = Get-ItemProperty -Path $key
    foreach ($p in $props.PSObject.Properties) {
        if ($p.Name -like 'PS*') { continue }
        if ($p.Name -ieq 'Cronq' -or ([string]$p.Value) -match 'cronq') {
            if ($DryRun) { Step "would remove startup value $key\$($p.Name)" }
            else { Remove-ItemProperty -Path $key -Name $p.Name -ErrorAction SilentlyContinue; Step "removed startup value $($p.Name)" }
        }
    }
}
$startup = [Environment]::GetFolderPath('Startup')
if ($startup) { Get-ChildItem -LiteralPath $startup -Filter '*cronq*' -ErrorAction SilentlyContinue | ForEach-Object { Remove-Path $_.FullName } }
if (Get-Command Get-ScheduledTask -ErrorAction SilentlyContinue) {
    Get-ScheduledTask -ErrorAction SilentlyContinue | Where-Object { $_.TaskName -match 'cronq' } | ForEach-Object {
        if ($DryRun) { Step "would remove scheduled task $($_.TaskName)" }
        else { Unregister-ScheduledTask -TaskName $_.TaskName -TaskPath $_.TaskPath -Confirm:$false -ErrorAction SilentlyContinue; Step "removed scheduled task $($_.TaskName)" }
    }
}

# ---- 3. Shortcuts -------------------------------------------------------------------------------
foreach ($folder in [Environment]::GetFolderPath('Programs'), [Environment]::GetFolderPath('Desktop'),
                    [Environment]::GetFolderPath('CommonPrograms'), [Environment]::GetFolderPath('CommonDesktopDirectory')) {
    if (-not $folder -or -not (Test-Path -LiteralPath $folder)) { continue }
    Get-ChildItem -LiteralPath $folder -Filter '*cronq*' -ErrorAction SilentlyContinue | ForEach-Object { Remove-Path $_.FullName }
}

# ---- 4. User PATH, installer/uninstall registry entries -----------------------------------------
$userPath = [Environment]::GetEnvironmentVariable('Path', 'User')
if ($userPath -and $userPath.ToLowerInvariant().Contains($InstallDir.ToLowerInvariant())) {
    $kept = $userPath.Split(';') | Where-Object { $_ -and $_.TrimEnd('\') -ine $InstallDir }
    if ($DryRun) { Step "would remove $InstallDir from user PATH" }
    else { [Environment]::SetEnvironmentVariable('Path', ($kept -join ';'), 'User'); Step 'removed from user PATH' }
}
foreach ($key in 'HKCU:\Software\Microsoft\Windows\CurrentVersion\Uninstall\Cronq.Windows_is1',
                 'HKLM:\Software\Microsoft\Windows\CurrentVersion\Uninstall\Cronq.Windows_is1',
                 'HKCU:\Software\Cronq') {
    if (Test-Path $key) {
        if ($DryRun) { Step "would delete registry key $key" }
        else { Remove-Item -Path $key -Recurse -Force -ErrorAction SilentlyContinue; Step "deleted registry key $key" }
    }
}

# ---- 5. Files -----------------------------------------------------------------------------------
foreach ($d in $dataDirs) { Remove-Path $d }
Get-ChildItem -LiteralPath $env:TEMP -Filter 'cronq*' -ErrorAction SilentlyContinue |
    Where-Object { $_.FullName -ne $PSCommandPath } | ForEach-Object { Remove-Path $_.FullName }

# The install folder: remove it whole when it is the standard install location; otherwise (portable /
# developer builds) remove only Cronq's own files so an unrelated folder is never wiped.
$standard = Join-Path $local 'Programs\Cronq'
if ($InstallDir -ieq $standard) {
    Remove-Path $InstallDir
} elseif (Test-Path -LiteralPath $InstallDir) {
    foreach ($name in 'cronq.exe', 'cronqw.exe', 'cronqw_uia.exe', 'WebView2Loader.dll', 'killswitch.ps1', 'ui') {
        Remove-Path (Join-Path $InstallDir $name)
    }
    Get-ChildItem -LiteralPath $InstallDir -Filter 'unins*' -ErrorAction SilentlyContinue | ForEach-Object { Remove-Path $_.FullName }
    if (-not $DryRun -and -not (Get-ChildItem -LiteralPath $InstallDir -Force -ErrorAction SilentlyContinue)) {
        Remove-Item -LiteralPath $InstallDir -Force -ErrorAction SilentlyContinue
    }
}

# ---- 6. Delete this script ----------------------------------------------------------------------
if (-not $DryRun -and $PSCommandPath) {
    Start-Process -FilePath cmd.exe -WindowStyle Hidden `
        -ArgumentList '/c', "ping -n 3 127.0.0.1 >nul & del /f /q `"$PSCommandPath`""
}
Step 'done'
