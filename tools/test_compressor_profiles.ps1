param(
    [string]$FixtureBuild = "build\rectruitro_transfer_validation\cmake",
    [string]$Configuration = "Release",
    [switch]$KeepArtifacts
)

$ErrorActionPreference = "Stop"

function Invoke-Native {
    param([string]$Program, [string[]]$Arguments)
    & $Program @Arguments
    if ($LASTEXITCODE -ne 0) { throw "$Program failed with exit code $LASTEXITCODE" }
}

$repository_root = Split-Path -Parent $PSScriptRoot
if (-not [System.IO.Path]::IsPathRooted($FixtureBuild)) {
    $FixtureBuild = Join-Path $repository_root $FixtureBuild
}
$header_source = Join-Path $FixtureBuild "packed_assets.h"
$features_source = Join-Path $FixtureBuild "packed_features.cmake"
if (-not (Test-Path -LiteralPath $header_source -PathType Leaf) -or
    -not (Test-Path -LiteralPath $features_source -PathType Leaf)) {
    throw "Packed fixture manifests are missing from: $FixtureBuild"
}
if (-not (Get-Command upx -ErrorAction SilentlyContinue)) {
    throw "UPX was not found on PATH"
}

$build_root = Join-Path $repository_root "build"
$output_root = Join-Path $build_root "compressor_profiles"
$resolved_build = [System.IO.Path]::GetFullPath($build_root)
$resolved_output = [System.IO.Path]::GetFullPath($output_root)
if (-not $resolved_output.StartsWith($resolved_build + [System.IO.Path]::DirectorySeparatorChar,
        [System.StringComparison]::OrdinalIgnoreCase)) {
    throw "Refusing to use compressor output outside build: $resolved_output"
}
if (Test-Path -LiteralPath $output_root) {
    Remove-Item -LiteralPath $output_root -Recurse -Force
}

$results = @()
foreach ($profile in @("GENERAL", "INTRO")) {
    $profile_root = Join-Path $output_root $profile.ToLowerInvariant()
    New-Item -ItemType Directory -Force -Path $profile_root | Out-Null
    Copy-Item -LiteralPath $header_source, $features_source -Destination $profile_root

    Invoke-Native cmake @("-S", $repository_root, "-B", $profile_root,
        "-DHIMYM_RELEASE_PROFILE=$profile")
    Invoke-Native cmake @("--build", $profile_root, "--config", $Configuration,
        "--target", "minimal_intro_packed")

    $built_exe = Join-Path $profile_root "bin\$Configuration\minimal_intro_packed.exe"
    $raw_exe = Join-Path $profile_root "rectruitro_$($profile.ToLowerInvariant())_raw.exe"
    $packed_exe = Join-Path $profile_root "rectruitro_$($profile.ToLowerInvariant())_upx.exe"
    Copy-Item -LiteralPath $built_exe -Destination $raw_exe
    Copy-Item -LiteralPath $built_exe -Destination $packed_exe
    Invoke-Native upx @("--best", "--lzma", $packed_exe)
    Invoke-Native upx @("-t", $packed_exe)

    $raw_bytes = (Get-Item -LiteralPath $raw_exe).Length
    $packed_bytes = (Get-Item -LiteralPath $packed_exe).Length
    $results += [pscustomobject]@{
        Profile = $profile
        RawBytes = $raw_bytes
        UpxBytes = $packed_bytes
        SavedBytes = $raw_bytes - $packed_bytes
        RatioPercent = [math]::Round(100.0 * $packed_bytes / $raw_bytes, 2)
        RawSha256 = (Get-FileHash -Algorithm SHA256 -LiteralPath $raw_exe).Hash
        UpxSha256 = (Get-FileHash -Algorithm SHA256 -LiteralPath $packed_exe).Hash
    }
}

$report_path = Join-Path $output_root "compressor_size_report.txt"
$report = @(
    "HiMYM compressor profile report"
    "Generated: $([DateTime]::Now.ToString('yyyy-MM-dd HH:mm:ss zzz'))"
    "Fixture: $FixtureBuild"
    "Compressor: $((upx --version | Select-Object -First 1)) --best --lzma"
    ""
    ($results | Format-Table Profile, RawBytes, UpxBytes, SavedBytes, RatioPercent -AutoSize | Out-String).TrimEnd()
    ""
)
foreach ($result in $results) {
    $report += "$($result.Profile) raw SHA256: $($result.RawSha256)"
    $report += "$($result.Profile) UPX SHA256: $($result.UpxSha256)"
}
$report | Set-Content -LiteralPath $report_path -Encoding UTF8

Write-Host "[compressor_profiles] PASS"
$results | Format-Table Profile, RawBytes, UpxBytes, SavedBytes, RatioPercent -AutoSize
Write-Host "Report: $report_path"
if (-not $KeepArtifacts) {
    Get-ChildItem -LiteralPath $output_root -Directory | Remove-Item -Recurse -Force
}

