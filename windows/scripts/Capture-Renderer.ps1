<#
.SYNOPSIS
Capture a selected Electron/Chromium renderer through its existing local CDP endpoint.
.DESCRIPTION
Experimental producer-side capture. Requires an application that exposes remote debugging.
Does not inject into a process, change window protection, or fall back to desktop capture.
A returned PNG is unverified until its visible contents have been inspected.
#>
[CmdletBinding(DefaultParameterSetName='Capture')]
param(
    [ValidateRange(1,65535)][int]$Port = 9222,
    [Parameter(Mandatory=$true,ParameterSetName='Capture')]
    [ValidateNotNullOrEmpty()][string]$TargetId,
    [Parameter(Mandatory=$true,ParameterSetName='List')][switch]$ListTargets,
    [string]$OutputDirectory = (Join-Path ([Environment]::GetFolderPath('LocalApplicationData')) 'cronq/renderer-probe'),
    [ValidateRange(1,30)][int]$TimeoutSeconds = 10
)
$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Net.Http
$budget = New-Object System.Threading.CancellationTokenSource
$budget.CancelAfter($TimeoutSeconds * 1000)
$handler = New-Object System.Net.Http.HttpClientHandler
$handler.AllowAutoRedirect = $false
$handler.UseProxy = $false
$http = New-Object System.Net.Http.HttpClient($handler)
$socket = $null
$response = $null
$stage = 'discovery'

function Read-LimitedBody($Stream, [int]$Limit) {
    $memory = New-Object System.IO.MemoryStream
    try {
        $chunk = New-Object byte[] 65536
        while ($true) {
            $count = $Stream.ReadAsync($chunk,0,$chunk.Length,$budget.Token).GetAwaiter().GetResult()
            if ($count -eq 0) { break }
            if ($memory.Length + $count -gt $Limit) { throw 'Discovery response exceeds size limit.' }
            $memory.Write($chunk,0,$count)
        }
        return ,$memory.ToArray()
    } finally { $memory.Dispose() }
}

try {
    $endpoint = [Uri]("http://127.0.0.1:{0}/json/list" -f $Port)
    try {
        $response = $http.GetAsync($endpoint,[System.Net.Http.HttpCompletionOption]::ResponseHeadersRead,$budget.Token).GetAwaiter().GetResult()
        if (-not $response.IsSuccessStatusCode) { throw 'Debug discovery returned an HTTP error or redirect.' }
        $stream = $response.Content.ReadAsStreamAsync().GetAwaiter().GetResult()
        try { $body = Read-LimitedBody $stream 2097152 } finally { $stream.Dispose() }
        $targets = [Text.Encoding]::UTF8.GetString($body) | ConvertFrom-Json
    } catch {
        throw 'No usable debugging endpoint at the selected local port. The app must expose renderer debugging; this probe cannot enable it externally.'
    }
    $pages = @($targets | Where-Object { $_.type -eq 'page' -and $_.webSocketDebuggerUrl })
    if ($ListTargets) {
        # Exclude page URLs and titles, which can contain account or document details.
        $items = @($pages | ForEach-Object { [ordered]@{ targetId=$_.id; type=$_.type } })
        ConvertTo-Json -InputObject ([ordered]@{status='Targets';port=$Port;targets=$items}) -Depth 5
    } else {
        $stage = 'selection'
        $selected = @($pages | Where-Object { $_.id -ceq $TargetId })
        if ($selected.Count -ne 1) { throw 'Target ID is absent or ambiguous. List targets again and explicitly select the intended page.' }
        $address = [Uri]$selected[0].webSocketDebuggerUrl
        # Never follow discovery data to another host, another port or a browser-level endpoint.
        if ($address.Scheme -ne 'ws' -or $address.Host -ne '127.0.0.1' -or
            $address.Port -ne $Port -or $address.UserInfo -or $address.Query -or $address.Fragment -or
            -not $address.AbsolutePath.StartsWith('/devtools/page/')) {
            throw 'Target WebSocket is not a page endpoint on the selected local port.'
        }
        $stage = 'renderer-capture'
        $socket = New-Object System.Net.WebSockets.ClientWebSocket
        $socket.ConnectAsync($address,$budget.Token).GetAwaiter().GetResult() | Out-Null
        $command = @{id=1;method='Page.captureScreenshot';params=@{format='png';fromSurface=$true;captureBeyondViewport=$false}} | ConvertTo-Json -Depth 4 -Compress
        $payload = [Text.Encoding]::UTF8.GetBytes($command)
        $segment = New-Object 'System.ArraySegment[byte]' -ArgumentList (,$payload)
        $socket.SendAsync($segment,[System.Net.WebSockets.WebSocketMessageType]::Text,$true,$budget.Token).GetAwaiter().GetResult() | Out-Null
        $frame = New-Object byte[] 65536
        $receiveSegment = New-Object 'System.ArraySegment[byte]' -ArgumentList (,$frame)
        $reply = $null
        while ($null -eq $reply) {
            $message = New-Object System.IO.MemoryStream
            try {
                do {
                    $part = $socket.ReceiveAsync($receiveSegment,$budget.Token).GetAwaiter().GetResult()
                    if ($part.MessageType -ne [System.Net.WebSockets.WebSocketMessageType]::Text) { throw 'Renderer closed the connection or sent a non-text response.' }
                    if ($message.Length + $part.Count -gt 33554432) { throw 'Renderer response exceeds 32 MiB limit.' }
                    $message.Write($frame,0,$part.Count)
                } while (-not $part.EndOfMessage)
                $event = [Text.Encoding]::UTF8.GetString($message.ToArray()) | ConvertFrom-Json
                if ($event.id -eq 1) { $reply = $event }
            } finally { $message.Dispose() }
        }
        if ($reply.error) { throw ("Renderer rejected capture: {0}" -f $reply.error.message) }
        if (-not $reply.result.data) { throw 'Renderer returned no screenshot data.' }
        $png = [Convert]::FromBase64String($reply.result.data)
        $signature = [byte[]](137,80,78,71,13,10,26,10)
        if ($png.Length -lt 33) { throw 'Renderer returned a truncated PNG.' }
        for ($n=0;$n -lt 8;$n++) { if ($png[$n] -ne $signature[$n]) { throw 'Renderer returned non-PNG data.' } }
        if ([Text.Encoding]::ASCII.GetString($png,12,4) -ne 'IHDR') { throw 'PNG has no image header.' }
        $width = [uint64]$png[16]*16777216 + [uint64]$png[17]*65536 + [uint64]$png[18]*256 + [uint64]$png[19]
        $height = [uint64]$png[20]*16777216 + [uint64]$png[21]*65536 + [uint64]$png[22]*256 + [uint64]$png[23]
        if ($width -eq 0 -or $height -eq 0 -or $width -gt 32768 -or $height -gt 32768 -or $width*$height -gt 100000000) { throw 'PNG dimensions are outside the supported limits.' }
        $stage = 'save'
        $folder = [IO.Path]::GetFullPath($OutputDirectory)
        [IO.Directory]::CreateDirectory($folder) | Out-Null
        $name = 'renderer-{0}-{1}.png' -f [DateTime]::UtcNow.ToString('yyyyMMddTHHmmssfffZ'),[Guid]::NewGuid().ToString('N')
        $path = Join-Path $folder $name
        $file = [IO.File]::Open($path,[IO.FileMode]::CreateNew,[IO.FileAccess]::Write,[IO.FileShare]::None)
        try { $file.Write($png,0,$png.Length) } finally { $file.Dispose() }
        $sha = [Security.Cryptography.SHA256]::Create()
        try { $digest = ([BitConverter]::ToString($sha.ComputeHash($png))).Replace('-','').ToLowerInvariant() } finally { $sha.Dispose() }
        ConvertTo-Json -InputObject ([ordered]@{status='CapturedUnverified';method='chromium-renderer-cdp';targetId=$TargetId;width=$width;height=$height;path=$path;sha256=$digest;contentVerified=$false})
    }
} catch {
    ConvertTo-Json -InputObject ([ordered]@{status='Unavailable';stage=$stage;message=$_.Exception.Message})
    exit 2
} finally {
    if ($socket) { $socket.Abort();$socket.Dispose() }
    if ($response) { $response.Dispose() }
    $http.Dispose();$handler.Dispose();$budget.Dispose()
}
