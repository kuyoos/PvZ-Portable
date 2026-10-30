# SPDX-License-Identifier: LGPL-3.0-or-later
#
# Local M0 harness: starts the lobby, runs two scripted clients (host + guest)
# and prints both transcripts. Usage:
#
#   pwsh -File scripts/demo.ps1
#   pwsh -File scripts/demo.ps1 -Port 7800
#
param(
    [int]$Port = 7777
)

$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot
$bin = Join-Path $root "bin"
$stamp = Get-Date -Format "HHmmss"
$logs = Join-Path ([System.IO.Path]::GetTempPath()) "pvzlobby-$stamp"
New-Item -ItemType Directory -Force -Path $logs | Out-Null

function Wait-Port([int]$port, [int]$timeoutMs) {
    $deadline = (Get-Date).AddMilliseconds($timeoutMs)
    while ((Get-Date) -lt $deadline) {
        $client = New-Object System.Net.Sockets.TcpClient
        try {
            $client.Connect("127.0.0.1", $port)
            $client.Close()
            return $true
        }
        catch {
            $client.Dispose()
            Start-Sleep -Milliseconds 100
        }
    }
    return $false
}

# Free the port if an earlier run is still holding it.
Get-NetTCPConnection -LocalPort $Port -State Listen -ErrorAction SilentlyContinue |
    ForEach-Object { Stop-Process -Id $_.OwningProcess -Force -ErrorAction SilentlyContinue }
Start-Sleep -Milliseconds 200

Push-Location $root
try {
    & go build -o (Join-Path $bin "pvz-lobby.exe") ./cmd/pvz-lobby
    & go build -o (Join-Path $bin "pvz-cli.exe") ./cmd/pvz-cli
}
finally {
    Pop-Location
}

$srvOut = Join-Path $logs "server.out"
$srvErr = Join-Path $logs "server.err"
$srv = Start-Process -FilePath (Join-Path $bin "pvz-lobby.exe") `
    -ArgumentList "-listen", "127.0.0.1:$Port" `
    -RedirectStandardOutput $srvOut -RedirectStandardError $srvErr -PassThru

if (-not (Wait-Port $Port 8000)) {
    Write-Host "server did not start, log:"
    Get-Content $srvErr -Encoding UTF8
    exit 1
}

$aOut = Join-Path $logs "host.out"
$aErr = Join-Path $logs "host.err"
$bOut = Join-Path $logs "guest.out"
$bErr = Join-Path $logs "guest.err"

$host_cli = Start-Process -FilePath (Join-Path $bin "pvz-cli.exe") `
    -ArgumentList "-nick", "PlayerA", "-addr", "127.0.0.1:$Port" `
    -RedirectStandardInput (Join-Path $root "scripts/demo-a.txt") `
    -RedirectStandardOutput $aOut -RedirectStandardError $aErr -PassThru

Start-Sleep -Milliseconds 800

$guest_cli = Start-Process -FilePath (Join-Path $bin "pvz-cli.exe") `
    -ArgumentList "-nick", "PlayerB", "-addr", "127.0.0.1:$Port" `
    -RedirectStandardInput (Join-Path $root "scripts/demo-b.txt") `
    -RedirectStandardOutput $bOut -RedirectStandardError $bErr -PassThru

$null = $host_cli.WaitForExit(25000)
$null = $guest_cli.WaitForExit(25000)

if (-not $host_cli.HasExited) { $host_cli.Kill() }
if (-not $guest_cli.HasExited) { $guest_cli.Kill() }
if (-not $srv.HasExited) { $srv.Kill() }

Write-Host "=== server ==="
Get-Content $srvErr -Encoding UTF8
Write-Host "=== host (PlayerA) ==="
Get-Content $aOut -Encoding UTF8
if ((Get-Item $aErr).Length -gt 0) { Get-Content $aErr -Encoding UTF8 }
Write-Host "=== guest (PlayerB) ==="
Get-Content $bOut -Encoding UTF8
if ((Get-Item $bErr).Length -gt 0) { Get-Content $bErr -Encoding UTF8 }
Write-Host "logs: $logs"
