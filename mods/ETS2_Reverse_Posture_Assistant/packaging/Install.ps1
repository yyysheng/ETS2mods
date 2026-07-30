param(
    [switch]$RuntimeOnly
)

$ErrorActionPreference = 'Stop'
$packageRoot = Split-Path -Parent $PSScriptRoot

function Find-Ets2GameRoot {
    $candidates = [System.Collections.Generic.List[string]]::new()
    $installKeys = @(
        'HKLM:\SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall\Steam App 227300',
        'HKLM:\SOFTWARE\WOW6432Node\Microsoft\Windows\CurrentVersion\Uninstall\Steam App 227300',
        'HKCU:\SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall\Steam App 227300'
    )
    foreach ($key in $installKeys) {
        $location = (Get-ItemProperty -Path $key -ErrorAction SilentlyContinue).InstallLocation
        if ($location) { $candidates.Add($location) }
    }

    $steamPath = (Get-ItemProperty -Path 'HKCU:\Software\Valve\Steam' -ErrorAction SilentlyContinue).SteamPath
    if (-not $steamPath) { $steamPath = 'C:\Program Files (x86)\Steam' }
    $steamLibraries = [System.Collections.Generic.List[string]]::new()
    $steamLibraries.Add($steamPath)
    $libraryFile = Join-Path $steamPath 'steamapps\libraryfolders.vdf'
    if (Test-Path -LiteralPath $libraryFile) {
        foreach ($line in Get-Content -LiteralPath $libraryFile) {
            if ($line -match '"path"\s+"([^"]+)"') {
                $steamLibraries.Add($Matches[1].Replace('\\', '\'))
            }
        }
    }
    foreach ($library in $steamLibraries) {
        $candidates.Add((Join-Path $library 'steamapps\common\Euro Truck Simulator 2'))
    }

    foreach ($candidate in $candidates | Select-Object -Unique) {
        if (Test-Path -LiteralPath (Join-Path $candidate 'bin\win_x64\eurotrucks2.exe')) {
            return $candidate
        }
    }
    throw 'Euro Truck Simulator 2 was not found in the registered Steam libraries.'
}

if (Get-Process -Name eurotrucks2 -ErrorAction SilentlyContinue) {
    throw 'Euro Truck Simulator 2 is running. Exit the game before installing.'
}

$gameRoot = Find-Ets2GameRoot
$gameBin = Join-Path $gameRoot 'bin\win_x64'
$modDir = Join-Path ([Environment]::GetFolderPath('MyDocuments')) 'Euro Truck Simulator 2\mod'
$runtimeSource = Join-Path $packageRoot 'runtime\ETS2ReverseEntityRuntime.dll'
if (-not (Test-Path -LiteralPath $runtimeSource)) { throw "Runtime file is missing: $runtimeSource" }

New-Item -ItemType Directory -Path $modDir,(Join-Path $gameBin 'plugins') -Force | Out-Null

Copy-Item -LiteralPath $runtimeSource `
    -Destination (Join-Path $gameBin 'plugins') -Force

if (-not $RuntimeOnly) {
    $modSource = Join-Path $packageRoot 'mod\ETS2_Reverse_Posture_Assistant_1.60.scs'
    if (-not (Test-Path -LiteralPath $modSource)) { throw "Mod file is missing: $modSource" }
    Copy-Item -LiteralPath $modSource -Destination $modDir -Force
}

Write-Host ''
Write-Host 'ETS2 Reverse Posture Assistant v0.10.7 installed successfully.' -ForegroundColor Green
Write-Host "Game directory: $gameRoot"
Write-Host 'This build uses an SCS telemetry plug-in and native world entities.'
Write-Host 'dxgi.dll and d3d11.dll were not read, replaced, renamed, or removed.'
if (-not $RuntimeOnly) { Write-Host 'Enable the mod in the ETS2 Mod Manager before driving.' }
