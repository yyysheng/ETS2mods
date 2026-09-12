$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
& (Join-Path $PSScriptRoot 'PackageMod.ps1')
$release = Join-Path $root 'build/release'
$runtime = Join-Path $root 'build/entity_runtime/ETS2ReverseEntityRuntime.dll'
if (-not (Test-Path -LiteralPath $runtime)) { throw 'Build the native DLL first.' }
foreach ($variant in @('Full', 'Runtime_for_Workshop', 'Upgrade_from_v0.6.0')) {
    $stage = Join-Path $release ('stage_' + $variant)
    if (Test-Path -LiteralPath $stage) { throw "Staging directory already exists: $stage" }
    New-Item -ItemType Directory -Path $stage,"$stage/runtime","$stage/installer" | Out-Null
    Copy-Item -LiteralPath $runtime -Destination "$stage/runtime"
    Copy-Item -LiteralPath "$root/packaging/Install.ps1" -Destination "$stage/installer"
    Copy-Item -LiteralPath "$root/README.md","$root/RELEASE_NOTES_v0.10.10.md","$root/LICENSE","$root/THIRD_PARTY_NOTICES.md" -Destination $stage
    Copy-Item -LiteralPath "$root/licenses" -Destination $stage -Recurse
    if ($variant -eq 'Runtime_for_Workshop') {
        Copy-Item -LiteralPath "$root/packaging/Install-Runtime-Only.bat" -Destination $stage
    } else {
        New-Item -ItemType Directory -Path "$stage/mod" | Out-Null
        Copy-Item -LiteralPath "$release/ETS2_Reverse_Posture_Assistant_v0.10.10.scs" -Destination "$stage/mod/ETS2_Reverse_Posture_Assistant_1.60.scs"
        Copy-Item -LiteralPath "$root/packaging/Install-Full.bat" -Destination $stage
        if ($variant -eq 'Upgrade_from_v0.6.0') {
            Copy-Item -LiteralPath "$root/packaging/Upgrade-From-v0.6.0.bat" -Destination $stage
            Copy-Item -LiteralPath "$root/packaging/Upgrade-From-v0.6.0.ps1" -Destination "$stage/installer"
        }
    }
    $archive = Join-Path $release "ETS2_Reverse_Posture_Assistant_v0.10.10_$variant.zip"
    [IO.Compression.ZipFile]::CreateFromDirectory($stage, $archive)
    $zip = [IO.Compression.ZipFile]::OpenRead($archive)
    try {
        $entry = $zip.GetEntry('runtime/ETS2ReverseEntityRuntime.dll')
        if (-not $entry) { throw 'Packaged DLL missing' }
        $stream = $entry.Open()
        $sha = [Security.Cryptography.SHA256]::Create()
        try { $hash = [BitConverter]::ToString($sha.ComputeHash($stream)).Replace('-', '') }
        finally { $stream.Dispose(); $sha.Dispose() }
        if ($hash -ne (Get-FileHash -LiteralPath $runtime).Hash) { throw 'Packaged DLL mismatch' }
        if ($variant -ne 'Runtime_for_Workshop' -and -not $zip.GetEntry('mod/ETS2_Reverse_Posture_Assistant_1.60.scs')) { throw 'Packaged mod missing' }
        Write-Output "Verified $variant"
    } finally { $zip.Dispose() }
}
Get-ChildItem -LiteralPath $release -Filter '*.zip' | ForEach-Object {
    $hash = (Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash.ToLowerInvariant()
    "$hash  $($_.Name)"
} | Set-Content -LiteralPath "$release/SHA256SUMS.txt" -Encoding ascii
