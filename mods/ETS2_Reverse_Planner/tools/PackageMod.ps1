$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.IO.Compression.FileSystem
$root = Split-Path -Parent $PSScriptRoot
$mod = Join-Path $root 'src\mod'
$release = Join-Path $root 'build\release'
New-Item -ItemType Directory -Path $release -Force | Out-Null
$archive = Join-Path $release 'ETS2_Reverse_Planner_v0.1.0-beta.scs'
if (Test-Path -LiteralPath $archive) { Remove-Item -LiteralPath $archive -Force }
$zip = [IO.Compression.ZipFile]::Open($archive,'Create')
try {
    Get-ChildItem -LiteralPath $mod -Recurse -File | ForEach-Object {
        $name = $_.FullName.Substring($mod.Length+1).Replace('\','/')
        [IO.Compression.ZipFileExtensions]::CreateEntryFromFile(
            $zip,$_.FullName,$name,'Optimal') | Out-Null
    }
} finally { $zip.Dispose() }
$zip = [IO.Compression.ZipFile]::OpenRead($archive)
try {
    foreach ($required in @('manifest.sii','description.txt','mod_icon.jpg',
            'model/reverse_planer/sweep_edge_gren.pmd',
            'model/reverse_planer/sweep_edge_redd.pmd')) {
        if (-not $zip.GetEntry($required)) { throw "Missing resource: $required" }
    }
    if ($zip.Entries.FullName -match '^model/symbol/') { throw 'Base-game symbol override found.' }
    Write-Output "Package verified: $($zip.Entries.Count) entries."
} finally { $zip.Dispose() }
Write-Output $archive
