param([string]$Configuration = "Release", [switch]$KeepArtifacts)

$ErrorActionPreference = "Stop"

function Invoke-Native {
    param([string]$Program, [string[]]$Arguments)
    & $Program @Arguments
    if ($LASTEXITCODE -ne 0) { throw "$Program failed with exit code $LASTEXITCODE" }
}

$repository_root = Split-Path -Parent $PSScriptRoot
$build_root = Join-Path $repository_root "build"
$test_root = Join-Path $build_root "editor_pipeline_test"
$assets_root = Join-Path $test_root "project_assets"
$cmake_root = Join-Path $test_root "cmake"
$editor_cli = Join-Path $build_root "bin\$Configuration\editor_pipeline_cli.exe"
$pack_cli = Join-Path $build_root "bin\$Configuration\pack_cli.exe"

$resolved_build = [System.IO.Path]::GetFullPath($build_root)
$resolved_test = [System.IO.Path]::GetFullPath($test_root)
if (-not $resolved_test.StartsWith($resolved_build + [System.IO.Path]::DirectorySeparatorChar,
        [System.StringComparison]::OrdinalIgnoreCase)) {
    throw "Refusing to use an editor pipeline test directory outside build: $resolved_test"
}

Invoke-Native cmake @("--build", $build_root, "--config", $Configuration,
    "--target", "editor_pipeline_cli", "pack_cli")
if (Test-Path -LiteralPath $test_root) { Remove-Item -LiteralPath $test_root -Recurse -Force }
New-Item -ItemType Directory -Force -Path $assets_root, $cmake_root | Out-Null
Copy-Item -Path (Join-Path $repository_root "Salute\project_assets\*") -Destination $assets_root

$saved_project = Join-Path $test_root "project.json"
$exported_cues = Join-Path $test_root "cues.txt"
Invoke-Native $editor_cli @(
    (Join-Path $repository_root "Salute\project.json"),
    $saved_project,
    $exported_cues
)
Invoke-Native $pack_cli @(
    $exported_cues,
    (Join-Path $cmake_root "packed_assets.h"),
    (Join-Path $test_root "pack_cache.txt"),
    $test_root
)
Invoke-Native cmake @("-S", $repository_root, "-B", $cmake_root)
Invoke-Native cmake @("--build", $cmake_root, "--config", $Configuration,
    "--target", "minimal_intro_packed")

$packed_executable = Join-Path $cmake_root "bin\$Configuration\minimal_intro_packed.exe"
if (-not (Test-Path -LiteralPath $packed_executable -PathType Leaf)) {
    throw "Packed runtime was not generated: $packed_executable"
}

Write-Host "[editor_pipeline_test] PASS"
Write-Host "  Project: $saved_project"
Write-Host "  Cues:    $exported_cues"
Write-Host "  Runtime: $packed_executable"
if (-not $KeepArtifacts) { Remove-Item -LiteralPath $test_root -Recurse -Force }
exit 0
