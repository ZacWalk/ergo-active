<#
.SYNOPSIS
    Dev driver for ergo-active.

.EXAMPLE
    .\dd.ps1 run      # build Release and launch it
    .\dd.ps1 build    # build Release only
#>
[CmdletBinding()]
param(
    [Parameter(Position = 0)]
    [ValidateSet('run', 'build')]
    [string]$Command = 'run',

    [ValidateSet('x64', 'Win32')]
    [string]$Platform = 'x64',

    [ValidateSet('Release', 'Debug')]
    [string]$Configuration = 'Release'
)

$ErrorActionPreference = 'Stop'

$solution = Join-Path $PSScriptRoot 'ergo-active.sln'

$suffix = if ($Platform -eq 'x64') { '-64' } else { '-32' }
if ($Configuration -eq 'Debug') { $suffix += 'd' }
$exe = Join-Path $PSScriptRoot "Exe\ergo-active$suffix.exe"

function Find-MSBuild {
    $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
    if (Test-Path $vswhere) {
        $found = & $vswhere -latest -prerelease -products * `
            -requires Microsoft.Component.MSBuild `
            -find 'MSBuild\**\Bin\MSBuild.exe' | Select-Object -First 1
        if ($found) { return $found }
    }

    $fallback = Get-Command MSBuild.exe -ErrorAction SilentlyContinue
    if ($fallback) { return $fallback.Source }

    throw 'MSBuild.exe not found. Run from a Developer PowerShell, or install Visual Studio.'
}

# The linker cannot overwrite a running exe, and the single-instance mutex is shared
# across all build flavours, so a stray Debug/32-bit copy would swallow the new launch.
function Stop-App {
    $running = Get-Process -Name 'ergo-active-*' -ErrorAction SilentlyContinue
    if ($running) {
        Write-Host "Stopping running $(($running.ProcessName | Sort-Object -Unique) -join ', ')..." -ForegroundColor DarkGray
        $running | Stop-Process -Force
        $running | Wait-Process -Timeout 10 -ErrorAction SilentlyContinue
    }
}

function Invoke-Build {
    $msbuild = Find-MSBuild
    Write-Host "Building $Configuration|$Platform..." -ForegroundColor Cyan

    & $msbuild $solution /t:Build /p:Configuration=$Configuration /p:Platform=$Platform /m /nologo /v:minimal
    if ($LASTEXITCODE -ne 0) {
        throw "Build failed with exit code $LASTEXITCODE."
    }

    if (-not (Test-Path $exe)) {
        throw "Build reported success but $exe is missing."
    }
}

Stop-App
Invoke-Build

switch ($Command) {
    'build' {
        Write-Host "Built $exe" -ForegroundColor Green
    }
    'run' {
        Write-Host "Launching $exe" -ForegroundColor Green
        Start-Process -FilePath $exe
    }
}
