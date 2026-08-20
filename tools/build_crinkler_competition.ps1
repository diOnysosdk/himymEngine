param(
    [string]$CrinklerPath = $env:CRINKLER_EXE,
    [string]$SourceBuildDirectory = "build",
    [string]$PackedManifestDirectory = "build",
    [string]$CompetitionBuildDirectory = "build\crinkler-win32",
    [string]$OutputDirectory = "",
    [string]$Configuration = "Release",
    [ValidateSet("INSTANT", "FAST", "SLOW", "VERYSLOW")]
    [string]$CompetitionMode = "SLOW",
    [ValidateRange(0, 2097151)]
    [int]$SizeLimitKB = 0
)

$ErrorActionPreference = "Stop"

function Invoke-Native {
    param([string]$Program, [string[]]$Arguments)
    & $Program @Arguments
    if ($LASTEXITCODE -ne 0) { throw "$Program failed with exit code $LASTEXITCODE" }
}

function Invoke-NativeWithExitCode {
    param([string]$Program, [string[]]$Arguments)
    & $Program @Arguments 2>&1 | ForEach-Object { Write-Host $_ }
    return $LASTEXITCODE
}

function Split-CrinklerSectionParts {
    param(
        [string[]]$Lines,
        [string]$PartName,
        [string]$NextPartName
    )

    if ($Lines -contains "# $($PartName)4 sections") {
        return $null
    }
    $part_header = [Array]::IndexOf($Lines, "# $PartName sections")
    $next_header = [Array]::IndexOf($Lines, "# $NextPartName sections")
    if ($part_header -lt 0 -or $next_header -le ($part_header + 2)) {
        return $null
    }

    $sections = @($Lines[($part_header + 1)..($next_header - 1)] |
        Where-Object {
            -not [string]::IsNullOrWhiteSpace($_) -and
            $_ -notmatch "^# $([regex]::Escape($PartName))\d* sections$"
        })
    $rewritten = @()
    if ($part_header -gt 0) {
        $rewritten += $Lines[0..($part_header - 1)]
    }
    for ($part = 0; $part -lt 4; ++$part) {
        $split_part_name = if ($part -eq 0) { $PartName } else { "$PartName$($part + 1)" }
        $rewritten += "# $split_part_name sections"
        for ($index = $part; $index -lt $sections.Count; $index += 4) {
            $rewritten += $sections[$index]
        }
        $rewritten += ""
    }
    $rewritten += $Lines[$next_header..($Lines.Count - 1)]
    Write-Host "[competition] Split $($sections.Count) $($PartName.ToLowerInvariant()) sections across four parts."
    return ,$rewritten
}

function Split-CrinklerOversizedParts {
    param([string]$ReusePath)

    $lines = @(Get-Content -LiteralPath $ReusePath)
    $changed = $false
    $split_lines = Split-CrinklerSectionParts $lines "Code" "Data"
    if ($null -ne $split_lines) {
        $lines = @($split_lines)
        $changed = $true
    }
    $split_lines = Split-CrinklerSectionParts $lines "Text" "Uninitialized"
    if ($null -ne $split_lines) {
        $lines = @($split_lines)
        $changed = $true
    }
    if (-not $changed) {
        return $false
    }
    Set-Content -LiteralPath $ReusePath -Value $lines -Encoding ASCII
    return $true
}

function Resolve-RepositoryPath {
    param([string]$Path)
    if ([System.IO.Path]::IsPathRooted($Path)) {
        return [System.IO.Path]::GetFullPath($Path)
    }
    return [System.IO.Path]::GetFullPath((Join-Path $repository_root $Path))
}

$repository_root = Split-Path -Parent $PSScriptRoot
$source_build = Resolve-RepositoryPath $SourceBuildDirectory
$manifest_dir = Resolve-RepositoryPath $PackedManifestDirectory
$competition_build = Resolve-RepositoryPath $CompetitionBuildDirectory
if ([string]::IsNullOrWhiteSpace($OutputDirectory)) {
    $OutputDirectory = Join-Path $repository_root "build\competition-output"
}
$output_dir = Resolve-RepositoryPath $OutputDirectory

foreach ($manifest in @("packed_assets.h", "packed_features.cmake")) {
    $manifest_path = Join-Path $manifest_dir $manifest
    if (-not (Test-Path -LiteralPath $manifest_path -PathType Leaf)) {
        throw "Packed manifest missing: $manifest_path"
    }
}

# The supported x64 packed executable is always built and copied first. Any
# later Crinkler failure therefore leaves a usable final artifact in place.
Invoke-Native cmake @("--build", $source_build, "--config", $Configuration,
    "--target", "minimal_intro_packed")
$normal_exe = Join-Path $source_build "bin\$Configuration\minimal_intro_packed.exe"
if (-not (Test-Path -LiteralPath $normal_exe -PathType Leaf)) {
    throw "Normal packed executable was not produced: $normal_exe"
}
New-Item -ItemType Directory -Force -Path $output_dir | Out-Null
Copy-Item -LiteralPath $normal_exe -Destination (Join-Path $output_dir "minimal_intro_packed.exe") -Force
Write-Host "[competition] Normal x64 artifact preserved."

if ([string]::IsNullOrWhiteSpace($CrinklerPath)) {
    $path_command = Get-Command crinkler.exe -CommandType Application -ErrorAction SilentlyContinue
    if ($path_command) {
        $CrinklerPath = $path_command.Source
    } else {
        $local_crinkler = Join-Path $repository_root "tools\crinkler\crinkler.exe"
        if (Test-Path -LiteralPath $local_crinkler -PathType Leaf) {
            $CrinklerPath = $local_crinkler
        }
    }
}
if ([string]::IsNullOrWhiteSpace($CrinklerPath)) {
    throw "Crinkler was not found. Pass -CrinklerPath, set CRINKLER_EXE, add it to PATH, or place it at tools\crinkler\crinkler.exe."
}
$CrinklerPath = Resolve-RepositoryPath $CrinklerPath
if (-not (Test-Path -LiteralPath $CrinklerPath -PathType Leaf)) {
    throw "Crinkler executable not found: $CrinklerPath"
}

$reuse_file = Join-Path $competition_build "crinkler_reuse.txt"
$reuse_fingerprint_file = Join-Path $competition_build "crinkler_reuse.manifest.sha256"
$manifest_fingerprint = (@("packed_assets.h", "packed_features.cmake") | ForEach-Object {
    (Get-FileHash -LiteralPath (Join-Path $manifest_dir $_) -Algorithm SHA256).Hash
}) -join "`n"
if (Test-Path -LiteralPath $reuse_file -PathType Leaf) {
    $stored_fingerprint = if (Test-Path -LiteralPath $reuse_fingerprint_file -PathType Leaf) {
        Get-Content -LiteralPath $reuse_fingerprint_file -Raw
    } else { "" }
    if ($stored_fingerprint.Trim() -ne $manifest_fingerprint.Trim()) {
        Remove-Item -LiteralPath $reuse_file -Force
        Write-Host "[competition] Packed manifests changed; regenerating Crinkler reuse layout."
    }
}
$bootstrap_layout = -not (Test-Path -LiteralPath $reuse_file -PathType Leaf)
$configure_mode = if ($bootstrap_layout) { "FAST" } else { $CompetitionMode }
Invoke-Native cmake @("-S", $repository_root, "-B", $competition_build,
    "-G", "Visual Studio 17 2022", "-A", "Win32",
    "-DHIMYM_RELEASE_PROFILE=INTRO",
    "-DHIMYM_PACKED_MANIFEST_DIR=$manifest_dir",
    "-DHIMYM_CRINKLER_EXECUTABLE=$CrinklerPath",
    "-DHIMYM_CRINKLER_COMPMODE=$configure_mode")
$build_arguments = @("--build", $competition_build, "--config", $Configuration,
    "--target", "minimal_intro_packed")
$build_exit_code = Invoke-NativeWithExitCode cmake $build_arguments
if ($build_exit_code -ne 0 -and (Test-Path -LiteralPath $reuse_file -PathType Leaf)) {
    if (Split-CrinklerOversizedParts $reuse_file) {
        Invoke-Native cmake @("-S", $repository_root, "-B", $competition_build,
            "-G", "Visual Studio 17 2022", "-A", "Win32",
            "-DHIMYM_RELEASE_PROFILE=INTRO",
            "-DHIMYM_PACKED_MANIFEST_DIR=$manifest_dir",
            "-DHIMYM_CRINKLER_EXECUTABLE=$CrinklerPath",
            "-DHIMYM_CRINKLER_COMPMODE=$CompetitionMode")
        Invoke-Native cmake $build_arguments
        $build_exit_code = 0
    }
}
if ($build_exit_code -ne 0) {
    throw "Crinkler build failed with exit code $build_exit_code"
}
Set-Content -LiteralPath $reuse_fingerprint_file -Value $manifest_fingerprint -Encoding ASCII

$competition_exe = Join-Path $competition_build "bin\$Configuration\minimal_intro_competition.exe"
if (-not (Test-Path -LiteralPath $competition_exe -PathType Leaf)) {
    throw "Crinkler competition executable was not produced: $competition_exe"
}
$competition_size = (Get-Item -LiteralPath $competition_exe).Length
if ($SizeLimitKB -gt 0) {
    $size_limit_bytes = $SizeLimitKB * 1KB
    if ($competition_size -gt $size_limit_bytes) {
        $over_by = $competition_size - $size_limit_bytes
        throw "Crinkler competition executable is $competition_size bytes, exceeding the $SizeLimitKB KiB budget by $over_by bytes."
    }
    Write-Host "[competition] Size budget PASS: $competition_size / $size_limit_bytes bytes ($SizeLimitKB KiB)."
}
Copy-Item -LiteralPath $competition_exe -Destination $output_dir -Force
$report = Join-Path $competition_build "crinkler_report.html"
if (Test-Path -LiteralPath $report -PathType Leaf) {
    Copy-Item -LiteralPath $report -Destination $output_dir -Force
}

Write-Host "[competition] PASS"
Get-ChildItem -LiteralPath $output_dir -File |
    Where-Object { $_.Name -in @("minimal_intro_packed.exe", "minimal_intro_competition.exe", "crinkler_report.html") } |
    Select-Object Name, Length
