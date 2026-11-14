$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'LicensePackaging.ps1')
$licenseProjectRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..')).TrimEnd('\','/')
$null = Get-LicensePackageFiles $licenseProjectRoot
$root = Split-Path -Parent $PSScriptRoot
$source = Join-Path $root 'src\mod'
$destination = Join-Path $root 'workshop\160_content'
if ([IO.Path]::GetFullPath($destination) -ne [IO.Path]::GetFullPath((Join-Path $root 'workshop\160_content'))) { throw 'Unsafe Workshop destination.' }
if ((Test-Path -LiteralPath $destination) -and ((Get-Item -LiteralPath $destination).Attributes -band [IO.FileAttributes]::ReparsePoint)) { throw 'Workshop destination must not be a reparse point.' }
if (Test-Path -LiteralPath $destination) { Remove-Item -LiteralPath $destination -Recurse -Force }
Copy-Item -LiteralPath $source -Destination $destination -Recurse
$manifest = Join-Path $destination 'manifest.sii'
$lines = Get-Content -LiteralPath $manifest | Where-Object {
    $_ -notmatch '^\s*(display_name|compatible_versions\[\]):'
}
[IO.File]::WriteAllLines($manifest,$lines,[Text.UTF8Encoding]::new($false))
Write-Output $destination

foreach ($package in @('160_content', 'universal_info')) {
    Copy-LicenseFilesToDirectory $licenseProjectRoot (Join-Path $licenseProjectRoot ('workshop/' + $package))
}
