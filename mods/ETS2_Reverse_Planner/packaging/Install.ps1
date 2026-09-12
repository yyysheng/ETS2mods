param([switch]$RuntimeOnly)

$ErrorActionPreference = 'Stop'
$packageRoot = Split-Path -Parent $PSScriptRoot

if (Get-Process -Name eurotrucks2 -ErrorAction SilentlyContinue) {
    throw 'Euro Truck Simulator 2 is running. Exit the game before installing.'
}

$steamPath = (Get-ItemProperty -Path 'HKCU:\Software\Valve\Steam' -ErrorAction SilentlyContinue).SteamPath
if (-not $steamPath) { $steamPath = 'C:\Program Files (x86)\Steam' }
$libraries = [Collections.Generic.List[string]]::new()
$libraries.Add($steamPath)
$libraryFile = Join-Path $steamPath 'steamapps\libraryfolders.vdf'
if (Test-Path -LiteralPath $libraryFile) {
    foreach ($line in Get-Content -LiteralPath $libraryFile) {
        if ($line -match '"path"\s+"([^"]+)"') { $libraries.Add($Matches[1].Replace('\\','\')) }
    }
}
$gameRoot = $null
foreach ($library in $libraries | Select-Object -Unique) {
    $candidate = Join-Path $library 'steamapps\common\Euro Truck Simulator 2'
    if (Test-Path -LiteralPath (Join-Path $candidate 'bin\win_x64\eurotrucks2.exe')) {
        $gameRoot = $candidate; break
    }
}
if (-not $gameRoot) { throw 'Euro Truck Simulator 2 was not found in the registered Steam libraries.' }

$pluginDir = Join-Path $gameRoot 'bin\win_x64\plugins'
$modDir = Join-Path ([Environment]::GetFolderPath('MyDocuments')) 'Euro Truck Simulator 2\mod'
$runtimeSource = Join-Path $packageRoot 'runtime\ETS2ReversePlannerRuntime.dll'
if (-not (Test-Path -LiteralPath $runtimeSource)) { throw "Runtime file is missing: $runtimeSource" }
New-Item -ItemType Directory -Path $pluginDir,$modDir -Force | Out-Null
Copy-Item -LiteralPath $runtimeSource -Destination $pluginDir -Force

if (-not $RuntimeOnly) {
    $modSource = Join-Path $packageRoot 'mod\ETS2_Reverse_Planner_1.60.scs'
    if (-not (Test-Path -LiteralPath $modSource)) { throw "Mod file is missing: $modSource" }
    Copy-Item -LiteralPath $modSource -Destination $modDir -Force
}

Write-Host ''
Write-Host 'ReversePlanner v0.1.0-beta installed successfully.' -ForegroundColor Green
Write-Host "Game directory: $gameRoot"
Write-Host 'No dxgi.dll or d3d11.dll was installed, replaced or removed.'
if (-not $RuntimeOnly) { Write-Host 'Enable ReversePlanner (beta) For 1.60.x in the ETS2 Mod Manager.' }
