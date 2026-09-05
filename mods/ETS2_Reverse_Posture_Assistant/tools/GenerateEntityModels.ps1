param(
    [string]$OutputRoot = (Join-Path $PSScriptRoot '..\build\entity_models\base')
)

$ErrorActionPreference = 'Stop'
$culture = [Globalization.CultureInfo]::InvariantCulture
$modelDir = Join-Path $OutputRoot 'model\reverse_assist'
$null = New-Item -ItemType Directory -Path $modelDir -Force

function F([double]$value) {
    $bits = [BitConverter]::ToUInt32([BitConverter]::GetBytes([single]$value), 0)
    return '&' + $bits.ToString('x8', $culture)
}

function Add-Cuboid {
    param(
        [Collections.Generic.List[object]]$Vertices,
        [Collections.Generic.List[object]]$Triangles,
        [double]$MinX, [double]$MaxX,
        [double]$MinY, [double]$MaxY,
        [double]$MinZ, [double]$MaxZ
    )
    $faces = @(
        @(@($MinX,$MinY,$MinZ),@($MaxX,$MinY,$MinZ),@($MaxX,$MaxY,$MinZ),@($MinX,$MaxY,$MinZ),@(0,0,-1)),
        @(@($MaxX,$MinY,$MaxZ),@($MinX,$MinY,$MaxZ),@($MinX,$MaxY,$MaxZ),@($MaxX,$MaxY,$MaxZ),@(0,0,1)),
        @(@($MinX,$MinY,$MaxZ),@($MinX,$MinY,$MinZ),@($MinX,$MaxY,$MinZ),@($MinX,$MaxY,$MaxZ),@(-1,0,0)),
        @(@($MaxX,$MinY,$MinZ),@($MaxX,$MinY,$MaxZ),@($MaxX,$MaxY,$MaxZ),@($MaxX,$MaxY,$MinZ),@(1,0,0)),
        @(@($MinX,$MaxY,$MinZ),@($MaxX,$MaxY,$MinZ),@($MaxX,$MaxY,$MaxZ),@($MinX,$MaxY,$MaxZ),@(0,1,0)),
        @(@($MinX,$MinY,$MaxZ),@($MaxX,$MinY,$MaxZ),@($MaxX,$MinY,$MinZ),@($MinX,$MinY,$MinZ),@(0,-1,0))
    )
    foreach ($face in $faces) {
        $start = $Vertices.Count
        for ($i = 0; $i -lt 4; $i++) {
            $Vertices.Add([pscustomobject]@{
                Position = $face[$i]
                Normal = $face[4]
                UV = @(@(0,0),@(1,0),@(1,1),@(0,1))[$i]
            })
        }
        $Triangles.Add(@($start, ($start + 1), ($start + 2)))
        $Triangles.Add(@($start, ($start + 2), ($start + 3)))
    }
}

function Write-FrameModel(
    [string]$Name,
    [double]$Length,
    [double]$Width,
    [double]$OffsetX = 0.0,
    [double]$OffsetY = 0.0,
    [double]$OffsetZ = 0.0,
    [bool]$ReverseLamp = $false,
    [string]$RelativeModelDirectory = 'model\reverse_assist',
    [bool]$SingleCenterLine = $false,
    [string]$TextureName = 'guide_color',
    [string]$DiffuseColor = '1.000000 0.500000 0.050000'
) {
    $targetModelDir = Join-Path $OutputRoot $RelativeModelDirectory
    $null = New-Item -ItemType Directory -Path $targetModelDir -Force
    $vertices = [Collections.Generic.List[object]]::new()
    $triangles = [Collections.Generic.List[object]]::new()
    $locators = [Collections.Generic.List[object]]::new()
    $halfL = $Length / 2
    $halfW = $Width / 2
    $bar = 0.075
    $height = 0.055
    if ($SingleCenterLine) {
        Add-Cuboid $vertices $triangles ($OffsetX - $bar / 2) ($OffsetX + $bar / 2) $OffsetY ($OffsetY + $height) ($OffsetZ - $halfL) ($OffsetZ + $halfL)
    } else {
        Add-Cuboid $vertices $triangles ($OffsetX - $halfW) ($OffsetX + $halfW) $OffsetY ($OffsetY + $height) ($OffsetZ - $halfL) ($OffsetZ - $halfL + $bar)
        Add-Cuboid $vertices $triangles ($OffsetX - $halfW) ($OffsetX + $halfW) $OffsetY ($OffsetY + $height) ($OffsetZ + $halfL - $bar) ($OffsetZ + $halfL)
        Add-Cuboid $vertices $triangles ($OffsetX - $halfW) ($OffsetX - $halfW + $bar) $OffsetY ($OffsetY + $height) ($OffsetZ - $halfL + $bar) ($OffsetZ + $halfL - $bar)
        Add-Cuboid $vertices $triangles ($OffsetX + $halfW - $bar) ($OffsetX + $halfW) $OffsetY ($OffsetY + $height) ($OffsetZ - $halfL + $bar) ($OffsetZ + $halfL - $bar)
    }

    if ($ReverseLamp) {
        # The solid frame is only an invisible attachment carrier. Reverse-only,
        # no-light-source flare hookups form the visible dotted outline.
        for ($z = -$halfL; $z -le $halfL + 0.001; $z += 0.5) {
            $locators.Add(@(($OffsetX - $halfW), ($OffsetY + $height), ($OffsetZ + $z)))
            $locators.Add(@(($OffsetX + $halfW), ($OffsetY + $height), ($OffsetZ + $z)))
        }
        for ($x = -$halfW + 0.5; $x -lt $halfW - 0.001; $x += 0.5) {
            $locators.Add(@(($OffsetX + $x), ($OffsetY + $height), ($OffsetZ - $halfL)))
            $locators.Add(@(($OffsetX + $x), ($OffsetY + $height), ($OffsetZ + $halfL)))
        }
    }

    $lines = [Collections.Generic.List[string]]::new()
    $effect = if ($ReverseLamp) { 'eut2.none' } else { 'eut2.dif.lum' }
    $lines.Add("Header {`n    FormatVersion: 5`n    Source: `"Reverse Entity Model Generator`"`n    Type: `"Model`"`n    Name: `"$Name`"`n}")
    $lines.Add("Global {`n    VertexCount: $($vertices.Count)`n    TriangleCount: $($triangles.Count)`n    MaterialCount: 1`n    PieceCount: 1`n    PartCount: 1`n    BoneCount: 0`n    LocatorCount: $($locators.Count)`n    Skeleton: `"$Name.pis`"`n}")
    $lines.Add("Material {`n    Alias: `"guide_glow`"`n    Effect: `"$effect`"`n}")
    $lines.Add("Piece {`n    Index: 0`n    Material: 0`n    VertexCount: $($vertices.Count)`n    TriangleCount: $($triangles.Count)`n    StreamCount: 4")
    $lines.Add("    Stream {`n        Format: FLOAT3`n        Tag: `"_POSITION`"")
    for ($i = 0; $i -lt $vertices.Count; $i++) {
        $p = $vertices[$i].Position
        $lines.Add("        $i ( $(F $p[0]) $(F $p[1]) $(F $p[2]) )")
    }
    $lines.Add("    }")
    $lines.Add("    Stream {`n        Format: FLOAT3`n        Tag: `"_NORMAL`"")
    for ($i = 0; $i -lt $vertices.Count; $i++) {
        $n = $vertices[$i].Normal
        $lines.Add("        $i ( $(F $n[0]) $(F $n[1]) $(F $n[2]) )")
    }
    $lines.Add("    }")
    $uvAliases = "        AliasCount: 1`n        Aliases: `"_TEXCOORD0`""
    $lines.Add("    Stream {`n        Format: FLOAT2`n        Tag: `"_UV0`"`n$uvAliases")
    for ($i = 0; $i -lt $vertices.Count; $i++) {
        $uv = $vertices[$i].UV
        $lines.Add("        $i ( $(F $uv[0]) $(F $uv[1]) )")
    }
    $lines.Add("    }")
    $lines.Add("    Stream {`n        Format: FLOAT4`n        Tag: `"_RGBA`"")
    for ($i = 0; $i -lt $vertices.Count; $i++) {
        $lines.Add("        $i ( $(F 1) $(F 1) $(F 1) $(F 1) )")
    }
    $lines.Add("    }")
    $lines.Add("    Triangles {")
    for ($i = 0; $i -lt $triangles.Count; $i++) {
        $t = $triangles[$i]
        $lines.Add("        $i ( $($t[0]) $($t[1]) $($t[2]) )")
    }
    $lines.Add("    }`n}")
    $locatorIndices = if ($locators.Count -gt 0) { (0..($locators.Count - 1)) -join ' ' } else { '' }
    $lines.Add("Part {`n    Name: `"defaultpart`"`n    PieceCount: 1`n    LocatorCount: $($locators.Count)`n    Pieces: 0`n    Locators: $locatorIndices`n}")
    for ($i = 0; $i -lt $locators.Count; $i++) {
        $p = $locators[$i]
        $lines.Add("Locator {`n    Name: `"ra$i`"`n    Hookup: `"flare.revassist`"`n    Index: $i`n    Position: ( $(F $p[0]) $(F $p[1]) $(F $p[2]) )`n    Rotation: ( $(F 0) $(F 0) $(F 0) $(F 1) )`n    Scale: ( $(F 1) $(F 1) $(F 1) )`n}")
    }
    [IO.File]::WriteAllLines((Join-Path $targetModelDir "$Name.pim"), $lines)

    if ($ReverseLamp) {
        $pit = @"
Header {
    FormatVersion: 1
    Source: "Reverse Entity Model Generator"
    Type: "Trait"
    Name: "$Name"
}
Global {
    LookCount: 1
    VariantCount: 1
    PartCount: 1
    MaterialCount: 1
}
Look {
    Name: "default"
    Material {
        Alias: "guide_glow"
        Effect: "eut2.none"
        Flags: 0
        AttributeCount: 0
        TextureCount: 0
    }
}
Variant {
    Name: "default"
    Part {
        Name: "defaultpart"
        AttributeCount: 1
        Attribute { Format: INT Tag: "visible" Value: ( 1 ) }
    }
}
"@
    } else {
        $pit = @"
Header {
    FormatVersion: 1
    Source: "Reverse Entity Model Generator"
    Type: "Trait"
    Name: "$Name"
}
Global {
    LookCount: 1
    VariantCount: 1
    PartCount: 1
    MaterialCount: 1
}
Look {
    Name: "default"
    Material {
        Alias: "guide_glow"
        Effect: "eut2.dif.lum"
        Flags: 0
        AttributeCount: 6
        TextureCount: 1
        Attribute { Format: FLOAT Tag: "add_ambient" Value: ( 1.000000 ) }
        Attribute { Format: FLOAT2 Tag: "aux[5]" Value: ( 600.000000 4.000000 ) }
        Attribute { Format: FLOAT3 Tag: "diffuse" Value: ( $DiffuseColor ) }
        Attribute { Format: FLOAT Tag: "reflection" Value: ( 0.000000 ) }
        Attribute { Format: FLOAT Tag: "shininess" Value: ( 8.000000 ) }
        Attribute { Format: FLOAT3 Tag: "specular" Value: ( 0.000000 0.000000 0.000000 ) }
        Texture { Tag: "texture[0]:texture_base" Value: "/model/reverse_assist/$TextureName" }
    }
}
Variant {
    Name: "default"
    Part {
        Name: "defaultpart"
        AttributeCount: 1
        Attribute { Format: INT Tag: "visible" Value: ( 1 ) }
    }
}
"@
    }
    [IO.File]::WriteAllText((Join-Path $targetModelDir "$Name.pit"), $pit)
}

function Write-GuideTexture {
    $ddsPath = Join-Path $modelDir 'guide_color.dds'
    $header = [byte[]]::new(128)
    [Text.Encoding]::ASCII.GetBytes('DDS ').CopyTo($header, 0)
    [BitConverter]::GetBytes([uint32]124).CopyTo($header, 4)
    [BitConverter]::GetBytes([uint32]0x0002100F).CopyTo($header, 8)
    [BitConverter]::GetBytes([uint32]4).CopyTo($header, 12)
    [BitConverter]::GetBytes([uint32]4).CopyTo($header, 16)
    [BitConverter]::GetBytes([uint32]16).CopyTo($header, 20)
    [BitConverter]::GetBytes([uint32]32).CopyTo($header, 76)
    [BitConverter]::GetBytes([uint32]0x41).CopyTo($header, 80)
    [BitConverter]::GetBytes([uint32]32).CopyTo($header, 88)
    [BitConverter]::GetBytes([uint32]0x00FF0000).CopyTo($header, 92)
    [BitConverter]::GetBytes([uint32]0x0000FF00).CopyTo($header, 96)
    [BitConverter]::GetBytes([uint32]0x000000FF).CopyTo($header, 100)
    [BitConverter]::GetBytes([uint32]4278190080).CopyTo($header, 104)
    [BitConverter]::GetBytes([uint32]0x1000).CopyTo($header, 108)
    $pixels = [byte[]]::new(64)
    for ($i = 0; $i -lt 16; $i++) {
        $pixels[$i * 4] = 16
        $pixels[$i * 4 + 1] = 128
        $pixels[$i * 4 + 2] = 255
        $pixels[$i * 4 + 3] = 255
    }
    [IO.File]::WriteAllBytes($ddsPath, $header + $pixels)

    $texturePath = '/model/reverse_assist/guide_color.dds'
    $template = [IO.File]::ReadAllBytes((Join-Path $PSScriptRoot '..\src\mod\material\ui\reverse_path\clear.tobj'))
    $prefix = $template[0..47]
    [BitConverter]::GetBytes([uint32]$texturePath.Length).CopyTo($prefix, 40)
    [IO.File]::WriteAllBytes((Join-Path $modelDir 'guide_color.tobj.bin'),
                            $prefix + [Text.Encoding]::ASCII.GetBytes($texturePath))

    $bluePath = Join-Path $modelDir 'guide_blue.dds'
    $bluePixels = [byte[]]::new(64)
    for ($i = 0; $i -lt 16; $i++) {
        $bluePixels[$i * 4] = 255
        $bluePixels[$i * 4 + 1] = 112
        $bluePixels[$i * 4 + 2] = 24
        $bluePixels[$i * 4 + 3] = 255
    }
    [IO.File]::WriteAllBytes($bluePath, $header + $bluePixels)

    $blueTexturePath = '/model/reverse_assist/guide_blue.dds'
    $bluePrefix = $template[0..47]
    [BitConverter]::GetBytes([uint32]$blueTexturePath.Length).CopyTo($bluePrefix, 40)
    [IO.File]::WriteAllBytes((Join-Path $modelDir 'guide_blue.tobj.bin'),
                            $bluePrefix + [Text.Encoding]::ASCII.GetBytes($blueTexturePath))

    $maskPath = Join-Path $modelDir 'guide_mask.dds'
    $maskPixels = [byte[]]::new(64)
    for ($i = 0; $i -lt 16; $i++) {
        $maskPixels[$i * 4] = 255
        $maskPixels[$i * 4 + 1] = 0
        $maskPixels[$i * 4 + 2] = 0
        $maskPixels[$i * 4 + 3] = 0
    }
    [IO.File]::WriteAllBytes($maskPath, $header + $maskPixels)

    $maskTexturePath = '/model/reverse_assist/guide_mask.dds'
    $maskPrefix = $template[0..47]
    [BitConverter]::GetBytes([uint32]$maskTexturePath.Length).CopyTo($maskPrefix, 40)
    [IO.File]::WriteAllBytes((Join-Path $modelDir 'guide_mask.tobj.bin'),
                            $maskPrefix + [Text.Encoding]::ASCII.GetBytes($maskTexturePath))
}

Write-FrameModel 'tractor_box' 7.0 2.55
Write-FrameModel 'trailer_box' 10.2 2.55
# One short collisionless bar is instanced between adjacent samples of the
# calculated swept-area envelope. Two chains of these bars form the left and
# right prediction boundaries without the overlapping rectangle crossbars.
# Runtime boundary samples are spaced by 0.25 m.  A 0.36 m bar closes the
# largest tested high-curvature chord without the >3x overlap of the old
# 0.90 m bar, which crossed neighbouring segments into a false kink/X.
Write-FrameModel -Name 'sweep_edge' -Length 0.36 -Width 0.0 -SingleCenterLine $true
Write-FrameModel -Name 'sweep_edge_blue' -Length 0.36 -Width 0.0 -SingleCenterLine $true -TextureName 'guide_blue' -DiffuseColor '0.050000 0.350000 1.000000'
# Universal-accessory calibration build: use an ordinary always-visible model
# until every original truck's toy slot and local origin have been verified.
Write-FrameModel 'accessory_anchor' 10.2 2.55 0.0 0.0 0.0 $false
# Game-owned model/symbol assets must remain supplied by the base game.
Write-GuideTexture
Write-Output $OutputRoot
