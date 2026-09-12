$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$source = Join-Path $root 'src\mod'
$destination = Join-Path $root 'workshop\160_content'
if (Test-Path -LiteralPath $destination) { Remove-Item -LiteralPath $destination -Recurse -Force }
Copy-Item -LiteralPath $source -Destination $destination -Recurse
$manifest = Join-Path $destination 'manifest.sii'
$lines = Get-Content -LiteralPath $manifest | Where-Object {
    $_ -notmatch '^\s*(display_name|compatible_versions\[\]):'
}
[IO.File]::WriteAllLines($manifest,$lines,[Text.UTF8Encoding]::new($false))
Write-Output $destination
