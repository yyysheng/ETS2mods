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
    throw 'Euro Truck Simulator 2 is running. Exit the game before upgrading.'
}
Get-Process -Name ETS2ReverseEnvironment -ErrorAction SilentlyContinue | Stop-Process -Force

$gameRoot = Find-Ets2GameRoot
$gameBin = Join-Path $gameRoot 'bin\win_x64'
$pluginDir = Join-Path $gameBin 'plugins'
$modDir = Join-Path ([Environment]::GetFolderPath('MyDocuments')) 'Euro Truck Simulator 2\mod'
$runtimeSource = Join-Path $packageRoot 'runtime\ETS2ReverseEntityRuntime.dll'
$modSource = Join-Path $packageRoot 'mod\ETS2_Reverse_Posture_Assistant_1.60.scs'
if (-not (Test-Path -LiteralPath $runtimeSource)) { throw "Runtime file is missing: $runtimeSource" }
if (-not (Test-Path -LiteralPath $modSource)) { throw "Mod file is missing: $modSource" }

$backupDir = Join-Path $gameBin ('reverse_assist_backup\v0.6.0_' + (Get-Date -Format 'yyyyMMdd-HHmmss'))
New-Item -ItemType Directory -Path $backupDir,$pluginDir,$modDir -Force | Out-Null

$legacyFiles = @(
    (Join-Path $gameBin 'ETS2ReverseScreenRuntime.addon64'),
    (Join-Path $gameBin 'ETS2ReverseEnvironment.exe'),
    (Join-Path $gameBin 'ETS2TerrainIndex.bin'),
    (Join-Path $gameBin 'custom_resources.zip'),
    (Join-Path $gameBin 'ETS2ReverseGroundGuideProbe.addon64'),
    (Join-Path $pluginDir 'scs-telemetry.dll')
)
foreach ($legacyFile in $legacyFiles) {
    if (Test-Path -LiteralPath $legacyFile) {
        Move-Item -LiteralPath $legacyFile -Destination $backupDir -Force
    }
}

Copy-Item -LiteralPath $runtimeSource -Destination $pluginDir -Force
Copy-Item -LiteralPath $modSource -Destination $modDir -Force

Write-Host ''
Write-Host 'ETS2 Reverse Posture Assistant upgraded from v0.6.0 to v0.10.7.' -ForegroundColor Green
Write-Host "Legacy Reverse Assistant components were backed up to: $backupDir"
Write-Host 'dxgi.dll, d3d11.dll, TsMap.dll, Newtonsoft.Json.dll, and libdeflate.dll were left untouched.'
Write-Host 'This prevents the upgrade from damaging ReShade, Snowymoon, or another mod using those files.'
Write-Host 'Enable or refresh ETS2 Reverse Posture Assistant in the Mod Manager before driving.'
