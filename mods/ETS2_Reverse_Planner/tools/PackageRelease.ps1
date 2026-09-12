$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.IO.Compression.FileSystem
$root = Split-Path -Parent $PSScriptRoot
& (Join-Path $PSScriptRoot 'PackageMod.ps1') | Out-Host
$release = Join-Path $root 'build\release'
$runtime = Join-Path $root 'build\game_runtime\ETS2ReversePlannerRuntime.dll'
if (-not (Test-Path -LiteralPath $runtime)) { throw 'Build the native DLL first.' }

foreach ($variant in @('Full','Runtime_for_Workshop')) {
    $stage = Join-Path $release ('stage_'+$variant)
    if (Test-Path -LiteralPath $stage) { Remove-Item -LiteralPath $stage -Recurse -Force }
    New-Item -ItemType Directory -Path "$stage\runtime","$stage\installer" -Force | Out-Null
    Copy-Item -LiteralPath $runtime -Destination "$stage\runtime"
    Copy-Item -LiteralPath "$root\packaging\Install.ps1" -Destination "$stage\installer"
    Copy-Item -LiteralPath "$root\README.md","$root\RELEASE_NOTES.md","$root\LICENSE","$root\THIRD_PARTY_NOTICES.md" -Destination $stage
    Copy-Item -LiteralPath "$root\licenses" -Destination $stage -Recurse
    if ($variant -eq 'Runtime_for_Workshop') {
        Copy-Item -LiteralPath "$root\packaging\Install-Runtime-Only.bat" -Destination $stage
    } else {
        New-Item -ItemType Directory -Path "$stage\mod" -Force | Out-Null
        Copy-Item -LiteralPath "$release\ETS2_Reverse_Planner_v0.1.0-beta.scs" -Destination "$stage\mod\ETS2_Reverse_Planner_1.60.scs"
        Copy-Item -LiteralPath "$root\packaging\Install-Full.bat" -Destination $stage
    }
    $archive = Join-Path $release "ETS2_Reverse_Planner_v0.1.0-beta_$variant.zip"
    if (Test-Path -LiteralPath $archive) { Remove-Item -LiteralPath $archive -Force }
    [IO.Compression.ZipFile]::CreateFromDirectory($stage,$archive)
    $zip = [IO.Compression.ZipFile]::OpenRead($archive)
    try {
        $entry = $zip.GetEntry('runtime/ETS2ReversePlannerRuntime.dll')
        if (-not $entry) { throw 'Packaged DLL missing.' }
        $stream = $entry.Open(); $sha = [Security.Cryptography.SHA256]::Create()
        try { $hash=[BitConverter]::ToString($sha.ComputeHash($stream)).Replace('-','') }
        finally { $stream.Dispose(); $sha.Dispose() }
        if ($hash -ne (Get-FileHash -LiteralPath $runtime).Hash) { throw 'Packaged DLL mismatch.' }
        if ($variant -eq 'Full' -and -not $zip.GetEntry('mod/ETS2_Reverse_Planner_1.60.scs')) { throw 'Packaged mod missing.' }
        Write-Output "Verified $variant"
    } finally { $zip.Dispose() }
}

Get-ChildItem -LiteralPath $release -File | Where-Object Extension -in '.zip','.scs' | ForEach-Object {
    "{0}  {1}" -f (Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash.ToLowerInvariant(),$_.Name
} | Set-Content -LiteralPath "$release\SHA256SUMS.txt" -Encoding ascii
