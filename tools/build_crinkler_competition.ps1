param(
    [string]$CrinklerPath = $env:CRINKLER_EXE,
    [string]$SourceBuildDirectory = "build",
    [string]$PackedManifestDirectory = "build",
    [string]$CompetitionBuildDirectory = "build\crinkler-win32",
    [string]$OutputDirectory = "",
    [string]$Configuration = "Release",
    [ValidateSet("INSTANT", "FAST", "SLOW", "VERYSLOW")]
    [string]$CompetitionMode = "SLOW"
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

function Split-CrinklerCodeParts {
    param([string]$ReusePath)

    $lines = @(Get-Content -LiteralPath $ReusePath)
    if ($lines -contains "# Code4 sections") {
        return $false
    }
    $code_header = [Array]::IndexOf($lines, "# Code sections")
    $data_header = [Array]::IndexOf($lines, "# Data sections")
    if ($code_header -lt 0 -or $data_header -le ($code_header + 2)) {
        throw "Crinkler reuse file has no splittable Code section: $ReusePath"
    }

    $code_sections = @($lines[($code_header + 1)..($data_header - 1)] |
        Where-Object {
            -not [string]::IsNullOrWhiteSpace($_) -and $_ -notmatch '^# Code\d* sections$'
        })
    $rewritten = @()
    $rewritten += $lines[0..($code_header - 1)]
    for ($part = 0; $part -lt 4; ++$part) {
        $part_name = if ($part -eq 0) { "Code" } else { "Code$($part + 1)" }
        $rewritten += "# $part_name sections"
        for ($index = $part; $index -lt $code_sections.Count; $index += 4) {
            $rewritten += $code_sections[$index]
        }
        $rewritten += ""
    }
    $rewritten += $lines[$data_header..($lines.Count - 1)]
    Set-Content -LiteralPath $ReusePath -Value $rewritten -Encoding ASCII
    Write-Host "[competition] Split $($code_sections.Count) code sections across four code parts."
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
    if (Split-CrinklerCodeParts $reuse_file) {
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
Copy-Item -LiteralPath $competition_exe -Destination $output_dir -Force
$report = Join-Path $competition_build "crinkler_report.html"
if (Test-Path -LiteralPath $report -PathType Leaf) {
    Copy-Item -LiteralPath $report -Destination $output_dir -Force
}

Write-Host "[competition] PASS"
Get-ChildItem -LiteralPath $output_dir -File |
    Where-Object { $_.Name -in @("minimal_intro_packed.exe", "minimal_intro_competition.exe", "crinkler_report.html") } |
    Select-Object Name, Length
