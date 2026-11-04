# Shared by the developer install/uninstall scripts. Wait only for processes from this installation.
function Wait-CronqExit {
    param(
        [Parameter(Mandatory = $true)][string]$InstallDir,
        [ValidateRange(0, 300)][int]$TimeoutSeconds = 15
    )
    $cronqDeadline = [DateTime]::UtcNow.AddSeconds($TimeoutSeconds)
    while ($true) {
        $cronqRunning = @(Get-Process -Name cronq,cronqw -ErrorAction SilentlyContinue | Where-Object {
            try {
                $cronqCandidatePath = $_.Path
                $cronqCandidatePath -and
                    ([IO.Path]::GetDirectoryName($cronqCandidatePath).TrimEnd('\') -ieq $InstallDir.TrimEnd('\'))
            } catch { $false } # A process may exit while its path is being queried.
        })
        if ($cronqRunning.Count -eq 0) { return }
        if ([DateTime]::UtcNow -ge $cronqDeadline) {
            throw "Cronq is still running from $InstallDir. Close it before continuing."
        }
        Start-Sleep -Milliseconds 100
    }
}
