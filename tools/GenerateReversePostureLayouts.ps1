param(
    [string]$SourceRoot = 'D:\ETS2ReverseDashboardSources160',
    [string]$ModRoot = (Join-Path $PSScriptRoot '..\src\mod')
)

$targets = @(
    'base_vehicle\ui\dashboard\renault_t_2024_gps.sii',
    'base_vehicle\ui\dashboard\scania_2025_gps.sii',
    'dlc_daf_2021\ui\dashboard\daf_2021_gps.sii',
    'dlc_daf_xd\ui\dashboard\daf_xd_gps.sii',
    'dlc_daf_xf_electric\ui\dashboard\daf_xf_electric_gps.sii',
    'dlc_daf_xf_electric\ui\dashboard\daf_xf_electric_uk_gps.sii',
    'dlc_iveco_sway\ui\dashboard\iveco_sway_ic10_gps.sii',
    'dlc_iveco_sway\ui\dashboard\iveco_sway_ic10_gps_mph.sii',
    'dlc_man_tgx_2020\ui\dashboard\man_tgx_2020_gps.sii',
    'dlc_renault_etech_t\ui\dashboard\renault_etech_t_gps.sii',
    'dlc_volvo_fh_2021\ui\dashboard\volvo_fh_2021_gps.sii',
    'dlc_volvo_fh_2021\ui\dashboard\volvo_fh_2021_mph_gps.sii',
    'dlc_volvo_fh_2024\ui\dashboard\volvo_fh_2024_gps.sii',
    'dlc_volvo_fh_2024\ui\dashboard\volvo_fh_2024_mph_gps.sii'
)

$dashboardRoot = Join-Path $ModRoot 'ui\dashboard'
New-Item -ItemType Directory -Path $dashboardRoot -Force | Out-Null
Get-ChildItem -LiteralPath $dashboardRoot -Filter '*.sii' | Remove-Item -Force

foreach ($relativePath in $targets) {
    $sourcePath = Join-Path $SourceRoot $relativePath
    if (-not (Test-Path -LiteralPath $sourcePath)) { throw "Missing dashboard source: $sourcePath" }

    $content = [IO.File]::ReadAllText($sourcePath)
    $windowMatch = [regex]::Match($content, 'ui::window\s*:\s*([^\s{]+)\s*\{')
    if (-not $windowMatch.Success) { throw "No root window in $sourcePath" }
    $windowName = $windowMatch.Groups[1].Value

    $blockStart = $windowMatch.Index
    $braceStart = $content.IndexOf('{', $blockStart)
    $depth = 0
    $blockEnd = -1
    for ($i = $braceStart; $i -lt $content.Length; $i++) {
        if ($content[$i] -eq '{') { $depth++ }
        elseif ($content[$i] -eq '}') {
            $depth--
            if ($depth -eq 0) { $blockEnd = $i; break }
        }
    }
    if ($blockEnd -lt 0) { throw "Unclosed root window in $sourcePath" }

    $windowBlock = $content.Substring($blockStart, $blockEnd - $blockStart + 1)
    $childCountMatch = [regex]::Match($windowBlock, '(?m)^\s*my_children:\s*(\d+)\s*$')
    if (-not $childCountMatch.Success) { throw "No child count in $sourcePath" }
    $childCount = [int]$childCountMatch.Groups[1].Value
    $newCountLine = $childCountMatch.Value -replace '\d+\s*$', ($childCount + 1).ToString()
    $windowBlock = $windowBlock.Remove($childCountMatch.Index, $childCountMatch.Length).Insert($childCountMatch.Index, $newCountLine)

    $childMatches = [regex]::Matches($windowBlock, '(?m)^(\s*)my_children\[(\d+)\]:[^\r\n]*$')
    if ($childMatches.Count -eq 0) { throw "No child entries in $sourcePath" }
    $lastChildMatch = $childMatches[$childMatches.Count - 1]
    $indent = $lastChildMatch.Groups[1].Value
    $insertAt = $lastChildMatch.Index + $lastChildMatch.Length
    $windowBlock = $windowBlock.Insert($insertAt, "`r`n${indent}my_children[$childCount]: _nameless._.revpose")
    $content = $content.Remove($blockStart, $blockEnd - $blockStart + 1).Insert($blockStart, $windowBlock)

    $coords = @{}
    foreach ($name in 'l','r','t','b') {
        $match = [regex]::Match($windowBlock, "(?m)^\s*coords_$name\s*:\s*(-?\d+)\s*$")
        if (-not $match.Success) { throw "Missing coords_$name in $sourcePath" }
        $coords[$name] = $match.Groups[1].Value
    }

    $overlay = @"

ui::text_common : _nameless._.revpose {
 value: "R"
 look_template: txt.revpose.gear_page
 text: ""
 coords_l: $($coords.l)
 coords_r: $($coords.r)
 coords_t: $($coords.t)
 coords_b: $($coords.b)
 area_l: 1
 area_r: 0
 area_t: 0
 area_b: 1
 id: 1380
 layer: 50
 tab: -1
 pointer: -1
 my_parent: $windowName
}

"@
    $finalBrace = $content.LastIndexOf('}')
    $content = $content.Insert($finalBrace, $overlay)

    $destination = Join-Path $dashboardRoot ([IO.Path]::GetFileName($sourcePath))
    [IO.File]::WriteAllText($destination, $content, [Text.UTF8Encoding]::new($false))
    Write-Output $destination
}
