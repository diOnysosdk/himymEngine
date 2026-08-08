param(
    [string]$BuildDirectory = "build",
    [string]$Configuration = "Release",
    [string]$OutputPath = ""
)

$ErrorActionPreference = "Stop"
$repository_root = Split-Path -Parent $PSScriptRoot
$build_root = [System.IO.Path]::GetFullPath((Join-Path $repository_root $BuildDirectory))
$header_path = Join-Path $build_root "packed_assets.h"
$features_path = Join-Path $build_root "packed_features.cmake"
$exe_path = Join-Path $build_root "bin\$Configuration\minimal_intro_packed.exe"
$map_path = Join-Path $build_root "bin\$Configuration\minimal_intro_packed.map"
if (-not $OutputPath) { $OutputPath = Join-Path $build_root "packed_size_report.txt" }
$OutputPath = [System.IO.Path]::GetFullPath($OutputPath)

foreach ($required in @($header_path, $features_path)) {
    if (-not (Test-Path -LiteralPath $required -PathType Leaf)) {
        throw "Required packed manifest not found: $required"
    }
}

& cmake -S $repository_root -B $build_root -DHIMYM_PACKED_LINK_MAPS=ON
if ($LASTEXITCODE -ne 0) { throw "CMake configure failed with exit code $LASTEXITCODE" }
& cmake --build $build_root --config $Configuration --target minimal_intro_packed
if ($LASTEXITCODE -ne 0) { throw "Packed runtime build failed with exit code $LASTEXITCODE" }

$header = Get-Content -LiteralPath $header_path -Raw
$unique_asset_bytes = 0L
foreach ($match in [regex]::Matches($header, 'CRC32=0x[0-9A-Fa-f]+\s+size=(\d+)')) {
    $unique_asset_bytes += [long]$match.Groups[1].Value
}
$cues_match = [regex]::Match($header, 'kPackedCuesSize\s*=\s*(\d+)')
$packed_cue_bytes = if ($cues_match.Success) { [long]$cues_match.Groups[1].Value } else { 0L }
$embedded_bytes = $unique_asset_bytes + $packed_cue_bytes
$exe_bytes = (Get-Item -LiteralPath $exe_path).Length
$non_asset_bytes = [Math]::Max(0L, $exe_bytes - $embedded_bytes)
$map_bytes = if (Test-Path -LiteralPath $map_path) { (Get-Item -LiteralPath $map_path).Length } else { 0L }
$header_bytes = (Get-Item -LiteralPath $header_path).Length

$features = @()
foreach ($line in Get-Content -LiteralPath $features_path) {
    if ($line -match '^set\(HIMYM_PACKED_USE_([A-Z]+)\s+(ON|OFF)\)') {
        $features += "$($Matches[1])=$($Matches[2])"
    }
}

$report = @(
    "HiMYM packed size report"
    "Generated: $([DateTime]::Now.ToString('yyyy-MM-dd HH:mm:ss zzz'))"
    "Configuration: $Configuration"
    "Features: $($features -join ', ')"
    ""
    "Executable bytes:          $exe_bytes"
    "Unique embedded assets:    $unique_asset_bytes"
    "Embedded cues:             $packed_cue_bytes"
    "Total embedded payload:    $embedded_bytes"
    "Non-asset EXE remainder:   $non_asset_bytes"
    "Generated header bytes:    $header_bytes"
    "Linker map bytes:          $map_bytes"
    ""
    "Executable: $exe_path"
    "Packed header: $header_path"
    "Feature manifest: $features_path"
    "Linker map: $map_path"
    ""
    "Note: non-asset EXE remainder is EXE size minus raw embedded payload; it"
    "includes PE headers, alignment, code, constants, imports, and linker overhead."
)
$report | Set-Content -LiteralPath $OutputPath -Encoding UTF8
$report | ForEach-Object { Write-Host $_ }
Write-Host "Report: $OutputPath"
