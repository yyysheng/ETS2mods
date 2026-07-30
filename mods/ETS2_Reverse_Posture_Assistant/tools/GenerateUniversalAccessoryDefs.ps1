param(
    [string]$ModRoot = (Join-Path $PSScriptRoot '..\src\mod')
)

$ErrorActionPreference = 'Stop'

$toyhangTrucks = @(
    'daf.xf',
    'daf.xf_euro6',
    'iveco.hiway',
    'iveco.stralis',
    'man.tgx',
    'man.tgx_euro6',
    'mercedes.actros',
    'mercedes.actros2014',
    'renault.magnum',
    'renault.premium',
    'renault.t',
    'scania.r',
    'scania.r_2016',
    'scania.s_2016',
    'scania.streamline',
    'volvo.fh16',
    'volvo.fh16_2012',
    'daf.2021',
    'daf.xd',
    'iveco.sway',
    'man.tgx_2020',
    'renault.etech_t',
    'scania.s_2024e',
    'volvo.fh_2021',
    'volvo.fh_2024'
)

function Write-AccessoryDefinition {
    param(
        [string]$Truck,
        [string]$Slot,
        [string]$Icon
    )

    $directory = Join-Path $ModRoot "def\vehicle\truck\$Truck\accessory\$Slot"
    $null = New-Item -ItemType Directory -Path $directory -Force
    $content = @"
SiiNunit
{
accessory_addon_int_data : revassist.$Truck.$Slot
{
	name: "Reverse Assist Anchor"
	price: 1
	unlock: 0
	icon: "$Icon"
	part_type: aftermarket

	interior_model: "/model/reverse_assist/accessory_anchor.pmd"
	exterior_model: "/model/reverse_assist/accessory_anchor.pmd"
}
}
"@
    [IO.File]::WriteAllText((Join-Path $directory 'revassist.sii'), $content)
}

foreach ($truck in $toyhangTrucks) {
    Write-AccessoryDefinition $truck 'toyhang' 'truck/upgrade/interior_decors/toyhang/airship_gy'

    $legacyToybig = Join-Path $ModRoot "def\vehicle\truck\$truck\accessory\toybig\revassist.sii"
    if (Test-Path -LiteralPath $legacyToybig) {
        Remove-Item -LiteralPath $legacyToybig -Force
    }
}

# The original DAF XF Electric interior has no toyhang locator.
Write-AccessoryDefinition 'daf.xf_electric' 'toystand' 'truck/upgrade/interior_decors/toystand/gr_owl'

Write-Output "Generated $($toyhangTrucks.Count + 1) universal accessory definitions."
