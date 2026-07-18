param(
    [switch]$RuntimeOnly
)

$ErrorActionPreference = 'Stop'
$packageRoot = Split-Path -Parent $PSScriptRoot
$steamPath = (Get-ItemProperty -Path 'HKCU:\Software\Valve\Steam' -ErrorAction SilentlyContinue).SteamPath
if (-not $steamPath) { $steamPath = 'C:\Program Files (x86)\Steam' }
$gameRoot = Join-Path $steamPath 'steamapps\common\Euro Truck Simulator 2'
$gameBin = Join-Path $gameRoot 'bin\win_x64'
$modDir = Join-Path ([Environment]::GetFolderPath('MyDocuments')) 'Euro Truck Simulator 2\mod'

if (-not (Test-Path -LiteralPath (Join-Path $gameBin 'eurotrucks2.exe'))) {
    throw "Euro Truck Simulator 2 was not found at: $gameRoot"
}

New-Item -ItemType Directory -Path $modDir,(Join-Path $gameBin 'plugins') -Force | Out-Null

$runtimeFiles = @(
    'ETS2ReverseScreenRuntime.addon64',
    'ETS2ReverseEnvironment.exe',
    'ETS2TerrainIndex.bin',
    'custom_resources.zip',
    'TsMap.dll',
    'Newtonsoft.Json.dll',
    'libdeflate.dll'
)
foreach ($file in $runtimeFiles) {
    Copy-Item -LiteralPath (Join-Path $packageRoot "runtime\$file") -Destination $gameBin -Force
}
Copy-Item -LiteralPath (Join-Path $packageRoot 'runtime\scs-telemetry.dll') -Destination (Join-Path $gameBin 'plugins') -Force

$reshadeTarget = Join-Path $gameBin 'dxgi.dll'
if (-not (Test-Path -LiteralPath $reshadeTarget)) {
    Copy-Item -LiteralPath (Join-Path $packageRoot 'runtime\dxgi.dll') -Destination $reshadeTarget
} else {
    Write-Host 'Existing ReShade installation detected; dxgi.dll was left unchanged.' -ForegroundColor Yellow
}

if (-not $RuntimeOnly) {
    Copy-Item -LiteralPath (Join-Path $packageRoot 'mod\ETS2_Reverse_Posture_Assistant_1.60.scs') -Destination $modDir -Force
}

Write-Host ''
Write-Host 'ETS2 Reverse Posture Assistant installed successfully.' -ForegroundColor Green
Write-Host 'ReShade 6.7.3 with add-on support is required.'
if (-not $RuntimeOnly) { Write-Host 'Enable the mod in the ETS2 Mod Manager before driving.' }
