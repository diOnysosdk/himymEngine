param([string]$Configuration = "Release", [switch]$KeepArtifacts)

$ErrorActionPreference = "Stop"

function Invoke-Native {
    param([string]$Program, [string[]]$Arguments)
    & $Program @Arguments
    if ($LASTEXITCODE -ne 0) { throw "$Program failed with exit code $LASTEXITCODE" }
}

function Invoke-ExpectedFailure {
    param([string]$Program, [string[]]$Arguments, [string]$Description)
    $output = & $Program @Arguments 2>&1
    if ($LASTEXITCODE -eq 0) { throw "$Description unexpectedly succeeded" }
    Write-Host "  PASS expected failure: $Description"
}

$repository_root = Split-Path -Parent $PSScriptRoot
$primary_build = Join-Path $repository_root "build"
$pack_cli = Join-Path $primary_build "bin\$Configuration\pack_cli.exe"
$test_root = Join-Path $primary_build "packed_pipeline_test"
$pipeline_build = Join-Path $test_root "cmake"
$workspace = Join-Path $test_root "project"

$resolved_build = [System.IO.Path]::GetFullPath($primary_build)
$resolved_test = [System.IO.Path]::GetFullPath($test_root)
if (-not $resolved_test.StartsWith($resolved_build + [System.IO.Path]::DirectorySeparatorChar,
        [System.StringComparison]::OrdinalIgnoreCase)) {
    throw "Refusing to use a pipeline test directory outside build: $resolved_test"
}

Invoke-Native cmake @("--build", $primary_build, "--config", $Configuration, "--target", "pack_cli")
if (Test-Path -LiteralPath $test_root) { Remove-Item -LiteralPath $test_root -Recurse -Force }
New-Item -ItemType Directory -Force -Path $workspace, $pipeline_build | Out-Null

$cases = @(
    @{ Name = "empty"; Cues = "[metadata]`n"; Asset = $false },
    @{ Name = "shader"; Cues = "[shader_cues]`n0|0|1`n"; Asset = $false },
    @{ Name = "xm"; Cues = "[music_cues]`nmusic|asset.bin`n"; Asset = $true },
    @{ Name = "pixel"; Cues = "[pixel_cues]`npixels|asset.bin`n"; Asset = $true },
    @{ Name = "particle"; Cues = "[pixel_emitter_cues]`nnone|unused|1`n"; Asset = $false },
    @{ Name = "procedural_mesh"; Cues = "[mesh_cues]`nmesh||0`n"; Asset = $false },
    @{ Name = "gltf"; Cues = "[mesh_cues]`nmodel|asset.bin|4`n"; Asset = $true }
)

foreach ($case in $cases) {
    Write-Host "Testing packed pipeline: $($case.Name)"
    $cues_path = Join-Path $workspace "cues.txt"
    $asset_path = Join-Path $workspace "asset.bin"
    Set-Content -LiteralPath $cues_path -Value $case.Cues -Encoding Ascii -NoNewline
    if ($case.Asset) {
        Set-Content -LiteralPath $asset_path -Value "pipeline-test-asset" -Encoding Ascii -NoNewline
    } elseif (Test-Path -LiteralPath $asset_path) {
        Remove-Item -LiteralPath $asset_path -Force
    }
    Invoke-Native $pack_cli @($cues_path, (Join-Path $pipeline_build "packed_assets.h"),
        (Join-Path $workspace "pack_cache.txt"), $workspace)
    Invoke-Native cmake @("-S", $repository_root, "-B", $pipeline_build)
    Invoke-Native cmake @("--build", $pipeline_build, "--config", $Configuration,
        "--target", "minimal_intro_packed")
    $packed_executable = Join-Path $pipeline_build "bin\$Configuration\minimal_intro_packed.exe"
    if (-not (Test-Path -LiteralPath $packed_executable -PathType Leaf)) {
        throw "Packed executable was not produced for $($case.Name)"
    }
}

Write-Host "Testing missing required asset"
$cues_path = Join-Path $workspace "cues.txt"
Set-Content -LiteralPath $cues_path -Value "[music_cues]`nmissing|does-not-exist.xm`n" -Encoding Ascii -NoNewline
Invoke-ExpectedFailure $pack_cli @($cues_path, (Join-Path $pipeline_build "packed_assets.h"),
    (Join-Path $workspace "pack_cache.txt"), $workspace) "packing a missing required asset"

Write-Host "Testing mismatched generated manifests"
Set-Content -LiteralPath $cues_path -Value "[music_cues]`nmusic|asset.bin`n" -Encoding Ascii -NoNewline
Set-Content -LiteralPath (Join-Path $workspace "asset.bin") -Value "pipeline-test-asset" -Encoding Ascii -NoNewline
Invoke-Native $pack_cli @($cues_path, (Join-Path $pipeline_build "packed_assets.h"),
    (Join-Path $workspace "pack_cache.txt"), $workspace)
$features_path = Join-Path $pipeline_build "packed_features.cmake"
$features = (Get-Content -LiteralPath $features_path -Raw).Replace(
    "set(HIMYM_PACKED_USE_XM ON)", "set(HIMYM_PACKED_USE_XM OFF)")
Set-Content -LiteralPath $features_path -Value $features -Encoding Ascii -NoNewline
Invoke-Native cmake @("-S", $repository_root, "-B", $pipeline_build)
Invoke-ExpectedFailure cmake @("--build", $pipeline_build, "--config", $Configuration,
    "--target", "minimal_intro_packed") "building with stale/mismatched feature manifests"

if (-not $KeepArtifacts) { Remove-Item -LiteralPath $test_root -Recurse -Force }
Write-Host "[packed_pipeline_test] PASS"
