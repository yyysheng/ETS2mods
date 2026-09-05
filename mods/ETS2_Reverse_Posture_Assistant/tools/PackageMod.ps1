$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.IO.Compression.FileSystem
$root = Split-Path -Parent $PSScriptRoot
$mod = Join-Path $root 'src/mod'
# Reject stale converted test assets before they can shadow the base game.
if (Get-ChildItem -LiteralPath (Join-Path $mod 'model') -Recurse -File |
    Where-Object { $_.FullName -match '[\\/]symbol[\\/]' }) {
    throw 'Base-game symbol overrides are forbidden in the release package.'
}
$release = Join-Path $root 'build/release'
New-Item -ItemType Directory -Path $release -Force | Out-Null
$archive = Join-Path $release 'ETS2_Reverse_Posture_Assistant_v0.10.9.scs'
if (Test-Path -LiteralPath $archive) { Remove-Item -LiteralPath $archive }
# Create archive names with forward slashes for the game resource filesystem.
$zip = [IO.Compression.ZipFile]::Open($archive, 'Create')
try {
    Get-ChildItem -LiteralPath $mod -Recurse -File | ForEach-Object {
        $name = $_.FullName.Substring($mod.Length + 1).Replace('\', '/')
        [IO.Compression.ZipFileExtensions]::CreateEntryFromFile(
            $zip, $_.FullName, $name, 'Optimal') | Out-Null
    }
} finally { $zip.Dispose() }
$zip = [IO.Compression.ZipFile]::OpenRead($archive)
try {
    foreach ($required in @('manifest.sii', 'model/reverse_assist/accessory_anchor.pmd',
            'model/reverse_assist/sweep_edge.pmd', 'model/reverse_assist/sweep_edge_blue.pmd')) {
        if (-not $zip.GetEntry($required)) { throw "Missing resource: $required" }
    }
    if ($zip.Entries.FullName -match '^model/symbol/') { throw 'Native symbol override found' }
    Write-Output "Package verified: $($zip.Entries.Count) entries; native symbols supplied by base game."
} finally { $zip.Dispose() }
Write-Output $archive
