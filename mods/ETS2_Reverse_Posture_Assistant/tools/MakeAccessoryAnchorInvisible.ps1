param(
    [string]$ModelRoot = (Join-Path $PSScriptRoot '..\src\mod\model\reverse_assist')
)

$ErrorActionPreference = 'Stop'
$path = Join-Path $ModelRoot 'accessory_anchor.pmd'
$visibleMaterial = '/automat/f5/f57370b76733c5aa.mat'
$invisibleMaterial = '/automat/40/408526e9278658d1.mat'

$bytes = [IO.File]::ReadAllBytes($path)
$visibleBytes = [Text.Encoding]::ASCII.GetBytes($visibleMaterial)
$invisibleBytes = [Text.Encoding]::ASCII.GetBytes($invisibleMaterial)

function Find-ByteSequence {
    param([byte[]]$Buffer, [byte[]]$Needle)
    for ($offset = 0; $offset -le $Buffer.Length - $Needle.Length; $offset++) {
        $matches = $true
        for ($index = 0; $index -lt $Needle.Length; $index++) {
            if ($Buffer[$offset + $index] -ne $Needle[$index]) {
                $matches = $false
                break
            }
        }
        if ($matches) { return $offset }
    }
    return -1
}

$materialOffset = Find-ByteSequence $bytes $visibleBytes
if ($materialOffset -lt 0) {
    throw "Expected visible material reference was not found in $path"
}
if ($visibleBytes.Length -ne $invisibleBytes.Length) {
    throw 'Material paths must have identical byte lengths.'
}

[Array]::Copy($invisibleBytes, 0, $bytes, $materialOffset, $invisibleBytes.Length)
[IO.File]::WriteAllBytes($path, $bytes)
Write-Output $path
