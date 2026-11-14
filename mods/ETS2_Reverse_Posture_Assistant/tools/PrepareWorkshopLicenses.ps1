# Validate and supplement the existing uploader content; do not regenerate game resources.
$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'LicensePackaging.ps1')
$root = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..')).TrimEnd('\','/')
$null = Get-LicensePackageFiles $root
foreach ($package in @('162_content','universal_info')) {
    Copy-LicenseFilesToDirectory $root (Join-Path $root ('workshop/' + $package))
}
Write-Output 'Verified licenses in ETS2 workshop/162_content and universal_info. No upload performed.'
