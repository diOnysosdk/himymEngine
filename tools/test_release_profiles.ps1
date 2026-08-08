param([string]$Configuration = "Release", [switch]$KeepArtifacts)

$ErrorActionPreference = "Stop"

function Invoke-Native {
    param([string]$Program, [string[]]$Arguments)
    & $Program @Arguments
    if ($LASTEXITCODE -ne 0) { throw "$Program failed with exit code $LASTEXITCODE" }
}

$repository_root = Split-Path -Parent $PSScriptRoot
$build_root = Join-Path $repository_root "build"
$fixture_build = Join-Path $build_root "editor_pipeline_test\cmake"
$profile_root = Join-Path $build_root "release_profiles"
$header_source = Join-Path $fixture_build "packed_assets.h"
$features_source = Join-Path $fixture_build "packed_features.cmake"
if (-not (Test-Path -LiteralPath $header_source -PathType Leaf)) {
    throw "All-cue fixture is missing. Run tools\test_editor_pipeline.ps1 -KeepArtifacts first."
}

$resolved_build = [System.IO.Path]::GetFullPath($build_root)
$resolved_profiles = [System.IO.Path]::GetFullPath($profile_root)
if (-not $resolved_profiles.StartsWith($resolved_build + [System.IO.Path]::DirectorySeparatorChar,
        [System.StringComparison]::OrdinalIgnoreCase)) {
    throw "Refusing to use a profile test directory outside build: $resolved_profiles"
}
if (Test-Path -LiteralPath $profile_root) { Remove-Item -LiteralPath $profile_root -Recurse -Force }

$results = @()
foreach ($profile in @("GENERAL", "INTRO")) {
    $profile_build = Join-Path $profile_root $profile.ToLowerInvariant()
    New-Item -ItemType Directory -Force -Path $profile_build | Out-Null
    Copy-Item -LiteralPath $header_source, $features_source -Destination $profile_build
    Invoke-Native cmake @("-S", $repository_root, "-B", $profile_build,
        "-DHIMYM_RELEASE_PROFILE=$profile", "-DHIMYM_PACKED_LINK_MAPS=ON")
    Invoke-Native cmake @("--build", $profile_build, "--config", $Configuration,
        "--target", "minimal_intro_packed")
    & (Join-Path $PSScriptRoot "report_packed_size.ps1") `
        -BuildDirectory (Resolve-Path -LiteralPath $profile_build).Path `
        -Configuration $Configuration
    if ($LASTEXITCODE -ne 0) { throw "Size report failed for $profile" }
    $exe = Join-Path $profile_build "bin\$Configuration\minimal_intro_packed.exe"
    $results += [pscustomobject]@{ Profile = $profile; Bytes = (Get-Item -LiteralPath $exe).Length }
}

Write-Host "[release_profile_test] PASS"
$results | Format-Table -AutoSize
if (-not $KeepArtifacts) { Remove-Item -LiteralPath $profile_root -Recurse -Force }
exit 0
