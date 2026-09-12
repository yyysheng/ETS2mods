$ErrorActionPreference = 'Stop'
$root = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
$msbuild = & $vswhere -latest -products * -requires Microsoft.Component.MSBuild -find 'MSBuild\**\Bin\MSBuild.exe' | Select-Object -First 1
if (-not $msbuild) { throw 'MSBuild was not found.' }

$participant = Join-Path $root 'src\compat_test\HookParticipant.vcxproj'
$hostProject = Join-Path $root 'src\compat_test\HookChainHost.vcxproj'
$telemetryHostProject = Join-Path $root 'src\compat_test\TelemetryPairHost.vcxproj'
$legacyRoot = Join-Path $root '..\ETS2_Reverse_Posture_Assistant'
$legacyProject = Join-Path $legacyRoot 'src\entity_runtime\ETS2ReverseEntityRuntime.vcxproj'
$plannerProject = Join-Path $root 'src\game_runtime\ETS2ReversePlannerRuntime.vcxproj'
& (Join-Path $root 'tools\GenerateGameRuntime.ps1') | Out-Null
& $msbuild $legacyProject /t:Rebuild /p:Configuration=Release /p:Platform=x64 /m /v:minimal
if ($LASTEXITCODE -ne 0) { throw 'Legacy runtime build failed.' }
& $msbuild $plannerProject /t:Rebuild /p:Configuration=Release /p:Platform=x64 /m /v:minimal
if ($LASTEXITCODE -ne 0) { throw 'Planner runtime build failed.' }
foreach ($configuration in @('Legacy', 'Planner')) {
    & $msbuild $participant /t:Rebuild /p:Configuration=$configuration /p:Platform=x64 /m /v:minimal
    if ($LASTEXITCODE -ne 0) { throw "$configuration hook participant build failed." }
}
& $msbuild $hostProject /t:Rebuild /p:Configuration=Release /p:Platform=x64 /m /v:minimal
if ($LASTEXITCODE -ne 0) { throw 'Hook-chain host build failed.' }
& $msbuild $telemetryHostProject /t:Rebuild /p:Configuration=Release /p:Platform=x64 /m /v:minimal
if ($LASTEXITCODE -ne 0) { throw 'Telemetry-pair host build failed.' }

$output = Join-Path $root 'build\compat_test'
Push-Location $output
try {
    & '.\HookChainHost.exe' legacy-first
    if ($LASTEXITCODE -ne 0) { throw 'Legacy-first hook-chain test failed.' }
    & '.\HookChainHost.exe' planner-first
    if ($LASTEXITCODE -ne 0) { throw 'Planner-first hook-chain test failed.' }
}
finally { Pop-Location }

$legacyDll = Join-Path $legacyRoot 'build\entity_runtime\ETS2ReverseEntityRuntime.dll'
$plannerDll = Join-Path $root 'build\game_runtime\ETS2ReversePlannerRuntime.dll'
foreach ($loadOrder in @(
    @($legacyDll, $plannerDll, 'legacy-planner'),
    @($plannerDll, $legacyDll, 'planner-legacy'))) {
    foreach ($shutdownOrder in @('forward', 'reverse')) {
        & (Join-Path $output 'TelemetryPairHost.exe') $loadOrder[0] $loadOrder[1] $shutdownOrder
        if ($LASTEXITCODE -ne 0) {
            throw "Actual telemetry pair failed: $($loadOrder[2])/$shutdownOrder."
        }
    }
}

$legacy = Join-Path $legacyRoot 'src\entity_runtime\native_entity_runtime.cpp'
$generated = Join-Path $root 'src\game_runtime\generated\reverse_planner_game_runtime.generated.cpp'
$plannerHeader = Join-Path $root 'src\stage1a_planner.hpp'
$legacyText = [IO.File]::ReadAllText($legacy)
$plannerText = [IO.File]::ReadAllText($generated)
$plannerHeaderText = [IO.File]::ReadAllText($plannerHeader)
$legacyCreates = ([regex]::Matches($legacyText, 'MH_CreateHook\s*\(')).Count
$plannerCreates = ([regex]::Matches($plannerText, 'MH_CreateHook\s*\(')).Count
if ($legacyCreates -ne 9) { throw "Unexpected legacy hook source count: $legacyCreates." }
if ($plannerCreates -ne 1) { throw "Planner must own exactly one hook; found $plannerCreates." }
if ($plannerText -notmatch 'GET_MODULE_HANDLE_EX_FLAG_PIN' -or
    $plannerText -notmatch 'runtime_active\.store\(false' -or
    $plannerText -notmatch 'profile\.executable_sha256 == digest' -or
    $plannerText -notmatch 'executable_file_rva_matches\(executable, rva, bytes, size\)' -or
    $plannerText -notmatch 'sidecar_required_layout_matches\(executable\)' -or
    $plannerText -notmatch 'HookId::vehicle_render_dispatch' -or
    $plannerText -notmatch 'SHGetKnownFolderPath\(FOLDERID_Documents') {
    throw 'Planner residency/pass-through shutdown safeguards are missing.'
}
if ($plannerText -notmatch 'stage1a::plan_parking_terminal_region' -or
    $plannerText -notmatch 'stage1a_best_path\.csv' -or
    $plannerText -match 'stage1a_tractor_rear_tracks\.csv') {
    throw 'Planner must continuously replan from live telemetry, not replay a static track CSV.'
}
$failureGraceClears = ([regex]::Matches(
    $plannerText,
    'now - failure_started_tick >= 1000\)\s*publish_unreachable_x\(\);\s*else\s*clear_frames\(\);')).Count
if ($failureGraceClears -ne 2) {
    throw "The verified-plan failure grace window must be blank; found $failureGraceClears guarded branches."
}
if ($plannerText -notmatch 'now - failure_started_tick >= 1000' -or
    $plannerText -notmatch 'route_distance <= 7\.0' -or
    $plannerText -notmatch 'fmod\(route_distance - 7\.0, 1\.20\) < 0\.65' -or
    $plannerText -notmatch 'publish_cab_arrows\(steering_correction\)' -or
    $plannerText -notmatch 'append_cab_arrows\(frames, steering_correction\)' -or
    $plannerText -notmatch 'parking_entry_pose' -or
    $plannerText -notmatch 'parking_entry_corridor' -or
    $plannerText -match 'arrow_distance' -or
    $plannerText -notmatch 'parking_guidance_phase' -or
    $plannerText -notmatch '15\.0 \* stage0::pi / 180\.0' -or
    $plannerText -notmatch 'stable_terminal_approach' -or
    $plannerText -notmatch '25\.0 \* stage0::pi / 180\.0' -or
    $plannerText -notmatch 'steering_correction_rad' -or
    $plannerText -notmatch 'telemetry\.steering' -or
    $plannerText -notmatch 'steering_independent_route_start_curvature_m_inv' -or
    $plannerText -match 'SCS_TELEMETRY_TRUCK_CHANNEL_speed' -or
    $plannerText -notmatch 'repeated_failed_pose' -or
    $plannerText -notmatch 'refresh_arrow_suffix' -or
    $plannerText -notmatch 'worst-case zero-solution enumeration' -or
    $plannerText -notmatch 'tractor_reference_to_hitch_lateral_m' -or
    $plannerText -notmatch 'silently blank display' -or
    $plannerText -notmatch 'detached tractor-to-kingpin guidance is intentionally' -or
    $plannerText -notmatch '!telemetry\.trailer_connected \|\| !telemetry\.trailer_valid' -or
    $plannerText -notmatch 'request\.mode = stage1a::PlannerMode::trailer_to_parking_target' -or
    $plannerText -match 'next\.mode = stage1a::PlannerMode::tractor_to_target_trailer' -or
    $plannerText -match 'coupling_target_kingpin' -or
    $plannerText -notmatch 'connection_state_changed' -or
    $plannerText -notmatch 'now - failure_started_tick >= 1000' -or
    $plannerText -match 'live_start\.tractor_position\s*=' -or
    $plannerHeaderText -notmatch 'initial_tractor_position_error>0\.05' -or
    $plannerHeaderText -notmatch '0\.75\*stage0::pi/180\.0' -or
    $plannerHeaderText -notmatch 'required_reverse_curvature' -or
    $plannerHeaderText -notmatch 'solve_next_tractor_pose' -or
    $plannerHeaderText -notmatch 'articulated_trailer_curvature' -or
    $plannerHeaderText -notmatch 'plan_steering_profile_fallback' -or
    $plannerHeaderText -notmatch 'steering_arrow_heading_rad' -or
    $plannerHeaderText -notmatch 'steering_independent_route_start_curvature_m_inv' -or
    $plannerHeaderText -notmatch 'articulation_hard_limit_rad = 70\.0' -or
    $plannerHeaderText -notmatch '1\.0\*stage0::pi/180\.0' -or
    $plannerHeaderText -notmatch '36\.0\*stage0::pi/180\.0' -or
    $plannerHeaderText -notmatch 'candidate_grid_size' -or
    $plannerHeaderText -notmatch 'physical_seed_pair_limit' -or
    $plannerHeaderText -notmatch 'const int grid=std::clamp' -or
    $plannerHeaderText -notmatch 'parking_terminal_checkpoints' -or
    $plannerHeaderText -notmatch 'plan_parking_terminal_region' -or
    $plannerHeaderText -match 'quintic_grid' -or
    $plannerHeaderText -notmatch 'std::abs\(tractor_curvature\)>max_tractor_curvature' -or
    $plannerHeaderText -notmatch 'maximum_lateral_error_m=0\.15' -or
    $plannerHeaderText -notmatch 'maximum_heading_error_rad=5\.0') {
    throw 'Planner guidance display state-machine safeguards are missing.'
}

[pscustomobject]@{
    Result = 'PASS'
    LegacySourceHookSites = $legacyCreates
    PlannerSourceHookSites = $plannerCreates
    LegacySourceSha256 = (Get-FileHash -LiteralPath $legacy -Algorithm SHA256).Hash
    PlannerDllSha256 = (Get-FileHash -LiteralPath $plannerDll -Algorithm SHA256).Hash
} | Format-List
