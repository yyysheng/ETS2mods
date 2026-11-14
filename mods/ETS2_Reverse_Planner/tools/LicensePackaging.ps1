# Packaging only. Does not alter runtime or game resources.
Add-Type -AssemblyName System.IO.Compression.FileSystem
function Get-LicensePackageFiles {
    param([Parameter(Mandatory=$true)][string]$ProjectRoot)
    $manifest = Join-Path $ProjectRoot 'LICENSE_PACKAGE.json'
    $spec = Get-Content -LiteralPath $manifest -Raw | ConvertFrom-Json
    $fixed = @('LICENSE','THIRD_PARTY_NOTICES.md','licenses/LICENSE.scs-sdk.txt','licenses/LICENSE.scs-telemetry.txt','licenses/LICENSE.minhook.txt','licenses/LICENSE.historical-MIT.txt')
    if ((Split-Path $ProjectRoot -Leaf) -eq 'ETS2_Reverse_Planner') { $fixed += 'licenses/LICENSE.ets2la-plugin.txt' }
    if (@($spec.files).Count -ne $fixed.Count) { throw 'Public license manifest differs from reviewed allowlist.' }
    foreach ($required in $fixed) { if ($required -cnotin @($spec.files)) { throw "License manifest omits required file: $required" } }
    if ($spec.license_mode -ne 'historical-MIT-code') { throw 'Unapproved license mode.' }
    $expectedVersions = @{ 'ETS2_Reverse_Posture_Assistant' = '1.62.3.3'; 'ATS_Reverse_Trajectory_Predictor' = '0.11.3'; 'ETS2_Reverse_Planner' = '0.1.0-beta' }
    $expectedVersion = $expectedVersions[(Split-Path $ProjectRoot -Leaf)]
    if (-not $expectedVersion -or $spec.release_version -cne $expectedVersion) { throw 'Release licensing profile is not approved for this version.' }
    $modManifest = Get-Content -LiteralPath (Join-Path $ProjectRoot 'src/mod/manifest.sii') -Raw
    $versions = [regex]::Matches($modManifest, 'package_version\s*:\s*"([^"]+)"')
    if ($versions.Count -ne 1 -or $versions[0].Groups[1].Value -cne $expectedVersion) { throw 'Mod version does not match the effective licensing profile.' }
    $historical = Join-Path $ProjectRoot 'licenses/LICENSE.historical-MIT.txt'
    if ($spec.historical_mit_hash_rule -cne 'sha256:utf8-crlf-to-lf-only-v1') { throw 'Unsupported historical license hash rule.' }
    $utf8 = [Text.UTF8Encoding]::new($false, $true)
    $text = $utf8.GetString([IO.File]::ReadAllBytes($historical)).Replace("`r`n", "`n")
    $sha = [Security.Cryptography.SHA256]::Create()
    try { $actual = [BitConverter]::ToString($sha.ComputeHash($utf8.GetBytes($text))).Replace('-', '').ToLowerInvariant() }
    finally { $sha.Dispose() }
    if ($actual -cne $spec.historical_mit_sha256_lf) { throw 'Historical MIT text integrity mismatch (LF canonical SHA-256).' }
    if ($spec.proprietary_active -ne $false) { throw 'Future proprietary activation requires a reviewed release profile.' }
    $paths = @($spec.files)
    if (@($paths | Select-Object -Unique).Count -ne $paths.Count) { throw 'Duplicate license manifest path.' }
    foreach ($relative in $paths) {
        if ($relative -notmatch '^[A-Za-z0-9_./-]+$' -or $relative -match '(^/|(^|/)\.\.(/|$))') { throw "Unsafe license path: $relative" }
        $p = Join-Path $ProjectRoot $relative
        if (-not (Test-Path -LiteralPath $p -PathType Leaf) -or (Get-Item -LiteralPath $p).Length -eq 0) { throw "Missing or empty license file: $relative" }
    }
    return $paths
}
function Assert-LicenseArchive {
    param([Parameter(Mandatory=$true)][string]$ProjectRoot,[Parameter(Mandatory=$true)][string]$Archive)
    $paths = @(Get-LicensePackageFiles -ProjectRoot $ProjectRoot)
    $zip = [IO.Compression.ZipFile]::OpenRead($Archive)
    try {
        Assert-NoInternalLicenseFiles @($zip.Entries.FullName)
        foreach ($relative in $paths) {
            $entries = @($zip.Entries | Where-Object { $_.FullName.Replace('\','/') -ceq $relative })
            if ($entries.Count -ne 1) { throw "Missing or duplicate packaged license: $relative" }
            $stream = $entries[0].Open(); $sha = [Security.Cryptography.SHA256]::Create()
            try { $actual = [BitConverter]::ToString($sha.ComputeHash($stream)).Replace('-','') }
            finally { $sha.Dispose(); $stream.Dispose() }
            if ($actual -ne (Get-FileHash -LiteralPath (Join-Path $ProjectRoot $relative) -Algorithm SHA256).Hash) { throw "Packaged license mismatch: $relative" }
        }
    } finally { $zip.Dispose() }
}
function Add-LicenseFilesToArchive {
    param([Parameter(Mandatory=$true)][string]$ProjectRoot,[Parameter(Mandatory=$true)][string]$Archive)
    $paths = @(Get-LicensePackageFiles -ProjectRoot $ProjectRoot)
    $zip = [IO.Compression.ZipFile]::Open($Archive,[IO.Compression.ZipArchiveMode]::Update)
    try {
        Assert-NoInternalLicenseFiles @($zip.Entries.FullName)
        foreach ($relative in $paths) {
            $existing = @($zip.Entries | Where-Object { $_.FullName.Replace('\','/') -ceq $relative })
            if ($existing.Count -gt 1) { throw "Duplicate packaged license: $relative" }
            foreach ($e in $existing) { $e.Delete() }
            [IO.Compression.ZipFileExtensions]::CreateEntryFromFile($zip,(Join-Path $ProjectRoot $relative),$relative,[IO.Compression.CompressionLevel]::Optimal) | Out-Null
        }
    } finally { $zip.Dispose() }
    Assert-LicenseArchive -ProjectRoot $ProjectRoot -Archive $Archive
}

function Assert-NoInternalLicenseFiles {
    param([string[]]$Names)
    foreach ($name in $Names) {
        if ([string]::IsNullOrEmpty($name)) { continue }
        $normalized = $name.Replace('\','/')
        $leaf = ($normalized -split '/')[-1]
        if ($leaf -in @('FILE_LICENSES.csv','ASSET_OWNERSHIP.csv','LICENSE_ACTIVATION.json','LICENSE_PACKAGE.json','LICENSE_SCOPE.md','LICENSE_HISTORY.md','LICENSE.proprietary-code.txt','LICENSE.original-assets.txt','FUTURE_LICENSING_PLAN.md','DEPENDENCY_PROVENANCE.md','FUTURE_LICENSING_POLICY.md')) {
            throw "Internal licensing file leaked into public content: $name"
        }
    }
}
function Assert-LicenseDirectory {
    param([Parameter(Mandatory=$true)][string]$ProjectRoot,[Parameter(Mandatory=$true)][string]$Directory)
    $paths = @(Get-LicensePackageFiles $ProjectRoot)
    Assert-NoInternalLicenseFiles @(Get-ChildItem -LiteralPath $Directory -Recurse -File | ForEach-Object { $_.FullName })
    foreach ($relative in $paths) {
        $target = Join-Path $Directory $relative
        if (-not (Test-Path -LiteralPath $target -PathType Leaf)) { throw "Missing Workshop license: $relative" }
        if ((Get-FileHash -LiteralPath $target -Algorithm SHA256).Hash -ne (Get-FileHash -LiteralPath (Join-Path $ProjectRoot $relative) -Algorithm SHA256).Hash) { throw "Workshop license mismatch: $relative" }
    }
}
function Copy-LicenseFilesToDirectory {
    param([Parameter(Mandatory=$true)][string]$ProjectRoot,[Parameter(Mandatory=$true)][string]$Directory)
    $paths = @(Get-LicensePackageFiles $ProjectRoot)
    if (-not (Test-Path -LiteralPath $Directory -PathType Container)) { throw "Workshop directory missing: $Directory" }
    Assert-NoInternalLicenseFiles @(Get-ChildItem -LiteralPath $Directory -Recurse -File | ForEach-Object { $_.FullName })
    foreach ($relative in $paths) {
        $target = Join-Path $Directory $relative
        New-Item -ItemType Directory -Path (Split-Path $target -Parent) -Force | Out-Null
        Copy-Item -LiteralPath (Join-Path $ProjectRoot $relative) -Destination $target -Force
    }
    Assert-LicenseDirectory $ProjectRoot $Directory
}
