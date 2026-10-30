#!/usr/bin/env pwsh
# Push the branch and start a CI build on GitHub.
#
# Why this exists: pushes to this fork do NOT start workflows by themselves
# (GitHub keeps workflow triggering disabled on forks), so the run has to be
# dispatched explicitly. Requires a stored GitHub credential for github.com.
#
# Usage: ./scripts/ci-build.ps1 [-Ref main]
param([string]$Ref = 'main')

$ErrorActionPreference = 'Stop'

$repo = 'kuyoos/PvZ-Portable'
$workflow = 'ci.yml'

$env:GIT_TERMINAL_PROMPT = '0'
$cred = ("protocol=https`nhost=github.com`n`n" | git credential fill) | Out-String
$m = [regex]::Match($cred, 'password=(?<t>.+)')
if (-not $m.Success) { throw 'no GitHub credential for github.com (git credential fill returned nothing)' }
$headers = @{ Authorization = 'Bearer ' + $m.Groups['t'].Value.Trim(); Accept = 'application/vnd.github+json' }

git push origin $Ref
if ($LASTEXITCODE -ne 0) { throw "git push failed: $LASTEXITCODE" }

$body = '{"ref":"' + $Ref + '"}'
Invoke-WebRequest -Method Post -ContentType 'application/json' -Body $body `
    -Uri "https://api.github.com/repos/$repo/actions/workflows/$workflow/dispatches" -Headers $headers | Out-Null

Start-Sleep -Seconds 5
$runs = Invoke-RestMethod "https://api.github.com/repos/$repo/actions/runs?per_page=3" -Headers $headers
$runs.workflow_runs | Select-Object -First 3 id, head_sha, status, conclusion, html_url | Format-Table -AutoSize
